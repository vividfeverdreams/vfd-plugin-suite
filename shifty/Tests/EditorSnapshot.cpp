#include "../Source/Plugin/ShiftyProcessor.h"
#include "../Source/UI/ShiftyEditor.h"
#include "FeatureTests.h"

#include <juce_graphics/juce_graphics.h>
#include <iostream>

namespace {
juce::Component* findComponent(juce::Component& root, const juce::String& name) {
    if (root.getName() == name) return &root;
    for (auto* child : root.getChildren())
        if (auto* found = findComponent(*child, name)) return found;
    return nullptr;
}

bool checkInfoPage(shifty::ShiftyProcessor& processor, shifty::ShiftyEditor& editor) {
    editor.setVisible(true);
    auto* info = dynamic_cast<juce::Button*>(findComponent(editor, "INFO"));
    auto* page = findComponent(editor, "VFD Info Page");
    auto* close = dynamic_cast<juce::Button*>(findComponent(editor, "Close info"));
    if (!info || !info->onClick || !page || !close || !close->onClick || page->isVisible()) return false;
    std::vector<float> before;
    for (auto* parameter : processor.getParameters()) before.push_back(parameter->getValue());
    info->onClick();
    if (!page->isVisible() || page->getBounds() != editor.getLocalBounds()) return false;
    std::set<juce::String> urls;
    const auto collectLinks = [&](const auto& self, juce::Component& root) -> void {
        if (auto* link = dynamic_cast<juce::HyperlinkButton*>(&root)) urls.insert(link->getURL().toString(true));
        for (auto* child : root.getChildren()) self(self, *child);
    };
    collectLinks(collectLinks, *page);
    if (urls != std::set<juce::String>{"https://x.com/cxy", "https://x.com/clt",
            "https://www.vividfeverdreams.xyz", "https://fundraising.fracturedatlas.org/dream-sequence"}) return false;
    auto* random = findComponent(editor, "Randomize");
    if (!random) return false;
    auto* hit = editor.getComponentAt(editor.getLocalPoint(random, random->getLocalBounds().getCentre()));
    if (hit != page && !page->isParentOf(hit)) return false;
    if (!page->keyPressed(juce::KeyPress(juce::KeyPress::downKey)) ||
        !page->keyPressed(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0))) return false;
    if (!page->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)) || page->isVisible()) return false;
    info->onClick();
    close->onClick();
    if (page->isVisible()) return false;
    for (int index = 0; index < processor.getParameters().size(); ++index)
        if (std::bit_cast<std::uint32_t>(before[static_cast<size_t>(index)]) !=
            std::bit_cast<std::uint32_t>(processor.getParameters()[index]->getValue())) return false;
    std::cout << "INFO page open, Escape, close, links, blocked underlying controls, bounds and parameter-preservation checks passed.\n";
    return true;
}

int checkVst3HostLatency(const juce::String& pluginPath) {
    if (!juce::File::isAbsolutePath(pluginPath) || !juce::File(pluginPath).exists()) {
        std::cerr << "VST3 path must be an existing absolute path.\n";
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI gui;
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    format.findAllTypesForFile(descriptions, pluginPath);
    if (descriptions.isEmpty()) {
        std::cerr << "Host could not find a VST3 plugin type in the bundle.\n";
        return 1;
    }

    juce::String error;
    auto hosted = format.createInstanceFromDescription(*descriptions[0], 48000.0, 512, error);
    if (!hosted) {
        std::cerr << "Host could not instantiate VST3: " << error << '\n';
        return 1;
    }

    const int beforePrepare = hosted->getLatencySamples();
    hosted->prepareToPlay(48000.0, 512);
    const int at48k = hosted->getLatencySamples();
    hosted->releaseResources();
    hosted->prepareToPlay(96000.0, 512);
    const int at96k = hosted->getLatencySamples();
    hosted->releaseResources();

    std::cout << "Hosted VST3 latency: before prepare " << beforePrepare
              << ", prepared 48 kHz " << at48k
              << ", prepared 96 kHz " << at96k << " samples.\n";

    // The exported parameter list must stay as in 0.4.2: SHIFTY's 16 controls
    // in order, then the VST3 wrapper's own host bypass.
    const auto& parameters = hosted->getParameters();
    juce::StringArray names;
    for (auto* parameter : parameters) names.add(parameter->getName(64));
    auto* hostBypass = hosted->getBypassParameter();
    std::cout << "Hosted VST3 parameters (" << parameters.size() << "): "
              << names.joinIntoString(", ") << "; host bypass: "
              << (hostBypass != nullptr ? hostBypass->getName(64) : juce::String("none")) << ".\n";
    const juce::StringArray expected { "Shift", "Focus", "Bite", "Drive", "Mix", "Sub Safe",
        "Sub Safe Frequency", "Output", "Motion Enabled", "Motion Shape", "Motion Rate", "Motion Depth",
        "Motion Sync", "Motion Free Rate", "Bypass", "Vowel Color", "Bypass" };
    const bool parameterListOk = names == expected && hostBypass != nullptr
                              && hostBypass == parameters.getLast();
    return beforePrepare == 0 && at48k == 2048 && at96k == 4096 && parameterListOk ? 0 : 1;
}
}

