#include "BackScratchProcessor.h"
#include "BackScratchEditor.h"
#include "BinaryData.h"
#include "ProcessorTests.h"
#include "GeneratedProcessorTests.h"
#include "PlaybackAlignmentTests.h"
#include "TimingStateTests.h"
#include "VstTimingTests.h"
#include "VstEditedTests.h"
#include "InteractionTests.h"
#include "GridEditorInteractionTests.h"
#include "EditedProcessorTests.h"
#include "EditedSerializationTests.h"
#include "MotionVersionTests.h"
#include "PerformanceRender.h"
#include "TransportContinuityTests.h"
#include "FeatureUiTests.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <iostream>

namespace {
void checkHardwareLight(const backScratch::BackScratchEditor& editor, const juce::Image& snapshot)
{
    const auto original = juce::ImageFileFormat::loadFrom(BinaryData::Shell_png, BinaryData::Shell_pngSize);
    backScratch::processorTests::check(original.isValid(), "original enclosure reference loads");
    juce::Image reference(juce::Image::ARGB, snapshot.getWidth(), snapshot.getHeight(), true);
    {
        juce::Graphics graphics(reference);
        graphics.fillAll(juce::Colour::fromRGB(28, 4, 7));
        // The editor composites its enclosure with high-quality resampling; match it.
        graphics.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        graphics.addTransform(editor.designToEditor());
        graphics.drawImage(original, juce::Rectangle<float>(0, 0, 1122, 1402));
    }

    // Compare a disc safely inside the physical LED. Its antialiased boundary and
    // surrounding glow are intentionally excluded; overlays may not cover the light.
    const juce::Point<float> centre {929.0f, 119.0f};
    const auto sampleBounds = juce::Rectangle<float>(centre.x - 8, centre.y - 8, 16, 16)
        .transformedBy(editor.designToEditor()).getSmallestIntegerContainer();
    const auto toDesign = editor.designToEditor().inverted();
    int samples = 0, largestDifference = 0;
    for (int y = sampleBounds.getY(); y < sampleBounds.getBottom(); ++y) {
        for (int x = sampleBounds.getX(); x < sampleBounds.getRight(); ++x) {
            const auto design = juce::Point<float>(x + 0.5f, y + 0.5f).transformedBy(toDesign);
            if (design.getDistanceSquaredFrom(centre) > 64.0f) continue;
            const auto expected = reference.getPixelAt(x, y), actual = snapshot.getPixelAt(x, y);
            largestDifference = std::max({largestDifference,
                std::abs(static_cast<int>(expected.getRed()) - actual.getRed()),
                std::abs(static_cast<int>(expected.getGreen()) - actual.getGreen()),
                std::abs(static_cast<int>(expected.getBlue()) - actual.getBlue())});
            ++samples;
        }
    }
    backScratch::processorTests::check(samples > 32, "the enclosure light comparison covers visible interior pixels");
    // The editor composites its enclosure into a cached image before blitting it, which can move a pixel
    // by a couple of levels. Any overlay on the light differs by far more than this.
    constexpr int tolerance = 3;
    if (largestDifference > tolerance)
        std::cerr << "Original enclosure light pixel difference: " << largestDifference << '\n';
    backScratch::processorTests::check(largestDifference <= tolerance, "the UI leaves the original hardware light unobscured");
}
}

