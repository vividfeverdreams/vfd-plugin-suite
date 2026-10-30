// Renders every factory groove on a held reese bass with dry halftime drums, at 140 BPM.
// STUTTER-STEP Demos <output folder>: one WAV per preset plus 00 TOUR.wav (two bars each) and TOUR.txt.
#include "Groove.h"
#include "StutterProcessor.h"
#include <juce_events/juce_events.h>
#include <cmath>
#include <cstdio>

using namespace stutter;

namespace {
constexpr double sr = 48000.0, bpm = 140.0;
constexpr double twoPi = 6.283185307179586;
const int samplesPerBar = (int) std::round (sr * 60.0 / bpm * 4.0);

struct Head final : juce::AudioPlayHead
{
    juce::int64 sample = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setIsPlaying (true);
        info.setPpqPosition ((double) sample / sr * bpm / 60.0);
        return info;
    }
};

// Held reese: detuned saws through a moving resonant low-pass, light drive, sine sub. One note per bar.
struct Reese
{
    double ph[3] {}, sub = 0, ic1 = 0, ic2 = 0, t = 0;
    float next (int bar)
    {
        static const double notes[] { 43.65, 43.65, 51.91, 38.89 }; // F1 F1 Ab1 Eb1
        const double f = notes[bar % 4];
        const double det[] { 1.0085, 0.9915, 2.003 };
        double saw = 0;
        for (int k = 0; k < 3; ++k) { ph[k] = std::fmod (ph[k] + f * det[k] / sr, 1.0); saw += (ph[k] * 2.0 - 1.0) * (k == 2 ? 0.5 : 1.0); }
        t += 1.0 / sr;
        const double cutoff = 380.0 + 300.0 * (0.5 + 0.5 * std::sin (twoPi * t * bpm / 60.0 / 8.0)); // slow movement over two bars
        const double g = std::tan (3.14159265 * cutoff / sr), k = 0.55;  // TPT state-variable low-pass
        const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const double v3 = saw - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        sub = std::fmod (sub + f / sr, 1.0);
        return (float) (0.24 * std::tanh (1.8 * v2) + 0.30 * std::sin (twoPi * sub));
    }
};

// Kick on 1, snare on 3 (halftime), quiet eighth hats.
float drums (juce::int64 n, juce::Random& rnd)
{
    const double beat = (double) n / sr * bpm / 60.0;
    const double inBar = std::fmod (beat, 4.0), inEighth = std::fmod (beat, 0.5);
    const double tk = inBar * 60.0 / bpm, ts = (inBar - 2.0) * 60.0 / bpm, th = inEighth * 60.0 / bpm;
    float out = 0;
    if (tk < 0.4) out += (float) (0.9 * std::sin (twoPi * (45.0 * tk + 9.0 * (1.0 - std::exp (-tk * 30.0)))) * std::exp (-tk * 7.0));
    if (ts >= 0 && ts < 0.3) out += (float) (std::exp (-ts * 18.0) * (0.45 * (rnd.nextFloat() * 2 - 1) + 0.3 * std::sin (twoPi * 185.0 * ts)));
    if (th < 0.05) out += (float) (0.06 * (rnd.nextFloat() * 2 - 1) * std::exp (-th * 90.0));
    return out;
}

void writeWav (const juce::File& file, const std::vector<float>& left, const std::vector<float>& right)
{
    juce::MemoryOutputStream data;
    for (size_t i = 0; i < left.size(); ++i)
        for (const float v : { left[i], right[i] }) {
            const int s = juce::jlimit (-8388608, 8388607, (int) std::lround (v * 8388607.0));
            data.writeByte ((char) (s & 0xff)); data.writeByte ((char) ((s >> 8) & 0xff)); data.writeByte ((char) ((s >> 16) & 0xff));
        }
    juce::MemoryOutputStream wav;
    const auto bytes = (int) data.getDataSize();
    wav.write ("RIFF", 4); wav.writeInt (36 + bytes); wav.write ("WAVE", 4);
    wav.write ("fmt ", 4); wav.writeInt (16); wav.writeShort (1); wav.writeShort (2); wav.writeInt ((int) sr);
    wav.writeInt ((int) sr * 6); wav.writeShort (6); wav.writeShort (24);
    wav.write ("data", 4); wav.writeInt (bytes); wav.write (data.getData(), data.getDataSize());
    file.replaceWithData (wav.getData(), wav.getDataSize());
}

// Renders one groove for a number of bars; returns peak.
float render (int preset, int bars, std::vector<float>& left, std::vector<float>& right, juce::int64 startSample)
{
    StutterProcessor proc;
    Head head;
    head.sample = startSample;
    proc.setPlayHead (&head);
    proc.loadFactory (preset);
    proc.prepareToPlay (sr, 512);
    Reese bass;
    juce::Random rnd (11);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    const int total = bars * samplesPerBar;
    float peak = 0;
    for (int done = 0; done < total; done += 512) {
        const int n = std::min (512, total - done);
        buffer.setSize (2, n, false, false, true);
        for (int i = 0; i < n; ++i) {
            const auto absolute = startSample + done + i;
            const float b = bass.next ((int) (absolute / samplesPerBar));
            buffer.setSample (0, i, b); buffer.setSample (1, i, b);
        }
        proc.processBlock (buffer, midi);
        for (int i = 0; i < n; ++i) {
            const float d = 0.5f * drums (startSample + done + i, rnd);
            left.push_back (0.78f * (buffer.getSample (0, i) + d)); // demo-mix headroom, about -1 dBFS peak
            right.push_back (0.78f * (buffer.getSample (1, i) + d));
            peak = std::max ({ peak, std::abs (left.back()), std::abs (right.back()) });
        }
        head.sample += n;
    }
    return peak;
}
}

int main (int argc, char** argv)
{
    // Pure offline DSP/WAV rendering: do not initialise JUCE GUI or open an audio device.
    const juce::File folder (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    folder.createDirectory();
    std::vector<float> tourL, tourR;
    juce::String tourText ("STUTTER-STEP factory tour: 140 BPM, two bars per groove, held reese bass + dry halftime drums\n\n");
    for (int i = 0; i < numFactoryGrooves; ++i) {
        const auto& f = factoryGroove (i);
        std::vector<float> l, r;
        const float peak = render (i, 4, l, r, 0);
        const auto name = juce::String (i + 1).paddedLeft ('0', 2) + " " + f.folder + " - " + f.name + ".wav";
        writeWav (folder.getChildFile (name), l, r);
        const double seconds = (double) tourL.size() / sr;
        tourText << juce::String ((int) seconds / 60) << ":" << juce::String ((int) seconds % 60).paddedLeft ('0', 2)
                 << "  " << f.folder << " / " << f.name << "\n";
        std::vector<float> tl, tr;
        render (i, 2, tl, tr, 0);
        tourL.insert (tourL.end(), tl.begin(), tl.end());
        tourR.insert (tourR.end(), tr.begin(), tr.end());
        std::printf ("%-12s %-18s peak %.2f\n", f.folder, f.name, peak);
    }
    writeWav (folder.getChildFile ("00 TOUR.wav"), tourL, tourR);
    folder.getChildFile ("TOUR.txt").replaceWithText (tourText);
    std::printf ("tour %.0f s\n", tourL.size() / sr);
    return 0;
}
