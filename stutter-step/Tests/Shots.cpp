// Renders the real editor offscreen to PNGs: STUTTER-STEP Shots <output folder>
#include "StutterProcessor.h"
#include "StutterEditor.h"
#include <juce_events/juce_events.h>

int runStutterHostKeys();

namespace {
struct Head final : juce::AudioPlayHead
{
    juce::int64 sample = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (140.0);
        info.setIsPlaying (true);
        info.setPpqPosition ((double) sample / 48000.0 * 140.0 / 60.0);
        return info;
    }
};

void run (StutterProcessor& proc, Head& head, int samples)
{
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    juce::Random rnd (3);
    for (int done = 0; done < samples; done += 512) {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                buffer.setSample (ch, i, 0.5f * std::sin ((float) (done + i) * 0.0072f) + 0.05f * (rnd.nextFloat() - 0.5f));
        proc.processBlock (buffer, midi);
        head.sample += 512;
    }
}

void shot (juce::AudioProcessorEditor& editor, const juce::File& file, float scale = 2.0f)
{
    const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, scale);
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat().writeImageToStream (image, out);
    std::printf ("wrote %s (%dx%d)\n", file.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc > 1 && juce::String (argv[1]) == "--host-keys") return runStutterHostKeys();
    const juce::File folder (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    folder.createDirectory();
    StutterProcessor proc;
    Head head;
    proc.setPlayHead (&head);
    proc.prepareToPlay (48000, 512);
    run (proc, head, 48000 * 3 + 7000);
    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    shot (*editor, folder.getChildFile ("01-factory.png"));

    proc.rollNewGroove();
    run (proc, head, 9000);
    shot (*editor, folder.getChildFile ("02-new-groove.png"));

    proc.loadFactory (stutter::factoryIndexOf ("Glass Stairs"));
    run (proc, head, 9000);
    shot (*editor, folder.getChildFile ("03-glass-stairs.png"));

    auto* rate = proc.apvts.getParameter ("rate");
    rate->setValueNotifyingHost (rate->convertTo0to1 (0.0f)); // 1/4: the 16 steps span four bars
    run (proc, head, 9000);
    shot (*editor, folder.getChildFile ("05-quarter-rate.png"));
    rate->setValueNotifyingHost (rate->convertTo0to1 (2.0f));

    editor->setSize (StutterEditor::baseWidth * 3 / 2, StutterEditor::baseWidth * 3 / 2 * StutterEditor::cropH / StutterEditor::cropW);
    shot (*editor, folder.getChildFile ("04-150-percent.png"), 1.0f);

    // The random key beside the arrows: a stored groove at random, at 100% and at the smallest size (80%, drag only).
    proc.loadRandomGroove();
    run (proc, head, 9000);
    editor->setSize (StutterEditor::baseWidth, StutterEditor::baseWidth * StutterEditor::cropH / StutterEditor::cropW);
    shot (*editor, folder.getChildFile ("06-random-groove.png"));
    editor->setSize (StutterEditor::baseWidth * 4 / 5, StutterEditor::baseWidth * 4 / 5 * StutterEditor::cropH / StutterEditor::cropW);
    shot (*editor, folder.getChildFile ("07-80-percent.png"), 1.0f);
    editor->setSize (StutterEditor::baseWidth, StutterEditor::baseWidth * StutterEditor::cropH / StutterEditor::cropW);
    proc.uiBpm = 120.0;
    rate->setValueNotifyingHost (rate->convertTo0to1 (1.0f));
    auto* swing = proc.apvts.getParameter ("swing");
    swing->setValueNotifyingHost (swing->convertTo0to1 (60.0f));
    auto* lock = proc.apvts.getParameter ("slapLock");
    lock->setValueNotifyingHost (1.0f);
    shot (*editor, folder.getChildFile ("08-lock-300-ms.png"));
    proc.uiBpm = 20.0;
    rate->setValueNotifyingHost (rate->convertTo0to1 (0.0f));
    shot (*editor, folder.getChildFile ("09-lock-950-ms.png"));
    return 0;
}
