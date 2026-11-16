#include "LoudnessVoicing.h"
#include "Dsp/CompatibleVoice.h"
#include "Dsp/IntegratedLoudness.h"
#include <chrono>
#include <memory>
namespace yoiboi
{
PhraseRender renderVoicingPhrase (const Patch& p)
{
    constexpr int rate = 48000, frames = rate * 3, block = 256;
    auto voice = std::make_unique<dsp::CompatibleVoice>();
    const auto values = parameterValuesFor (p);
    HostTiming timing; timing.bpm = 140; timing.hasBpm = timing.hasPpq = timing.playing = true;
    voice->setControls (controlsFor (values, timing, p.soundEngine, rate, p.fmRatio, p.fmIndex, p.resonance), p.soundEngine);
    voice->prepare (rate);
    PhraseRender result; result.left.resize (frames); result.right.resize (frames);
    struct Note { int on, off, pitch; };
    constexpr Note notes[] { {0,18240,43}, {27360,36960,43}, {41280,51360,46}, {61440,89280,43} };
    dsp::IntegratedLoudness meter;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; ++i)
    {
        if (i % block == 0)
        {
            timing.ppq = static_cast<double> (i) / rate * 140 / 60;
            const auto c = controlsFor (values, timing, p.soundEngine, rate, p.fmRatio, p.fmIndex, p.resonance);
            voice->setControls (c, p.soundEngine);
            const auto sweep = static_cast<int> (64 + 63 * std::sin (i / static_cast<double> (rate) * 6));
            if (c.talkMode >= 2) voice->setTalkScrub ((c.talkMode == 2 ? sweep : 127 - sweep) / 127.f);
        }
        for (const auto& n : notes)
        {
            if (i == n.on) voice->noteOn (n.pitch, 112 / 127.f, p.glideMilliseconds);
            if (i == n.off) voice->noteOff();
        }
        const auto f = voice->renderFrame();
        result.left[static_cast<std::size_t> (i)] = f.left;
        result.right[static_cast<std::size_t> (i)] = f.right;
        result.peak = std::max ({result.peak, std::abs (f.left), std::abs (f.right)});
    }
    result.seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    for (int i = 0; i < frames; ++i) meter.push (result.left[static_cast<std::size_t> (i)], result.right[static_cast<std::size_t> (i)]);
    result.lufs = meter.integrated();
    return result;
}
void voicePatch (Patch& p, bool precise)
{
    auto probe = p; probe.outputDecibels = 0; probe.loudnessTrim = 0;
    const auto audio = renderVoicingPhrase (probe);
    p.loudnessTrim = static_cast<float> (std::clamp (-10.0 - audio.lufs + .15, -24.0, 36.0));
    if (precise)
        for (int iteration = 0; iteration < 4; ++iteration)
        {
            probe = p; probe.outputDecibels = 0;
            const auto measured = renderVoicingPhrase (probe);
            if (std::abs (measured.lufs + 10) < .02) break;
            p.loudnessTrim += static_cast<float> (std::clamp (-10.0 - measured.lufs, -2.0, 2.0));
        }
}
void voiceGeneratedPatch (Patch& p)
{
    p.finish = .25f; p.subBalance = .75f; p.subOnNote = p.lowCutTrack = true;
    voicePatch (p, false);
}
}