int main(int argc, char* argv[]) {
    if (argc == 2 && juce::String::fromUTF8(argv[1]) == "--feature-tests") {
        juce::ScopedJuceInitialiser_GUI gui;
        return shifty::tests::runFeatureTests();
    }

    if (argc == 3 && juce::String::fromUTF8(argv[1]) == "--host-latency")
        return checkVst3HostLatency(juce::String::fromUTF8(argv[2]));

    const bool motionOnSnapshot = argc == 3 && juce::String::fromUTF8(argv[1]) == "--motion-on";
    const bool vowelOnSnapshot = argc == 3 && juce::String::fromUTF8(argv[1]) == "--vowel-on";
    const bool infoSnapshot = argc == 3 && juce::String::fromUTF8(argv[1]) == "--info";
    if (argc != 2 && !motionOnSnapshot && !vowelOnSnapshot && !infoSnapshot) {
        std::cerr << "Usage: ShiftyEditorSnapshot /absolute/path/to/snapshot.png\n"
                  << "   or: ShiftyEditorSnapshot --feature-tests\n"
                  << "   or: ShiftyEditorSnapshot --motion-on /absolute/path/to/snapshot.png\n"
                  << "   or: ShiftyEditorSnapshot --vowel-on /absolute/path/to/snapshot.png\n"
                  << "   or: ShiftyEditorSnapshot --info /absolute/path/to/snapshot.png\n"
                  << "   or: ShiftyEditorSnapshot --host-latency /absolute/path/to/SHIFTY.vst3\n";
        return 2;
    }

    const juce::String outputPath = juce::String::fromUTF8(argv[(motionOnSnapshot || vowelOnSnapshot || infoSnapshot) ? 2 : 1]);
    if (!juce::File::isAbsolutePath(outputPath)) {
        std::cerr << "Output path must be absolute.\n";
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI gui;
    shifty::ShiftyProcessor processor;
    if (processor.getLatencySamples() != 2048) {
        std::cerr << "Unexpected latency before audio preparation.\n";
        return 1;
    }
    processor.prepareToPlay(96000.0, 512);
    if (processor.getLatencySamples() != 4096) {
        std::cerr << "Unexpected latency at 96 kHz.\n";
        return 1;
    }
    processor.prepareToPlay(48000.0, 512);
    if (processor.getLatencySamples() != 2048) {
        std::cerr << "Unexpected latency at 48 kHz.\n";
        return 1;
    }
    if (motionOnSnapshot) {
        processor.parameters().getParameter("motionEnable")->setValueNotifyingHost(1.0f);
        processor.setParameterLocked("shift", true);
        processor.setParameterLocked("mix", true);
    }
    if (vowelOnSnapshot)
        processor.parameters().getParameter("vowel")->setValueNotifyingHost(1.0f);
    shifty::ShiftyEditor editor(processor);
    if (!checkInfoPage(processor, editor)) {
        std::cerr << "INFO page interaction checks failed.\n";
        return 1;
    }
    if (infoSnapshot) dynamic_cast<juce::Button*>(findComponent(editor, "INFO"))->onClick();
    const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), true);
    if (!image.isValid() || !image.hasAlphaChannel()) {
        std::cerr << "Editor snapshot was not a valid ARGB image.\n";
        return 1;
    }

    const int lastX = image.getWidth() - 1;
    const int lastY = image.getHeight() - 1;
    const bool transparentCorners =
        image.getPixelAt(0, 0).getAlpha() == 0 &&
        image.getPixelAt(lastX, 0).getAlpha() == 0 &&
        image.getPixelAt(0, lastY).getAlpha() == 0 &&
        image.getPixelAt(lastX, lastY).getAlpha() == 0;
    const bool visibleDevice =
        image.getPixelAt(image.getWidth() / 2, image.getHeight() / 2).getAlpha() > 0;

    const juce::File output(outputPath);
    if (output.getParentDirectory().createDirectory().failed()) {
        std::cerr << "Could not create the output directory.\n";
        return 1;
    }
    juce::FileOutputStream stream(output);
    if (!stream.openedOk() || !stream.setPosition(0) || stream.truncate().failed() ||
        !juce::PNGImageFormat().writeImageToStream(image, stream)) {
        std::cerr << "Could not write the PNG snapshot.\n";
        return 1;
    }
    stream.flush();
    if (stream.getStatus().failed()) {
        std::cerr << "Could not flush the PNG snapshot.\n";
        return 1;
    }

    std::cout << output.getFullPathName().toStdString() << " ("
              << image.getWidth() << " x " << image.getHeight() << ")\n";
    if ((!infoSnapshot && !transparentCorners) || !visibleDevice) {
        std::cerr << "Transparency check failed: corners="
                  << (transparentCorners ? "clear" : "filled")
                  << ", center=" << (visibleDevice ? "visible" : "clear") << "\n";
        return 1;
    }
    std::cout << (infoSnapshot ? "Info page snapshot check passed.\n" : "Transparency check passed.\n");
    return 0;
}
