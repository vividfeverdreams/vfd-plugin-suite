// Renders the real editor offscreen to PNGs: STUTTER-STEP Shots <output folder>
#include "StutterProcessor.h"
#include "StutterEditor.h"
#include <juce_events/juce_events.h>

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

    editor->setSize (StutterEditor::baseWidth * 3 / 2, StutterEditor::baseWidth * 3 / 2 * StutterEditor::cropH / StutterEditor::cropW);
    shot (*editor, folder.getChildFile ("04-150-percent.png"), 1.0f);
    return 0;
}
