#include <juce_audio_processors/juce_audio_processors.h>
#include "Plugin/YoiBoiProcessor.h"
#include "PresetRender.h"
#include "CompatibilityFixtures.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace
{
bool smokeTestVst3 (const juce::String& path)
{
    juce::VST3PluginFormatHeadless format;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    format.findAllTypesForFile (descriptions, path);
    if (descriptions.isEmpty())
    {
        std::cerr << "VST3 scan found no plug-in at " << path << '\n';
        return false;
    }

    juce::String error;
    auto plugin = format.createInstanceFromDescription (*descriptions[0], 48000.0, 512, error);
    if (plugin == nullptr)
    {
        std::cerr << "VST3 host could not instantiate: " << error << '\n';
        return false;
    }
    plugin->setPlayConfigDetails (0, 2, 48000.0, 512);
    plugin->prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> audio (2, 512);
    audio.clear();
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 0);
    plugin->processBlock (audio, midi);
    float peak = 0.0f;
    for (int i = 0; i < audio.getNumSamples(); ++i)
    {
        const auto sample = audio.getSample (0, i);
        if (! std::isfinite (sample)) return false;
        peak = std::max (peak, std::abs (sample));
    }
    plugin->releaseResources();
    std::cout << "VST3 host scan: " << descriptions[0]->name << ", peak " << peak << '\n';
    return peak > 0.0001f;
}

bool renderEditor (const juce::String& path)
{
    juce::MessageManager::getInstance();
    yoiboi::YoiBoiProcessor synth;
    auto callbacks = synth.makeEditorCallbacks();
    for (const auto& item : callbacks.listPresets())
        if (! item.recent && ! item.user && item.name == "Tape Bounce")
            callbacks.loadPreset(item.id);
    synth.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> previewAudio (2, 512);
    juce::MidiBuffer previewMidi;
    for (int block = 0; block < 28; ++block)
    {
        previewAudio.clear();
        previewMidi.clear();
        if (block == 0 || block == 27)
            previewMidi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 0);
        synth.processBlock (previewAudio, previewMidi);
    }
    std::unique_ptr<juce::AudioProcessorEditor> editor (synth.createEditor());
    if (editor == nullptr) return false;
    const auto save = [&] (const juce::String& destination)
    {
        const auto width = editor->getWidth(), height = editor->getHeight();
        if (width <= 0 || height <= 0) return false;
        juce::Image image (juce::Image::ARGB, width, height, true);
        juce::Graphics graphics (image);
        editor->paintEntireComponent (graphics, true);
        auto stream = juce::File (destination).createOutputStream();
        if (stream == nullptr) return false;
        stream->setPosition (0);
        if (stream->truncate().failed()) return false;
        juce::PNGImageFormat png;
        return png.writeImageToStream (image, *stream);
    };
    const auto findTab = [&] (const auto& self, juce::Component* parent, const juce::String& name) -> juce::TextButton*
    {
        if (auto* button = dynamic_cast<juce::TextButton*> (parent))
            if (button->getButtonText() == name) return button;
        for (auto* child : parent->getChildren())
            if (auto* button = self (self, child, name)) return button;
        return nullptr;
    };
    if (! save (path)) return false;
    const auto file = juce::File (path);
    const auto findComponent = [&] (const auto& self, juce::Component* root,
                                   const juce::String& name) -> juce::Component*
    {
        if (root->getName() == name) return root;
        for (auto* child : root->getChildren())
            if (auto* match = self(self, child, name)) return match;
        return nullptr;
    };
    auto* info = dynamic_cast<juce::Button*>(findComponent(findComponent, editor.get(), "INFO"));
    auto* page = findComponent(findComponent, editor.get(), "VFD Info Page");
    if (!info || !info->onClick || !page || page->isVisible()) return false;
    info->onClick();
    if (!page->isVisible() || !save(file.getSiblingFile(file.getFileNameWithoutExtension()
                    + "-info.png").getFullPathName())) return false;
    if (!page->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)) || page->isVisible()) return false;
    for (const auto* name : { "SHAPE", "LIBRARY" })
    {
        auto* tab = findTab (findTab, editor.get(), name);
        if (tab == nullptr || ! tab->onClick) return false;
        tab->onClick();
        if (! save (file.getSiblingFile (file.getFileNameWithoutExtension()
                 + "-" + juce::String (name).toLowerCase() + ".png").getFullPathName())) return false;
    }
    if (auto* tab = findTab(findTab, editor.get(), "PLAY")) tab->onClick();
    for (const float scale : { 1.25f, 1.5f })
    {
        editor->setSize(juce::roundToInt(1055 * scale), juce::roundToInt(717 * scale));
        if (! save(file.getSiblingFile(file.getFileNameWithoutExtension() + "-"
                     + juce::String(juce::roundToInt(scale * 100)) + ".png").getFullPathName())) return false;
    }
    return true;
}
} // namespace

int main (int argc, char* argv[])
{
    if (argc >= 4 && juce::String::fromUTF8(argv[1]) == "--check-compat")
        return yoiboi::testPluginCompatibility (juce::String::fromUTF8(argv[2]),
                                               juce::String::fromUTF8(argv[3])) ? 0 : 1;
    if (argc >= 3 && juce::String::fromUTF8(argv[1]) == "--render-demos")
        return yoiboi::renderFactoryDemos (juce::File (juce::String::fromUTF8 (argv[2]))) ? 0 : 1;
    if (argc >= 3 && juce::String::fromUTF8(argv[1]) == "--render-ui")
        return renderEditor(juce::String::fromUTF8(argv[2])) ? 0 : 1;
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runTestsInCategory ("YOI-BOI", 12345);

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        if (const auto* result = runner.getResult (i))
        {
            failures += result->failures;
            std::cout << result->unitTestName << " / " << result->subcategoryName
                      << ": " << result->passes << " passed, " << result->failures << " failed\n";
            for (const auto& message : result->messages)
                std::cout << "  " << message << '\n';
        }
    }
    std::cout << "YOI-BOI tests: " << failures << " failures\n";
    const bool hostPassed = argc < 2 || smokeTestVst3 (juce::String::fromUTF8 (argv[1]));
    const bool editorPassed = argc < 3 || renderEditor (juce::String::fromUTF8 (argv[2]));
    if (argc >= 3) std::cout << "Editor offscreen render: " << (editorPassed ? "passed" : "failed") << '\n';
    return failures == 0 && runner.getNumResults() > 0 && hostPassed && editorPassed ? 0 : 1;
}
