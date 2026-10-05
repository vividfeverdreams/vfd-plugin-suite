#pragma once

#include "BackScratchEditor.h"
#include "EditedProcessorTests.h"
#include "VstTimingTests.h"
#include <memory>

namespace backScratch::vstEditedTests {

inline juce::MouseEvent event(juce::AudioProcessorEditor& editor, juce::Point<float> position,
                              juce::Point<float> down, bool dragged, bool released = false)
{
    const auto viewport = BackScratchEditor::designViewport();
    const auto transform = juce::AffineTransform::translation(-viewport.getX(), -viewport.getY())
        .scaled(static_cast<float>(editor.getWidth()) / viewport.getWidth(),
                static_cast<float>(editor.getHeight()) / viewport.getHeight());
    const auto time = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(), position.transformedBy(transform),
        juce::ModifierKeys(released ? 0 : juce::ModifierKeys::leftButtonModifier),
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &editor, &editor, time,
        down.transformedBy(transform), time, 1, dragged};
}

inline void click(juce::AudioProcessorEditor& editor, BackScratchEditor::Control control)
{
    const auto point = BackScratchEditor::controlBounds(control).getCentre();
    // Exercise the plugin's real editor implementation without relying on a
    // host-specific native-window event bridge.
    editor.mouseDown(event(editor, point, point, false));
    editor.mouseUp(event(editor, point, point, false, true));
}

inline juce::Point<float> graphPoint(float cell, float depth)
{
    const auto graph = BackScratchEditor::patternBounds();
    return {graph.getX() + cell / patternGridDivisions * graph.getWidth(),
            graph.getBottom() - depth * graph.getHeight()};
}

inline void pencilStroke(juce::AudioProcessorEditor& editor)
{
    click(editor, BackScratchEditor::Control::pencil);
    const auto down = graphPoint(2.2f, 0.25f);
    editor.mouseDown(event(editor, down, down, false));
    for (const auto point : {graphPoint(5.2f, 0.62f), graphPoint(9.2f, 0.84f), graphPoint(12.2f, 0.1f)})
        editor.mouseDrag(event(editor, point, down, true));
    editor.mouseUp(event(editor, graphPoint(12.2f, 0.1f), down, true, true));
}

inline void eraseAll(juce::AudioProcessorEditor& editor)
{
    click(editor, BackScratchEditor::Control::eraser);
    const auto down = graphPoint(0.1f, 0.5f), up = graphPoint(15.9f, 0.5f);
    editor.mouseDown(event(editor, down, down, false));
    editor.mouseDrag(event(editor, up, down, true));
    editor.mouseUp(event(editor, up, down, true, true));
}

inline void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

inline void loadDrawnState(juce::AudioPluginInstance& plugin, BackScratchProcessor& direct)
{
    juce::MemoryBlock component;
    direct.getStateInformation(component);
    // JUCE's local VST3PluginFormatImpl wraps an IComponent byte stream in this
    // XML/base64 container. The loaded VST3 then receives its actual component
    // setState call. Its native NSView editor cannot receive virtual mouse
    // events through the host's VST3PluginWindow, so UI gestures are verified
    // through the direct editor and the resulting geometry crosses this real
    // VST3 state boundary for packaged DSP/recall validation.
    juce::XmlElement wrapper("VST3PluginState");
    wrapper.createNewChildElement("IComponent")->addTextElement(component.toBase64Encoding());
    juce::MemoryBlock wrapped;
    juce::AudioProcessor::copyXmlToBinary(wrapper, wrapped);
    plugin.setStateInformation(wrapped.getData(), static_cast<int>(wrapped.getSize()));
}

} // namespace backScratch::vstEditedTests