int main(int argc, char* argv[])
{
    try {
        juce::ScopedJuceInitialiser_GUI gui;
        if (argc == 3 && juce::String(argv[1]) == "--export-presets") {
            backScratch::motionVersionTests::exportFactoryBank(juce::File(juce::String::fromUTF8(argv[2])));
            return 0;
        }
        if (argc == 2 && juce::String(argv[1]) == "--timing-tests") {
            runTimingStateTests();
            return 0;
        }
        runProcessorTests();
        runGeneratedProcessorTests();
        runPlaybackAlignmentTests();
        runTimingStateTests();
        runEditedProcessorTests();
        runEditedSerializationTests();
        runMotionVersionTests();
        runGridEditorInteractionTests();
        runTransportContinuityTests();
        runFeatureUiTests();
        backScratch::BackScratchProcessor processor;
        processor.prepareToPlay(48000, 512);
        runInteractionTests(processor);
        processor.selectFactoryPreset(0); processor.setKeepSub(true); processor.setPhraseBars(1); processor.setPhraseLocked(true);
        if (argc >= 2) {
            const juce::File file(juce::String::fromUTF8(argv[1]));
            const auto renderEditor = [](const juce::File& destination, const auto& configure) {
                backScratch::BackScratchProcessor preview;
                preview.prepareToPlay(48000, 512);
                preview.setHostParameter("generated", 1);
                preview.setHostParameter("generationSeed", 83177);
                preview.setPhraseBars(4);
                preview.setPhraseLocked(true);
                preview.setKeepSub(true);
                configure(preview);
                backScratch::BackScratchEditor editor(preview);
                editor.setVisible(true);
                backScratch::interactionTests::checkControlLayout(editor);
                if (destination.getFileNameWithoutExtension().endsWith("-pencil")
                    || destination.getFileNameWithoutExtension().endsWith("-eraser")) {
                    using namespace backScratch::interactionTests;
                    using namespace backScratch::gridEditorInteractionTests;
                    using Control = backScratch::BackScratchEditor::Control;
                    click(editor, Control::pencil);
                    const auto start = gridPoint(4.0f, 0.0f);
                    editor.mouseDown(event(editor, start, start));
                    for (auto point : {gridPoint(7.0f, 0.72f), gridPoint(10.0f, 0.10f), gridPoint(13.0f, 0.70f)})
                        editor.mouseDrag(event(editor, point, start, true));
                    editor.mouseUp(event(editor, gridPoint(13.0f, 0.70f), start, true));
                    check(preview.hasCustomPattern() && preview.canUndoPatternEdit(),
                          "release pencil preview contains a committed editable drawing");
                    if (destination.getFileNameWithoutExtension().endsWith("-eraser")) {
                        click(editor, Control::eraser);
                        stroke(editor, gridPoint(7.2f, 0.45f), gridPoint(9.8f, 0.45f));
                        editor.mouseMove(event(editor, gridPoint(9.8f, 0.45f), gridPoint(9.8f, 0.45f)));
                    } else {
                        editor.mouseMove(event(editor, gridPoint(10.0f, 0.30f), gridPoint(10.0f, 0.30f)));
                    }
                }
                using Control = backScratch::BackScratchEditor::Control;
                if (destination.getFileNameWithoutExtension().endsWith("-shape")) {
                    backScratch::gridEditorInteractionTests::chooseMenuItem(editor, Control::shapes, "Baby Scratch");
                    backScratch::processorTests::check(preview.hasCustomPattern() && preview.canUndoPatternEdit(), "shape preview is real editable geometry");
                }
                if (destination.getFileNameWithoutExtension().endsWith("-tools-menu")
                    || destination.getFileNameWithoutExtension().endsWith("-shapes-menu")) {
                    backScratch::interactionTests::click(editor, destination.getFileNameWithoutExtension().endsWith("-tools-menu")
                        ? Control::drawOptions : Control::shapes);
                    backScratch::gridEditorInteractionTests::pumpMenuCallbacks();
                    backScratch::processorTests::check(juce::Component::getCurrentlyModalComponent() != nullptr,
                        "release dropdown screenshot contains the actual open menu");
                }
                const auto snapshot = editor.createComponentSnapshot(editor.getLocalBounds());
                backScratch::processorTests::check(snapshot.isValid(), "editor renders");
                checkHardwareLight(editor, snapshot);
                destination.getParentDirectory().createDirectory();
                juce::FileOutputStream output(destination);
                backScratch::processorTests::check(output.openedOk(), "editor snapshot opens");
                output.setPosition(0); output.truncate();
                backScratch::processorTests::check(juce::PNGImageFormat().writeImageToStream(snapshot, output), "editor PNG saved");
                if (destination.getFileName() == "BACK-SCRATCH-editor.png") {
                    auto* info = dynamic_cast<juce::Button*>(backScratch::interactionTests::findNamed(editor, "INFO"));
                    backScratch::processorTests::check(info != nullptr, "INFO key available for release screenshot");
                    info->onClick();
                    const auto credits = editor.createComponentSnapshot(editor.getLocalBounds());
                    juce::FileOutputStream creditsOutput(destination.getSiblingFile("BACK-SCRATCH-info.png"));
                    backScratch::processorTests::check(creditsOutput.openedOk(), "credits snapshot opens");
                    creditsOutput.setPosition(0); creditsOutput.truncate();
                    backScratch::processorTests::check(juce::PNGImageFormat().writeImageToStream(credits, creditsOutput), "credits PNG saved");
                }
            };
            backScratch::featureUiTests::renderFeatureScreens(file.getParentDirectory());
            renderEditor(file, [](auto&) {});
            renderEditor(file.getSiblingFile(file.getFileNameWithoutExtension() + "-pencil.png"), [](auto&) {});
            renderEditor(file.getSiblingFile(file.getFileNameWithoutExtension() + "-eraser.png"), [](auto&) {});
            renderEditor(file.getSiblingFile(file.getFileNameWithoutExtension() + "-tools-menu.png"), [](auto&) {});
            renderEditor(file.getSiblingFile(file.getFileNameWithoutExtension() + "-shapes-menu.png"), [](auto&) {});
            renderEditor(file.getSiblingFile(file.getFileNameWithoutExtension() + "-shape.png"), [](auto&) {});
            renderEditor(file.getSiblingFile(file.getFileNameWithoutExtension() + "-short.png"), [](auto& preview) {
                preview.setPhraseDurationChoice(0);
                preview.setKeepSub(false);
                preview.setPhraseLocked(false);
            });
            renderEditor(file.getSiblingFile(file.getFileNameWithoutExtension() + "-factory.png"), [](auto& preview) {
                preview.selectFactoryPreset(54); // A long display name: Breakbeat Telegraph.
                preview.setPhraseBars(16);
            });
            renderEditor(file.getSiblingFile(file.getFileNameWithoutExtension() + "-dotted.png"), [](auto& preview) {
                preview.setPhraseTimingChoice(1);
                preview.shiftPattern(3);
            });
            renderEditor(file.getSiblingFile(file.getFileNameWithoutExtension() + "-triplet.png"), [](auto& preview) {
                preview.setPhraseDurationChoice(0);
                preview.setPhraseTimingChoice(2);
                preview.shiftPattern(-1);
            });
        }
        if (argc >= 3) {
            juce::VST3PluginFormat format;
            juce::OwnedArray<juce::PluginDescription> descriptions;
            format.findAllTypesForFile(descriptions, juce::String::fromUTF8(argv[2]));
            backScratch::processorTests::check(!descriptions.isEmpty(), "VST3 scans");
            juce::String error;
            auto plugin = format.createInstanceFromDescription(*descriptions[0], 48000, 512, error);
            backScratch::processorTests::check(plugin != nullptr, "VST3 instantiates");
            backScratch::processorTests::Head head;
            plugin->setPlayHead(&head); plugin->prepareToPlay(48000, 512);
            processor.setPlayHead(&head); processor.prepareToPlay(48000, 512);
            backScratch::processorTests::check(plugin->getLatencySamples() == processor.getLatencySamples(), "shared-path latency correctly reported by VST3");
            double worst = 0;
            juce::MidiBuffer midi;
            for (int block = 0; block < 200; ++block) {
                juce::AudioBuffer<float> a(2, 512), b(2, 512);
                for (int i = 0; i < 512; ++i) for (int ch = 0; ch < 2; ++ch)
                    a.setSample(ch, i, backScratch::processorTests::testSound(block * 512 + i, ch));
                b.makeCopyOf(a);
                head.beat = block * 512.0 * head.bpm / (48000.0 * 60.0);
                plugin->processBlock(a, midi); processor.processBlock(b, midi);
                for (int i = 0; i < 512; ++i) for (int ch = 0; ch < 2; ++ch)
                    worst = std::max(worst, static_cast<double>(std::abs(a.getSample(ch, i) - b.getSample(ch, i))));
            }
            backScratch::processorTests::check(worst < 0.00003, "built VST3 audio matches tested processor");
            std::cout << "Packaged VST3 real processing matches processor; worst difference " << worst << '\n';
            runVstTimingTests(*plugin, processor);
            runVstEditedTests(*plugin, processor);
            backScratch::transportContinuityTests::checkPackagedHostBypass(*plugin);
            plugin->releaseResources();
        }
        if (argc >= 4) renderPerformanceDemos(juce::File(juce::String::fromUTF8(argv[3])),
            argc >= 5 ? juce::File(juce::String::fromUTF8(argv[4])) : juce::File{});
        std::cout << "BACK-SCRATCH live pattern host smoke passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n'; return 1;
    }
}
