// Renders the real editor offscreen to PNGs: STUTTER-STEP Shots <output folder>
//   --host-keys                  the host-keys and editor regression checks (EditorTests.cpp)
//   --window-edges [crop folder] every size option: the window's corners and edge midpoints show the device, never
//                                background; writes corner crops to the folder when one is given
#include "StutterProcessor.h"
#include "StutterEditor.h"
#include <juce_events/juce_events.h>

int runStutterHostKeys();
int runStutterWindowEdges (const juce::File& cropsFolder);

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

void setParam (StutterProcessor& proc, const char* id, float value)
{
    auto* p = proc.apvts.getParameter (id);
    p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void setScale (juce::AudioProcessorEditor& editor, float scale)
{
    editor.setSize (juce::roundToInt (StutterEditor::baseWidth * scale),
                    juce::roundToInt (StutterEditor::baseWidth * scale * StutterEditor::cropH / StutterEditor::cropW));
}

// Hovers the step lane over a step, so the SLAP layer shows that step's send amount.
void hoverStep (juce::AudioProcessorEditor& editor, int step)
{
    for (auto* c : editor.getChildren())
        if (c->getTitle() == "Step lane") {
            const juce::Point<float> at ((float) c->getWidth() * ((float) step + 0.5f) / 16.0f, (float) c->getHeight() * 0.5f);
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            c->mouseMove (juce::MouseEvent (source, at, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, c, c, now, at, now, 0, false));
        }
}

// Both sizes the review asks for: 100% (rendered at 2x, as a Retina screen shows it) and 150%.
void pair (juce::AudioProcessorEditor& editor, const juce::File& folder, const juce::String& name)
{
    setScale (editor, 1.0f);
    shot (editor, folder.getChildFile (name + "-100.png"));
    setScale (editor, 1.5f);
    shot (editor, folder.getChildFile (name + "-150.png"), 1.0f);
    setScale (editor, 1.0f);
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc > 1 && juce::String (argv[1]) == "--host-keys") return runStutterHostKeys();
    if (argc > 1 && juce::String (argv[1]) == "--window-edges")
        return runStutterWindowEdges (argc > 2 ? juce::File (juce::String (argv[2])) : juce::File());
    const juce::File folder (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    folder.createDirectory();
    StutterProcessor proc;
    Head head;
    proc.setPlayHead (&head);
    proc.prepareToPlay (48000, 512);
    run (proc, head, 48000 * 3 + 7000);
    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());

    // GATE (the default layer) and SLAP on the same groove.
    pair (*editor, folder, "01-gate-layer");
    proc.editLayer = 1;
    hoverStep (*editor, 14);
    pair (*editor, folder, "02-slap-layer");
    // SLAP with send amounts set per step.
    auto pattern = proc.getPattern();
    pattern[6].send = 9; pattern[10].slap = true; pattern[10].send = 14; pattern[0].slap = true; pattern[0].send = 5;
    proc.setPattern (pattern);
    run (proc, head, 9000);
    hoverStep (*editor, 10);
    pair (*editor, folder, "03-slap-send-amounts");
    proc.editLayer = 0;
    hoverStep (*editor, 0);

    proc.loadFactory (stutter::factoryIndexOf ("Rubber Shuffle"));
    setParam (proc, "timing", 1.0f);
    run (proc, head, 9000);
    pair (*editor, folder, "04-dotted");
    setParam (proc, "timing", 2.0f);
    run (proc, head, 9000);
    pair (*editor, folder, "05-triplet");

    proc.loadFactory (stutter::factoryIndexOf ("Trap Rolls"));
    run (proc, head, 9000);
    pair (*editor, folder, "06-hats-trap-rolls");
    proc.loadFactory (stutter::factoryIndexOf ("Triplet Rolls"));
    run (proc, head, 9000);
    pair (*editor, folder, "07-hats-triplet-rolls");
    proc.loadFactory (stutter::factoryIndexOf ("Three Three Two"));
    run (proc, head, 9000);
    pair (*editor, folder, "08-simple-three-three-two");
    proc.loadFactory (stutter::factoryIndexOf ("Dotted Eighth Pulse"));
    run (proc, head, 9000);
    pair (*editor, folder, "09-simple-dotted-eighth-pulse");

    // Nudged one step earlier: Lean Back's opening tie now wraps the loop and holds across it.
    proc.loadFactory (stutter::factoryIndexOf ("Lean Back"));
    proc.nudge (-1);
    run (proc, head, 9000);
    pair (*editor, folder, "10-nudged-earlier");

    // A rolled HATS groove, then the smallest size in both layers.
    proc.rollFrom = (int) stutter::Family::hats;
    proc.rollNewGroove();
    run (proc, head, 9000);
    pair (*editor, folder, "11-new-groove-hats");
    proc.editLayer = 1;
    setScale (*editor, 0.8f);
    shot (*editor, folder.getChildFile ("12-slap-layer-80.png"), 1.0f);
    proc.editLayer = 0;
    shot (*editor, folder.getChildFile ("13-gate-layer-80.png"), 1.0f);
    setScale (*editor, 1.0f);
    return 0;
}