inline void runVstEditedTests(juce::AudioPluginInstance& plugin, backScratch::BackScratchProcessor& direct)
{
    using namespace backScratch;
    using namespace backScratch::vstEditedTests;
    playbackAlignmentTests::Head head;
    head.looping = true; head.loopStart = 2.125; head.loopEnd = 2.875;
    struct RestorePlayHeads {
        juce::AudioPluginInstance& plugin;
        BackScratchProcessor& direct;
        juce::AudioPlayHead* oldPlugin;
        juce::AudioPlayHead* oldDirect;
        ~RestorePlayHeads() { plugin.setPlayHead(oldPlugin); direct.setPlayHead(oldDirect); }
    } restore {plugin, direct, plugin.getPlayHead(), direct.getPlayHead()};
    plugin.releaseResources(); direct.releaseResources();
    plugin.setPlayHead(&head); direct.setPlayHead(&head);
    auto directEditor = std::make_unique<BackScratchEditor>(direct);
    directEditor->setSize(700, 942);
    const auto beforeEdit = patternTests::fingerprint(direct.currentPattern());
    pencilStroke(*directEditor);
    loadDrawnState(plugin, direct);
    require(direct.hasCustomPattern()
            && vstTimingTests::parameterNamed(plugin, "Edited Pattern").getValue() > 0.5f
            && patternTests::fingerprint(direct.currentPattern()) != beforeEdit,
            "a real pencil gesture produces a custom contour accepted by the packaged VST3");
    const auto drawn = direct.currentPattern();
    plugin.prepareToPlay(48000.0, 512); direct.prepareToPlay(48000.0, 512);
    const auto beforeRecall = vstTimingTests::render(plugin, direct, head, false, 80);
    juce::MemoryBlock hostedState, directState;
    plugin.getStateInformation(hostedState); direct.getStateInformation(directState);
    require(hostedState.getSize() > 0 && hostedState.getSize() < 1024 * 1024,
            "the VST3 container saves the drawn phrase without embedding rolling audio history");

    const auto wrapper = juce::AudioProcessor::getXmlFromBinary(hostedState.getData(), static_cast<int>(hostedState.getSize()));
    require(wrapper != nullptr && wrapper->getChildByName("IComponent") != nullptr,
            "the actual packaged plugin saves its component state in the host's VST3 container");
    juce::MemoryBlock savedComponent;
    require(savedComponent.fromBase64Encoding(wrapper->getChildByName("IComponent")->getAllSubText()),
            "the packaged VST3 component state decodes");
    const auto packagedXml = juce::AudioProcessor::getXmlFromBinary(savedComponent.getData(), static_cast<int>(savedComponent.getSize()));
    require(packagedXml != nullptr && packagedXml->getChildByName("EDITED_PATTERN") != nullptr,
            "the actual VST3 state writer retains the full custom contour child");

    eraseAll(*directEditor);
    loadDrawnState(plugin, direct);
    for (int probe = 0; probe < 64; ++probe)
        require(evaluatePattern(direct.currentPattern(), direct.phraseBeats() * (probe + 0.25) / 64.0).wet == 0.0f,
                "dragging the actual eraser across the entire graph restores dry audio throughout the phrase");
    plugin.releaseResources(); direct.releaseResources();
    plugin.prepareToPlay(48000.0, 512); direct.prepareToPlay(48000.0, 512);
    const auto erased = vstTimingTests::render(plugin, direct, head, false, 80);
    double audibleChange = 0.0;
    for (size_t sample = 0; sample < erased.size(); ++sample)
        audibleChange = std::max(audibleChange, std::abs(static_cast<double>(erased[sample] - beforeRecall[sample])));
    require(audibleChange > 0.01, "pencil and eraser actions make an audible change in the packaged plugin");

    plugin.setStateInformation(hostedState.getData(), static_cast<int>(hostedState.getSize()));
    direct.setStateInformation(directState.getData(), static_cast<int>(directState.getSize()));
    require(direct.hasCustomPattern() && vstTimingTests::parameterNamed(plugin, "Edited Pattern").getValue() > 0.5f,
            "actual VST3 state restoration restores custom mode after the contour was erased");
    editedProcessorTests::compare(drawn, direct.currentPattern());
    const auto afterRecall = vstTimingTests::render(plugin, direct, head, false, 80);
    vstTimingTests::compare(beforeRecall, afterRecall,
        "packaged VST3 project recall reproduces the exact drawn audio, timing and shifted loop behavior");
    std::cout << "Packaged VST3 pencil/eraser contour audio and custom project-container recall passed\n";
}
