// STUTTER-STEP console tests: groove math, generator, processor renders, slap, state, MIDI clips.
#include "Groove.h"
#include "MidiClip.h"
#include "SlapDelay.h"
#include "StutterProcessor.h"
#include <juce_events/juce_events.h>
#include <cmath>
#include <cstdio>
#include <set>
#include <thread>
#include <juce_cryptography/juce_cryptography.h>
#include "SubSafeReference.h"

using namespace stutter;

namespace {
int failures = 0, checks = 0;
void check (bool ok, const char* what, double got = 0, double want = 0)
{
    ++checks;
    if (! ok) { ++failures; std::printf ("  FAIL %s (got %.6f, want %.6f)\n", what, got, want); }
}
void near (double got, double want, double tol, const char* what) { check (std::abs (got - want) <= tol, what, got, want); }

struct FakePlayHead final : juce::AudioPlayHead
{
    double bpm = 120.0, sr = 48000.0;
    juce::int64 sample = 0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setIsPlaying (playing);
        info.setTimeInSamples (sample);
        info.setPpqPosition ((double) sample / sr * bpm / 60.0);
        return info;
    }
};

void setParam (StutterProcessor& proc, const char* id, float value)
{
    auto* parameter = proc.apvts.getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

// Renders a constant input through the processor and returns the left channel.
std::vector<float> render (StutterProcessor& proc, FakePlayHead& head, int numSamples, float input = 0.5f, int block = 512)
{
    std::vector<float> out;
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    for (int done = 0; done < numSamples; done += block) {
        const int n = std::min (block, numSamples - done);
        buffer.setSize (2, n, false, false, true);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < n; ++i) buffer.setSample (ch, i, input);
        proc.processBlock (buffer, midi);
        for (int i = 0; i < n; ++i) out.push_back (buffer.getSample (0, i));
        head.sample += n;
    }
    return out;
}

void testTiming()
{
    std::puts ("timing");
    near (stepStart (1, 0.5), 1.0, 1e-12, "straight off-step");
    near (stepStart (1, 0.62), 1.24, 1e-12, "swung off-step");
    near (stepStart (3, 2.0 / 3.0), 2.0 + 4.0 / 3.0, 1e-12, "triplet swing");
    double total = 0;
    for (int i = 0; i < numSteps; ++i) total += slotLength (i, 0.71);
    near (total, 16.0, 1e-12, "slots fill the cycle");
    check (stepAt (1.1, 0.62) == 0 && stepAt (1.3, 0.62) == 1, "stepAt follows swing");
}

void testEnvelope()
{
    std::puts ("envelope");
    Envelope e;
    e.build (parsePattern ("X...............", ""), 0.5, 0.5, 0.05, 0.0f, false);
    near (e.eval (0.0), 0.5, 1e-3, "attack centred on the grid line");
    near (e.eval (0.25), 1.0, 1e-6, "open after attack");
    near (e.eval (1.0), 0.0, 1e-6, "closed after length");
    near (e.eval (16.25), 1.0, 1e-6, "wraps every 16 steps");
    near (e.eval (-15.75), 1.0, 1e-6, "wraps backwards");

    e.build (parsePattern ("XXXXXXXXXXXXXXXX", ""), 0.6, 1.0, 0.05, 0.0f, false);
    double lowest = 1;
    for (double t = 0; t < 16; t += 0.01) lowest = std::min (lowest, (double) e.eval (t));
    near (lowest, 1.0, 1e-6, "full length holds open (no chatter)");

    e.build (parsePattern ("g...............", ""), 0.5, 0.5, 0.05, 0.0f, false);
    near (e.eval (0.25), ghostLevel, 1e-6, "ghost level");

    e.build (parsePattern ("X-..............", ""), 0.5, 0.5, 0.05, 0.0f, false);
    near (e.eval (1.2), 1.0, 1e-6, "tie holds through the next step");
    near (e.eval (1.7), 0.0, 1e-6, "tie closes after its own length");

    e.build (parsePattern ("XX..............", ""), 0.66, 0.6, 0.05, 0.0f, false);
    near (e.eval (1.2), 0.0, 1e-6, "swung off-step has not opened yet");
    near (e.eval (1.4), 1.0, 1e-6, "swung off-step open");

    e.build (parsePattern ("X...............", ""), 0.5, 0.5, 0.05, 0.25f, false);
    near (e.eval (2.0), 0.25, 1e-6, "closed level follows depth");
    Envelope gaps;
    gaps.buildInverseOf (e, 0.25f);
    near (gaps.eval (0.25), 0.0, 1e-6, "gaps closed while gate open");
    near (gaps.eval (2.0), 1.0, 1e-6, "gaps open while gate closed");

    Envelope send;
    send.build (parsePattern ("X...X...........", "....*..........."), 0.5, 0.5, 0.05, 0.0f, true);
    near (send.eval (0.25), 0.0, 1e-6, "send skips unflagged hits");
    near (send.eval (4.25), 1.0, 1e-6, "send follows flagged hits");

    // Click bound: at SMOOTH = 1 ms the gain never moves more than a raised-cosine ramp allows per sample.
    const double sr = 48000, stepSeconds = 60.0 / 140.0 * 0.25, ramp = 0.001 / stepSeconds;
    const double perSample = 1.0 / (sr * stepSeconds);
    double worst = 0;
    for (std::uint32_t seed = 1; seed <= 200; ++seed) {
        const auto g = generate (seed, -1);
        e.build (g.pattern, g.swing, g.length, ramp, 0.0f, false);
        float prev = e.eval (0);
        for (double t = perSample; t < 16; t += perSample) { const float v = e.eval (t); worst = std::max (worst, (double) std::abs (v - prev)); prev = v; }
    }
    const double bound = 3.14159265 / 2.0 / (0.001 * sr) * 1.02;
    check (worst <= bound, "gain slope within the 1 ms ramp bound", worst, bound);
}

void testGenerator()
{
    std::puts ("generator");
    const auto a = generate (1234, -1), b = generate (1234, -1);
    check (a.pattern == b.pattern && a.name == b.name && std::abs (a.swing - b.swing) < 1e-9f, "same seed, same groove");
    int swung = 0, bad = 0, badRun = 0, badSlap = 0;
    constexpr int rolls = 5000;
    for (std::uint32_t seed = 1; seed <= rolls; ++seed) {
        const auto g = generate (seed * 2654435761u, -1);
        if (g.swing >= 0.55f) ++swung;
        int on = 0, hits = 0, sends = 0;
        for (const auto& c : g.pattern) { on += c.v != Step::off; hits += c.v == Step::hit; sends += c.slap; }
        if (on < 5 || on > 12) ++bad;
        if (hits >= 2 && (sends < 2 || sends > 4)) ++badSlap;
        int run = 0, longest = 0;
        for (int i = 0; i < 2 * numSteps; ++i) { run = g.pattern[(size_t) (i % numSteps)].v == Step::off ? run + 1 : 0; longest = std::max (longest, run); }
        if (longest > 4) ++badRun;
        for (int i = 0; i < numSteps; ++i)
            if (g.pattern[(size_t) i].v == Step::tie && (i == 0 || g.pattern[(size_t) i - 1].v == Step::off)) ++bad;
    }
    check (bad == 0, "density 5-12 and ties follow a sounding step", bad, 0);
    check (badRun == 0, "never more than 4 empty steps in a row", badRun, 0);
    check (badSlap == 0, "2-4 slap sends", badSlap, 0);
    check (swung >= rolls * 8 / 10, "mostly swung under ALL", swung / (double) rolls, 0.8);
    std::printf ("  %.1f%% of %d rolls swung (>= 55%%)\n", 100.0 * swung / rolls, rolls);
    for (int f = 0; f < (int) Family::count; ++f)
        check (generate (99, f).family == (Family) f, "family filter honoured");

    // NEW GROOVE composes new rhythms: never a factory groove or a near copy of one, never a factory
    // name, never the groove it replaces, and hardly ever the same rhythm twice.
    std::vector<Pattern> factoryPatterns;
    for (int i = 0; i < numFactoryGrooves; ++i) factoryPatterns.push_back (parsePattern (factoryGroove (i).cells, factoryGroove (i).slap));
    const auto steps = [] (const Pattern& x, const Pattern& y) {
        int d = 0;
        for (int i = 0; i < numSteps; ++i) d += x[(size_t) i].v != y[(size_t) i].v;
        return d;
    };
    int nearPreset = 0, presetNames = 0, repeatsCurrent = 0;
    std::set<std::string> distinct;
    Pattern current = factoryPatterns[0];
    for (std::uint32_t seed = 1; seed <= 2000; ++seed) {
        const auto g = generate (seed * 2246822519u, (int) (seed % 9) - 2, &current);
        int nearest = numSteps;
        for (const auto& f : factoryPatterns) nearest = std::min (nearest, steps (g.pattern, f));
        if (nearest < 4) ++nearPreset;
        if (factoryIndexOf (g.name) >= 0) ++presetNames;
        if (steps (g.pattern, current) < 4) ++repeatsCurrent;
        distinct.insert (cellsToString (g.pattern));
        current = g.pattern;
    }
    check (nearPreset == 0, "rolled rhythms stay at least 4 steps from every factory groove", nearPreset, 0);
    check (presetNames == 0, "rolled grooves never take a factory name", presetNames, 0);
    check (repeatsCurrent == 0, "each roll differs from the groove it replaces", repeatsCurrent, 0);
    check (distinct.size() >= 1900, "rolls are almost always new rhythms", (double) distinct.size(), 1900);
    std::printf ("  %d distinct rhythms in 2000 rolls\n", (int) distinct.size());
}

void testProcessor()
{
    std::puts ("processor");
    StutterProcessor proc;
    FakePlayHead head;
    proc.setPlayHead (&head);
    setParam (proc, "slapOn", 0); setParam (proc, "length", 50); setParam (proc, "smooth", 2); setParam (proc, "depth", 100);
    setParam (proc, "swing", 50); setParam (proc, "rate", 2);
    proc.setPattern (parsePattern ("X.......X.......", ""));
    proc.prepareToPlay (48000, 512);
    const auto out = render (proc, head, 96000);
    // 120 BPM, 1/16: one step = 6000 samples. Hits at 0 and 48000, each open for half a step.
    near (out[1500], 0.5, 1e-3, "hit passes the input");
    near (out[4500], 0.0, 1e-3, "closed after LENGTH");
    near (out[20000], 0.0, 1e-3, "off steps are silent");
    near (out[48000 + 1500], 0.5, 1e-3, "second hit");
    // Onset: half-open within a sample or two of the grid line (attack centred, plus the 0.5 ms smoother).
    int cross = -1;
    for (int i = 47000; i < 49000; ++i) if (out[(size_t) i] >= 0.25f) { cross = i; break; }
    check (std::abs (cross - 48000) <= 30, "onset lands on the grid", cross, 48000);

    // Null: DEPTH 0, slap off = input.
    StutterProcessor dry;
    FakePlayHead head2;
    dry.setPlayHead (&head2);
    setParam (dry, "slapOn", 0); setParam (dry, "depth", 0);
    dry.prepareToPlay (48000, 512);
    const auto nulled = render (dry, head2, 24000);
    float worst = 0;
    for (auto v : nulled) worst = std::max (worst, std::abs (v - 0.5f));
    check (worst < 1e-5f, "DEPTH 0 with slap off passes the input untouched", worst, 0);

    // Stopped transport keeps gating on the free-running clock.
    StutterProcessor stopped;
    FakePlayHead head3;
    head3.playing = false;
    stopped.setPlayHead (&head3);
    setParam (stopped, "slapOn", 0);
    stopped.prepareToPlay (48000, 512);
    const auto free = render (stopped, head3, 48000);
    float lo = 1, hi = 0;
    for (auto v : free) { lo = std::min (lo, v); hi = std::max (hi, v); }
    check (lo < 0.01f && hi > 0.49f, "stopped transport still gates", hi - lo, 0.5);

    // RATE 1/4: one step is a quarter note, so the 16 steps span four bars.
    StutterProcessor quarter;
    FakePlayHead head4;
    quarter.setPlayHead (&head4);
    setParam (quarter, "slapOn", 0); setParam (quarter, "length", 50); setParam (quarter, "smooth", 2);
    setParam (quarter, "swing", 50); setParam (quarter, "rate", 0);
    check (quarter.apvts.getParameter ("rate")->getCurrentValueAsText() == "1/4", "RATE offers 1/4");
    quarter.setPattern (parsePattern ("X.X.............", ""));
    quarter.prepareToPlay (48000, 512);
    const auto slow = render (quarter, head4, 96000);
    // 120 BPM, 1/4: one step = 24000 samples. Hits at 0 and 48000, each open for half a step.
    near (slow[6000], 0.5, 1e-3, "1/4 hit passes the input");
    near (slow[18000], 0.0, 1e-3, "1/4 closes after LENGTH");
    near (slow[30000], 0.0, 1e-3, "1/4 off step is silent");
    near (slow[54000], 0.5, 1e-3, "1/4 second hit two quarters later");

    // LOCK time.
    auto snap = proc.snapshot();
    snap.swing = 0.62; snap.bpm = 140; snap.rate = 2; snap.slapLock = true;
    near (snap.slapSeconds(), 2.0 * 0.62 * 60.0 / 140.0 * 0.25, 1e-9, "LOCK lands the echo on the swung off-step");
    snap.rate = 0;
    near (snap.slapSeconds(), 2.0 * 0.62 * 60.0 / 140.0, 1e-9, "LOCK follows the swing at 1/4");
    snap.bpm = 60;
    near (snap.slapSeconds(), StutterProcessor::maxLockSeconds, 1e-9, "LOCK stays inside the delay line at slow tempos");
}

void testSlap()
{
    std::puts ("slap");
    SlapDelay d;
    d.prepare (48000);
    SlapDelay::Settings s;
    s.feedback = 0.0f; s.subSafeHz = 120.0f; s.toneHz = 12000.0f;
    d.setSettings (s);
    const auto rmsThrough = [&] (double hz) {
        d.reset();
        double in = 0, out = 0;
        for (int i = 0; i < 96000; ++i) {
            const float x = (float) std::sin (6.283185307 * hz * i / 48000.0);
            float l, r; d.process (x, l, r);
            if (i > 48000) { in += x * x; out += l * l; }
        }
        return 10.0 * std::log10 (out / in);
    };
    const double sub = rmsThrough (40.0), mid = rmsThrough (1000.0);
    check (sub < -30.0, "SUB SAFE keeps 40 Hz out of the echo", sub, -30);
    check (mid > -3.0, "1 kHz echoes through", mid, -3);

    // Maximum feedback stays bounded on loud noise.
    s.feedback = 0.75f; s.toneHz = 12000.0f; s.subSafeHz = 0.0f;
    d.setSettings (s);
    d.reset();
    juce::Random rnd (7);
    float peak = 0; bool finite = true;
    for (int i = 0; i < 48000 * 10; ++i) {
        float l, r; d.process (rnd.nextFloat() * 2.0f - 1.0f, l, r);
        peak = std::max ({ peak, std::abs (l), std::abs (r) });
        finite = finite && std::isfinite (l) && std::isfinite (r);
    }
    check (finite && peak < 4.0f, "max feedback stays bounded", peak, 4.0);
}

void testSubSafeCompatibility()
{
    std::puts ("sub safe compatibility");
   #if JUCE_MAC && JUCE_ARM
    for (const auto& reference : subSafeReferences) {
        const int sr = reference.sampleRate;
        SlapDelay delay;
        delay.prepare (sr);
        SlapDelay::Settings settings;
        settings.subSafeHz = (float) reference.hz; settings.feedback = .37f; settings.toneHz = 3700;
        delay.setSettings (settings); delay.reset();
        std::vector<float> data;
        for (int n = 0; n < sr * 2; ++n) {
            if (n % 257 == 0) delay.setSettings (settings);
            const float x = (float) (.2 * std::sin (6.283185307179586 * 43 * n / sr)
                                  + .17 * std::sin (6.283185307179586 * 731 * n / sr)) + (n % 4093 == 0 ? .4f : 0);
            float l, r; delay.process (x, l, r); data.push_back (l); data.push_back (r);
        }
        check (juce::SHA256 (data.data(), data.size() * sizeof (float)).toHexString() == reference.sha256,
               "fixed SUB SAFE render is sample-identical to 0.4.1 (whole stereo render)");
    }
   #else
    std::puts ("  fixed-render fingerprints skipped: captured on macOS arm64 (libm rounding is platform-specific)");
   #endif
    // Exercise every switch direction on DC with a very short delay, making discontinuities easy to expose.
    for (float from : { 0.0f, 80.0f, 120.0f, 200.0f }) {
        float worst = 0;
        bool finite = true;
        for (float to : { 0.0f, 80.0f, 120.0f, 200.0f }) {
            if (std::abs (from - to) < 1.0f) continue;
            SlapDelay delay;
            delay.prepare (48000);
            SlapDelay::Settings settings;
            settings.timeLeft = settings.timeRight = 2.0f / 48000;
            settings.feedback = 0; settings.toneHz = 12000; settings.subSafeHz = from;
            delay.setSettings (settings); delay.reset();
            float l = 0, r = 0;
            for (int n = 0; n < 48000; ++n) delay.process (.5f, l, r);
            settings.subSafeHz = to; delay.setSettings (settings);
            for (int n = 0; n < 2000; ++n) {
                const float previous = l;
                delay.process (.5f, l, r);
                finite = finite && std::isfinite (l) && std::isfinite (r);
                worst = std::max (worst, std::abs (l - previous));
            }
        }
        check (finite && worst < .02f, "SUB SAFE switches stay finite with no hard DC step", worst, .02);
    }
    SlapDelay rapid;
    rapid.prepare (48000);
    SlapDelay::Settings settings;
    bool finite = true;
    float peak = 0;
    for (int n = 0; n < 48000; ++n) {
        if (n % 32 == 0) { settings.subSafeHz = (float) ((n / 32) % 4) * 80; rapid.setSettings (settings); }
        float l, r; rapid.process (.4f * std::sin ((float) n * .007f), l, r);
        finite = finite && std::isfinite (l) && std::isfinite (r);
        peak = std::max ({ peak, std::abs (l), std::abs (r) });
    }
    check (finite && peak < 2, "rapid SUB SAFE retargeting stays stable", peak, 2);
}

void testConcurrentState()
{
    std::puts ("concurrent state");
    StutterProcessor proc;
    proc.prepareToPlay (48000, 128);
    std::atomic<bool> stop { false };
    std::atomic<int> badStates { 0 }, badSamples { 0 }, saves { 0 }, blocks { 0 };
    std::thread saver ([&] {
        while (! stop.load()) {
            juce::MemoryBlock state;
            proc.getStateInformation (state);
            const auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
            const auto* g = xml != nullptr ? xml->getChildByName ("GROOVE") : nullptr;
            if (g == nullptr || g->getIntAttribute ("schema") != 2 || g->getStringAttribute ("name").isEmpty()) ++badStates;
            else {
                const int preset = g->getIntAttribute ("preset");
                if (preset >= 0 && g->getStringAttribute ("name") != factoryGroove (preset).name) ++badStates;
            }
            const auto name = proc.getProgramName (proc.getCurrentProgram());
            if (name.isEmpty()) ++badStates;
            ++saves;
        }
    });
    std::thread audio ([&] {
        juce::AudioBuffer<float> buffer (2, 128);
        juce::MidiBuffer midi;
        while (! stop.load()) {
            for (int ch = 0; ch < 2; ++ch) for (int n = 0; n < 128; ++n) buffer.setSample (ch, n, .25f);
            proc.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch) for (int n = 0; n < 128; ++n)
                if (! std::isfinite (buffer.getSample (ch, n))) ++badSamples;
            ++blocks;
        }
    });
    for (int i = 0; i < 500; ++i) {
        if (i % 7 == 0) proc.rollNewGroove(); else proc.loadFactory (i % numFactoryGrooves);
        proc.setCell (i % numSteps, { Step::hit, true });
        proc.lockSwing = i % 2 == 0; proc.rollFrom = i % 8; proc.editorScale = 1.25f;
        std::this_thread::yield();
    }
    stop = true; saver.join(); audio.join();
    check (saves > 0 && badStates == 0, "background serialization/program queries safely snapshot changing groove strings");
    check (blocks > 0 && badSamples == 0, "audio keeps rendering finite samples while states are saved");
}

void testState()
{
    std::puts ("state");
    StutterProcessor a;
    a.loadFactory (3);
    a.setCell (5, { Step::ghost, true });
    setParam (a, "slapMix", 77);
    a.lockSwing = true; a.rollFrom = 2; a.editorScale = 1.25f;
    juce::MemoryBlock block;
    a.getStateInformation (block);
   #if JUCE_MAC
    // Captured by compiling HEAD's unmodified 0.4.1 processor and serializing this same state.
    check (juce::SHA256 (block).toHexString() == "d1329ececbc36b88ce6cf580a700516a4f933fd4e0f72ecb8d83d09ccda4b152",
           "schema 2 state bytes match the unmodified 0.4.1 processor");
   #endif
    StutterProcessor b;
    b.setStateInformation (block.getData(), (int) block.getSize());
    check (a.getPattern() == b.getPattern(), "pattern round-trips");
    check (b.getGrooveInfo().name == a.getGrooveInfo().name && b.getGrooveInfo().edited && b.getGrooveInfo().presetIndex == 3, "groove identity round-trips");
    near (b.apvts.getRawParameterValue ("slapMix")->load(), 77.0, 0.11, "parameters round-trip");
    near (b.apvts.getRawParameterValue ("swing")->load(), factoryGroove (3).swing * 100.0, 0.11, "factory swing restored");
    juce::MemoryBlock reopenedState;
    b.getStateInformation (reopenedState);
    check (block == reopenedState, "schema 2 state bytes round-trip unchanged");
    check (b.lockSwing && b.rollFrom == 2 && std::abs (b.editorScale - 1.25f) < 1e-6f, "preferences round-trip");

    // Projects saved before 1/4 existed (schema 1) stored RATE as 0 = 1/8, 1 = 1/16, 2 = 1/32.
    for (const auto& [old, now] : { std::pair { 0, "1/8" }, { 1, "1/16" }, { 2, "1/32" } }) {
        StutterProcessor saved;
        juce::MemoryBlock oldBlock;
        saved.getStateInformation (oldBlock);
        auto xml = juce::AudioProcessor::getXmlFromBinary (oldBlock.getData(), (int) oldBlock.getSize());
        xml->getChildByName ("GROOVE")->setAttribute ("schema", 1);
        for (auto* param : xml->getChildWithTagNameIterator ("PARAM"))
            if (param->getStringAttribute ("id") == "rate") param->setAttribute ("value", old);
        juce::MemoryBlock patched;
        juce::AudioProcessor::copyXmlToBinary (*xml, patched);
        StutterProcessor reopened;
        reopened.setStateInformation (patched.getData(), (int) patched.getSize());
        check (reopened.apvts.getParameter ("rate")->getCurrentValueAsText() == now, "old projects keep their RATE");
    }

    // A rolled groove stays rolled when reopened, even if an older build gave it a factory name.
    StutterProcessor rolled;
    rolled.rollNewGroove();
    check (rolled.getNumPrograms() == numFactoryGrooves && rolled.getCurrentProgram() == 0
               && rolled.getProgramName (0).startsWith ("NEW ") && rolled.getProgramName (0).contains (rolled.getGrooveInfo().name),
           "hosts see the rolled groove in the existing current-program slot");
    rolled.setCurrentProgram (0);
    check (rolled.getGrooveInfo().presetIndex == 0 && rolled.getProgramName (0) == factoryGroove (0).name,
           "selecting program zero still loads Lean Back and restores its factory name");
    rolled.rollNewGroove();
    check (rolled.getGrooveInfo().presetIndex < 0 && factoryIndexOf (rolled.getGrooveInfo().name.toStdString()) < 0, "NEW GROOVE is not a preset");
    juce::MemoryBlock rolledBlock;
    rolled.getStateInformation (rolledBlock);
    auto rolledXml = juce::AudioProcessor::getXmlFromBinary (rolledBlock.getData(), (int) rolledBlock.getSize());
    rolledXml->getChildByName ("GROOVE")->setAttribute ("name", "Rubber Shuffle");
    juce::AudioProcessor::copyXmlToBinary (*rolledXml, rolledBlock);
    StutterProcessor reopenedRoll;
    reopenedRoll.setStateInformation (rolledBlock.getData(), (int) rolledBlock.getSize());
    check (reopenedRoll.getGrooveInfo().presetIndex < 0, "rolled grooves reopen as rolled, not as the preset with that name");
    check (reopenedRoll.getPattern() == rolled.getPattern(), "rolled pattern round-trips");
}
// A held reese-style bass: two detuned saws and a sine sub, about -6 dBFS peak.
std::vector<float> reese (int numSamples, double sr = 48000.0)
{
    std::vector<float> out ((size_t) numSamples);
    double a = 0, b = 0, c = 0, lp = 0;
    for (int i = 0; i < numSamples; ++i) {
        const double f = 43.65;
        a = std::fmod (a + f * 1.007 / sr, 1.0); b = std::fmod (b + f * 0.993 / sr, 1.0); c += f / sr;
        const double saw = (a * 2 - 1) + (b * 2 - 1);
        lp += (saw - lp) * 0.06;
        out[(size_t) i] = (float) (0.22 * std::tanh (1.6 * lp) + 0.28 * std::sin (6.283185307 * c));
    }
    return out;
}

void testFactoryBank()
{
    std::puts ("factory bank");
    int badCells = 0, badSlaps = 0;
    for (int i = 0; i < numFactoryGrooves; ++i) {
        const auto& f = factoryGroove (i);
        const auto p = parsePattern (f.cells, f.slap);
        if (std::string (f.cells).size() != 16 || std::string (f.slap).size() != 16) ++badCells;
        for (const auto& c : p) if (c.slap && c.v != Step::hit && c.v != Step::ghost) ++badSlaps;
        check (factoryIndexOf (f.name) == i, "factory names are unique");
    }
    check (badCells == 0, "every pattern has 16 steps", badCells, 0);
    check (badSlaps == 0, "slap sends sit on sounding steps", badSlaps, 0);

    // Every preset on a loud held bass: never clips, never collapses to silence, always finite.
    const auto bass = reese (48000 * 4);
    double inRms = 0;
    for (auto v : bass) inRms += v * v;
    inRms = std::sqrt (inRms / bass.size());
    int clipped = 0, quiet = 0, broken = 0;
    for (int i = 0; i < numFactoryGrooves; ++i) {
        StutterProcessor proc;
        FakePlayHead head;
        head.bpm = 140.0;
        proc.setPlayHead (&head);
        proc.loadFactory (i);
        proc.prepareToPlay (48000, 512);
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        double peak = 0, rms = 0;
        for (int done = 0; done + 512 <= (int) bass.size(); done += 512) {
            for (int ch = 0; ch < 2; ++ch) for (int n = 0; n < 512; ++n) buffer.setSample (ch, n, bass[(size_t) (done + n)]);
            proc.processBlock (buffer, midi);
            head.sample += 512;
            for (int ch = 0; ch < 2; ++ch) for (int n = 0; n < 512; ++n) {
                const float v = buffer.getSample (ch, n);
                if (! std::isfinite (v)) ++broken;
                peak = std::max (peak, (double) std::abs (v));
                rms += v * v;
            }
        }
        rms = std::sqrt (rms / (bass.size() * 2.0));
        if (peak > 0.99) { ++clipped; std::printf ("  clips: %s (%.2f)\n", factoryGroove (i).name, peak); }
        if (rms < 0.2 * inRms) { ++quiet; std::printf ("  too quiet: %s (%.2f of input)\n", factoryGroove (i).name, rms / inRms); }
    }
    check (clipped == 0, "no factory groove clips a -6 dBFS bass", clipped, 0);
    check (quiet == 0, "no factory groove drops below 20% of the input level", quiet, 0);
    check (broken == 0, "all output finite", broken, 0);

    // A 0.1.0 project saved Syrup Drag as preset 3; it now lives elsewhere in the bank and must still be found.
    StutterProcessor oldProject;
    oldProject.loadFactory (factoryIndexOf ("Syrup Drag"));
    juce::MemoryBlock block;
    oldProject.getStateInformation (block);
    auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
    xml->getChildByName ("GROOVE")->setAttribute ("preset", 3);
    juce::MemoryBlock patched;
    juce::AudioProcessor::copyXmlToBinary (*xml, patched);
    StutterProcessor reopened;
    reopened.setStateInformation (patched.getData(), (int) patched.getSize());
    check (reopened.getGrooveInfo().presetIndex == factoryIndexOf ("Syrup Drag"), "old projects find their preset by name");
}
} // namespace


static void testUserGrooves()
{
    std::puts ("user grooves");
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("stutter-user-grooves-test");
    root.deleteRecursively();
    setenv ("STUTTER_STEP_PRESET_ROOT", root.getFullPathName().toRawUTF8(), 1);
    check (StutterProcessor::userGrooveRoot() == root, "tests use a temporary USER folder");
    check (StutterProcessor::userGrooveFiles().isEmpty(), "USER starts empty");

    StutterProcessor a;
    a.rollNewGroove();
    setParam (a, "rate", 1); setParam (a, "depth", 70); setParam (a, "slapMix", 55); setParam (a, "slapGaps", 1);
    a.setCell (3, { Step::ghost, true });
    const auto saved = a.getPattern();
    const float swing = a.apvts.getRawParameterValue ("swing")->load(), length = a.apvts.getRawParameterValue ("length")->load();
    check (a.saveUserGroove ("  My Groove  ").wasOk(), "a rolled groove saves to USER");
    const auto file = root.getChildFile (juce::String ("My Groove") + StutterProcessor::grooveExtension);
    check (file.existsAsFile(), "the file is named after the groove");
    check (a.getGrooveInfo().user == "My Groove" && a.getGrooveInfo().name == "My Groove" && ! a.getGrooveInfo().edited && a.getGrooveInfo().presetIndex == -1,
           "the saved groove becomes the USER groove");
    check (a.getNumPrograms() == numFactoryGrooves && a.getCurrentProgram() == 0 && a.getProgramName (0) == "My Groove",
           "hosts see the USER name without adding or reordering programs");
    check (a.userGrooveIndex() == 0, "it is the first USER groove");

    StutterProcessor b;
    check (b.loadUserGroove (file), "a USER groove loads");
    check (b.getPattern() == saved, "its pattern and slap sends come back");
    near (b.apvts.getRawParameterValue ("rate")->load(), 1, 1e-6, "its rate comes back");
    near (b.apvts.getRawParameterValue ("depth")->load(), 70, 0.05, "its depth comes back");
    near (b.apvts.getRawParameterValue ("slapMix")->load(), 55, 0.05, "its slap mix comes back");
    near (b.apvts.getRawParameterValue ("slapGaps")->load(), 1, 1e-6, "GAPS comes back");
    near (b.apvts.getRawParameterValue ("swing")->load(), swing, 0.05, "its swing comes back");
    near (b.apvts.getRawParameterValue ("length")->load(), length, 0.05, "its length comes back");
    check (b.getGrooveInfo().user == "My Groove" && b.getGrooveInfo().presetIndex == -1, "a loaded USER groove knows its name");

    // A different groove never replaces a saved one; the loaded USER groove updates its own file.
    b.rollNewGroove();
    check (b.saveUserGroove ("My Groove").wasOk() && b.getGrooveInfo().name == "My Groove 2", "a taken name gets a number");
    StutterProcessor reread;
    check (reread.loadUserGroove (file) && reread.getPattern() == saved, "the first groove is untouched");
    a.setCell (0, { Step::hit, false });
    check (a.getGrooveInfo().edited, "editing a USER groove marks it");
    check (a.saveUserGroove ("my groove").wasOk() && StutterProcessor::userGrooveFiles().size() == 2 && ! a.getGrooveInfo().edited,
           "saving the loaded USER groove under its own name updates it");
    check (a.getGrooveInfo().user == "My Groove" && file.existsAsFile(), "an update keeps the groove's name");
    check (reread.loadUserGroove (file) && reread.getPattern() == a.getPattern(), "the update reached the file");
    check (a.saveUserGroove (" ... ").failed() && a.saveUserGroove ("").failed(), "an empty name is refused");
    check (! StutterProcessor::cleanGrooveName ("../a/b:c").containsAnyOf ("/:\\") && ! StutterProcessor::cleanGrooveName ("..x").startsWithChar ('.'),
           "names can't leave the USER folder");

    // The arrows run through the factory grooves, then USER, and wrap.
    StutterProcessor c;
    c.loadFactory (numFactoryGrooves - 1);
    c.stepGroove (1);
    check (c.getGrooveInfo().user == "My Groove", "after the last factory groove comes the first USER groove");
    c.stepGroove (1);
    check (c.getGrooveInfo().user == "My Groove 2", "then the next USER groove");
    c.stepGroove (1);
    check (c.getGrooveInfo().presetIndex == 0 && c.getGrooveInfo().user.isEmpty(), "then back to the first factory groove");
    c.stepGroove (-1);
    check (c.getGrooveInfo().user == "My Groove 2", "and backwards from it to the last USER groove");
    root.getChildFile (juce::String ("Broken") + StutterProcessor::grooveExtension).replaceWithText ("not a groove");
    check (! c.loadUserGroove (root.getChildFile (juce::String ("Broken") + StutterProcessor::grooveExtension)), "an unreadable file doesn't load");
    c.loadFactory (numFactoryGrooves - 1);
    c.stepGroove (1);
    check (c.getGrooveInfo().user == "Broken" || c.getGrooveInfo().user == "My Groove", "stepping skips an unreadable file");

    // Projects remember a USER groove.
    juce::MemoryBlock block;
    a.getStateInformation (block);
    StutterProcessor restored;
    restored.setStateInformation (block.getData(), (int) block.getSize());
    check (restored.getGrooveInfo().user == "My Groove" && restored.getGrooveInfo().presetIndex == -1 && restored.getPattern() == a.getPattern(),
           "a project reopens on its USER groove");
    restored.loadFactory (2);
    restored.getStateInformation (block);
    StutterProcessor factory;
    factory.setStateInformation (block.getData(), (int) block.getSize());
    check (factory.getGrooveInfo().user.isEmpty() && factory.getGrooveInfo().presetIndex == 2, "a factory groove isn't marked USER");
    root.deleteRecursively();
    unsetenv ("STUTTER_STEP_PRESET_ROOT");
}

// What a host and the screen see of a processor: every parameter, the pattern and the groove's identity.
static bool sameGroove (StutterProcessor& a, StutterProcessor& b)
{
    for (int i = 0; i < a.getParameters().size(); ++i)
        if (std::abs (a.getParameters()[i]->getValue() - b.getParameters()[i]->getValue()) > 1e-6f) return false;
    return a.getPattern() == b.getPattern() && a.getGrooveInfo().name == b.getGrooveInfo().name && a.getGrooveInfo().family == b.getGrooveInfo().family
        && a.getGrooveInfo().presetIndex == b.getGrooveInfo().presetIndex && a.getGrooveInfo().user == b.getGrooveInfo().user && a.getGrooveInfo().edited == b.getGrooveInfo().edited;
}

struct HostCounter final : juce::AudioProcessorListener
{
    int values = 0, gestures = 0, changes = 0;
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { ++values; }
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int) override { ++gestures; }
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override { ++changes; }
};

static void testRandomGroove()
{
    std::puts ("random groove");
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("stutter-random-grooves-test");
    root.deleteRecursively();
    setenv ("STUTTER_STEP_PRESET_ROOT", root.getFullPathName().toRawUTF8(), 1);

    // USER empty: only factory grooves, every one of them in time, never the one already loaded.
    juce::Random random (7);
    StutterProcessor a;
    std::set<int> seen;
    int repeats = 0, misses = 0, notFactory = 0;
    for (int i = 0; i < 2000; ++i) {
        const int before = a.getGrooveInfo().presetIndex;
        if (! a.loadRandomGroove (random)) { ++misses; continue; }
        repeats += a.getGrooveInfo().presetIndex == before ? 1 : 0;
        notFactory += a.getGrooveInfo().presetIndex < 0 || a.getGrooveInfo().user.isNotEmpty() ? 1 : 0;
        seen.insert (a.getGrooveInfo().presetIndex);
    }
    check (misses == 0, "a random groove always loads", misses, 0);
    check (repeats == 0, "a random pick is never the groove already loaded", repeats, 0);
    check (notFactory == 0, "with no USER grooves it picks factory grooves", notFactory, 0);
    check ((int) seen.size() == numFactoryGrooves, "every factory groove comes up", (double) seen.size(), numFactoryGrooves);

    // It loads exactly as the arrows and the menu do: same state, same host notifications, swing lock honoured.
    StutterProcessor picked, manual;
    HostCounter pickedHost, manualHost;
    picked.addListener (&pickedHost);
    manual.addListener (&manualHost);
    juce::Random once (11);
    check (picked.loadRandomGroove (once) && picked.getGrooveInfo().presetIndex != 0, "a fresh instance moves off its first groove");
    manual.loadFactory (picked.getGrooveInfo().presetIndex);
    check (sameGroove (picked, manual), "a random factory groove matches loading it from the menu");
    check (pickedHost.gestures > 0 && pickedHost.values > 0 && pickedHost.changes > 0, "the host hears about a random groove");
    check (pickedHost.gestures == manualHost.gestures && pickedHost.values == manualHost.values && pickedHost.changes == manualHost.changes,
           "and hears exactly what a manual pick sends");
    picked.removeListener (&pickedHost);
    manual.removeListener (&manualHost);
    juce::MemoryBlock block;
    picked.getStateInformation (block);
    StutterProcessor reopened;
    reopened.setStateInformation (block.getData(), (int) block.getSize());
    check (reopened.getGrooveInfo().presetIndex == picked.getGrooveInfo().presetIndex && reopened.getPattern() == picked.getPattern(),
           "a project reopens on the random groove");
    StutterProcessor locked;
    locked.lockSwing = true;
    setParam (locked, "swing", 73.0f);
    locked.loadRandomGroove (random);
    near (locked.apvts.getRawParameterValue ("swing")->load(), 73.0, 0.05, "the swing padlock holds through a random groove");

    // With USER grooves: factory and USER both come up, never the loaded USER groove, and never an unreadable file.
    StutterProcessor saver;
    for (const auto* name : { "Rand One", "Rand Two", "Rand Three" }) {
        saver.rollNewGroove();
        saver.saveUserGroove (name);
    }
    root.getChildFile (juce::String ("Broken") + StutterProcessor::grooveExtension).replaceWithText ("not a groove");
    StutterProcessor b;
    std::set<juce::String> users;
    int factoryPicks = 0, broken = 0, userRepeats = 0;
    misses = 0;
    for (int i = 0; i < 3000; ++i) {
        const auto before = b.getGrooveInfo().user;
        if (! b.loadRandomGroove (random)) { ++misses; continue; }
        if (b.getGrooveInfo().presetIndex >= 0) { ++factoryPicks; continue; }
        users.insert (b.getGrooveInfo().user);
        broken += b.getGrooveInfo().user == "Broken" ? 1 : 0;
        userRepeats += b.getGrooveInfo().user == before ? 1 : 0;
    }
    check (misses == 0, "a random groove always loads with USER grooves too", misses, 0);
    check (factoryPicks > 0 && users.size() == 3, "factory and every readable USER groove come up", (double) users.size(), 3);
    check (broken == 0, "an unreadable USER file is never loaded", broken, 0);
    check (userRepeats == 0, "a random pick is never the USER groove already loaded", userRepeats, 0);
    for (int i = 0; i < 400 && b.getGrooveInfo().user.isEmpty(); ++i) b.loadRandomGroove (random);
    StutterProcessor fromMenu;
    check (b.getGrooveInfo().user.isNotEmpty() && fromMenu.loadUserGroove (root.getChildFile (b.getGrooveInfo().user + StutterProcessor::grooveExtension))
               && sameGroove (b, fromMenu),
           "a random USER groove matches loading it from the menu");
    check (b.userGrooveIndex() >= 0, "and shows as USER n / m");
    root.deleteRecursively();
    unsetenv ("STUTTER_STEP_PRESET_ROOT");
}

// ---- the DRAG key's MIDI clip ----
struct ClipNote
{
    int on, off, velocity;
    bool operator== (const ClipNote&) const = default;
};
struct Clip
{
    std::vector<ClipNote> notes;
    int end = -1, otherEvents = 0, badNotes = 0;
    bool orderedOffBeforeOn = true; // a note-on never arrives while the note still sounds
    double secondsPerQuarter = 0;
    juce::String name;
    int numerator = 0, denominator = 0;
};
static Clip readClip (const juce::MidiFile& file)
{
    Clip c;
    if (file.getNumTracks() != 1) { c.badNotes = 1; return c; }
    const auto& track = *file.getTrack (0);
    bool sounding = false;
    for (int i = 0; i < track.getNumEvents(); ++i) {
        const auto* e = track.getEventPointer (i);
        const auto& m = e->message;
        const int t = juce::roundToInt (m.getTimeStamp());
        if (m.isNoteOn()) {
            if (sounding) c.orderedOffBeforeOn = false;
            sounding = true;
            if (m.getChannel() != 1 || m.getNoteNumber() != clipNoteNumber || e->noteOffObject == nullptr) ++c.badNotes;
            c.notes.push_back ({ t, e->noteOffObject != nullptr ? juce::roundToInt (e->noteOffObject->message.getTimeStamp()) : -1, m.getVelocity() });
        } else if (m.isNoteOff()) {
            sounding = false;
        } else if (m.isEndOfTrackMetaEvent()) {
            c.end = t;
        } else if (m.isTempoMetaEvent()) {
            c.secondsPerQuarter = m.getTempoSecondsPerQuarterNote();
        } else if (m.isTrackNameEvent()) {
            c.name = m.getTextFromTextMetaEvent();
        } else if (m.isTimeSignatureMetaEvent()) {
            m.getTimeSignatureInfo (c.numerator, c.denominator);
        } else {
            ++c.otherEvents;
        }
    }
    return c;
}
static juce::MemoryBlock clipBytes (const juce::MidiFile& file)
{
    juce::MemoryOutputStream out;
    file.writeTo (out, 0);
    return out.getMemoryBlock();
}
static juce::MidiFile readMidi (const void* data, size_t size)
{
    juce::MemoryInputStream in (data, size, false);
    juce::MidiFile file;
    if (! file.readFrom (in)) file.clear();
    return file;
}

static void testMidiClip()
{
    std::puts ("midi clip");
    check (ghostVelocity == juce::roundToInt (hitVelocity * ghostLevel), "ghosts sit at the gate's ghost level", ghostVelocity, hitVelocity * ghostLevel);

    // Worked by hand at 1/16, 60% swing, LENGTH 50%: 240 ticks a step, even slots 1.2 steps, odd slots 0.8.
    const auto hand = parsePattern ("X.g-X...--..g..X", "");
    const auto clip = makeMidiClip (hand, 0.6, 0.5, 0.25, 140.0, "Hand Made");
    const auto c = readClip (clip);
    const std::vector<ClipNote> want { { 0, 144, 112 }, { 480, 864, 43 }, { 960, 1104, 112 }, { 1920, 2304, 112 },
                                       { 2880, 3024, 43 }, { 3648, 3744, 112 } };
    check (c.notes == want, "hand-worked clip: swing, LENGTH, ghosts, a held tie and a tie after silence", (double) c.notes.size(), 6);
    check (c.badNotes == 0 && c.otherEvents == 0, "every note is C1 on channel 1 with a matching off, and nothing else is in the track");
    check (c.end == 3840, "a 1/16 cycle is one bar long", c.end, 3840);
    check (clip.getTimeFormat() == clipTicksPerQuarter, "960 ticks per quarter", clip.getTimeFormat(), clipTicksPerQuarter);
    near (c.secondsPerQuarter, 60.0 / 140.0, 1.0e-6, "the tempo is the host's");
    check (c.name == "Hand Made" && c.numerator == 4 && c.denominator == 4, "the track carries the groove's name and 4/4");

    // Written and read back, the file says the same.
    const auto bytes = clipBytes (clip);
    const auto back = readClip (readMidi (bytes.getData(), bytes.getSize()));
    check (back.notes == want && back.end == 3840 && back.name == "Hand Made", "the written file reads back unchanged");

    const auto quarter = readClip (makeMidiClip (hand, 0.6, 0.5, 1.0, 140.0, "q"));
    check (quarter.notes.size() == 6 && quarter.notes[1] == ClipNote { 1920, 3456, 43 } && quarter.end == 15360,
           "1/4 spans four bars", quarter.end, 15360);
    const auto thirtySecond = readClip (makeMidiClip (hand, 0.6, 0.5, 0.125, 140.0, "t"));
    bool repeated = thirtySecond.notes.size() == 12;
    for (size_t i = 0; repeated && i < 6; ++i)
        repeated = thirtySecond.notes[i + 6] == ClipNote { thirtySecond.notes[i].on + 1920, thirtySecond.notes[i].off + 1920, thirtySecond.notes[i].velocity };
    check (repeated && thirtySecond.notes[5] == ClipNote { 1824, 1872, 112 } && thirtySecond.end == 3840,
           "1/32 plays twice to fill a whole bar", thirtySecond.end, 3840);
    const auto empty = readClip (makeMidiClip (parsePattern ("................", ""), 0.6, 0.5, 0.25, 140.0, "e"));
    check (empty.notes.empty() && empty.end == 3840, "an empty groove still makes a one-bar clip");
    const auto lastBarEmpty = readClip (makeMidiClip (parsePattern ("X...X...X.......", ""), 0.5, 0.5, 1.0, 140.0, "l"));
    check (lastBarEmpty.end == 15360, "an empty last bar keeps its length", lastBarEmpty.end, 15360);
    const auto legato = readClip (makeMidiClip (parsePattern ("XXXXXXXXXXXXXXXX", ""), 0.6, 1.0, 0.25, 140.0, "x"));
    bool joined = legato.notes.size() == 16;
    for (size_t i = 0; joined && i + 1 < legato.notes.size(); ++i) joined = legato.notes[i].off == legato.notes[i + 1].on;
    check (joined && legato.orderedOffBeforeOn, "LENGTH 100% joins the notes, each off before the next on");
    const auto held = readClip (makeMidiClip (parsePattern ("X---------------", ""), 0.6, 1.0, 0.25, 140.0, "h"));
    check (held.notes.size() == 1 && held.notes[0] == ClipNote { 0, 3840, 112 }, "a groove tied all the way is one note");

    // Every factory groove at every RATE, straight to fully swung, shortest to longest LENGTH: a note sounds exactly
    // where the processor's own gate is open, at the gate's level.
    int mismatches = 0, levelMismatches = 0, badClips = 0, cases = 0;
    for (int g = 0; g < numFactoryGrooves; ++g) {
        const auto& f = factoryGroove (g);
        const auto pattern = parsePattern (f.cells, f.slap);
        for (int rate = 0; rate < 4; ++rate)
            for (const double swing : { 0.5, (double) f.swing, 0.75 })
                for (const double length : { 0.05, (double) f.length, 1.0 }) {
                    ++cases;
                    const double stepBeats = StutterProcessor::rateBeats (rate), ticksPerStep = stepBeats * clipTicksPerQuarter;
                    const auto m = readClip (makeMidiClip (pattern, swing, length, stepBeats, 120.0, f.name));
                    const int cycle = juce::roundToInt (numSteps * ticksPerStep), repeats = rate == 3 ? 2 : 1;
                    bool ok = m.badNotes == 0 && m.orderedOffBeforeOn && m.end == cycle * repeats && m.notes.size() % (size_t) repeats == 0;
                    for (const auto& n : m.notes)
                        ok = ok && n.off > n.on && n.on >= 0 && n.off <= m.end && (n.velocity == hitVelocity || n.velocity == ghostVelocity);
                    if (! ok) { ++badClips; continue; }
                    Envelope gate;
                    gate.build (pattern, swing, length, 0.002, 0.0f, false);
                    for (int k = 0; k < 1600; ++k) {
                        const double t = (k + 0.5) / 1600.0 * numSteps * repeats, tick = t * ticksPerStep;
                        const ClipNote* in = nullptr;
                        bool nearEdge = false;
                        for (const auto& n : m.notes) {
                            if (tick >= n.on && tick < n.off) in = &n;
                            nearEdge = nearEdge || std::abs (tick - n.on) < 0.01 * ticksPerStep || std::abs (tick - n.off) < 0.01 * ticksPerStep;
                        }
                        if (nearEdge) continue;
                        const float level = gate.eval (std::fmod (t, (double) numSteps));
                        if ((level > 0.1f) != (in != nullptr)) ++mismatches;
                        else if (in != nullptr && std::abs (level - (in->velocity == hitVelocity ? 1.0f : ghostLevel)) > 0.01f) ++levelMismatches;
                    }
                }
    }
    check (badClips == 0, "every factory clip is well formed: whole bars, notes in order, C1, two velocities", badClips, 0);
    check (mismatches == 0, "notes sound exactly where the gate is open, at every RATE, swing and LENGTH", mismatches, 0);
    check (levelMismatches == 0, "hits are loud and ghosts soft where the gate says so", levelMismatches, 0);
    check (cases == numFactoryGrooves * 36, "36 settings per factory groove", cases, numFactoryGrooves * 36);

    // The processor writes the clip it plays, named after the groove, and replaces it on the next drag.
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("stutter-midi-" + juce::String (juce::Time::currentTimeMillis()));
    StutterProcessor proc;
    proc.uiBpm = 140.0;
    proc.loadFactory (factoryIndexOf ("Glass Stairs"));
    const auto expected = [&proc] {
        const auto s = proc.snapshot();
        return makeMidiClip (s.pattern, s.swing, s.length, StutterProcessor::rateBeats (s.rate), s.bpm,
                             StutterProcessor::cleanGrooveName (proc.getGrooveInfo().name));
    };
    const auto file = proc.writeMidiClip (folder);
    juce::MemoryBlock written;
    check (file == folder.getChildFile ("Glass Stairs.mid") && file.loadFileAsData (written) && written == clipBytes (expected()),
           "the file is named after the groove and holds its clip");
    proc.setCell (0, { proc.getPattern()[0].v == Step::off ? Step::hit : Step::off, false });
    const auto again = proc.writeMidiClip (folder);
    juce::MemoryBlock rewritten;
    check (again == file && again.loadFileAsData (rewritten) && rewritten == clipBytes (expected()) && rewritten != written,
           "an edited groove replaces the earlier file");
    check (folder.findChildFiles (juce::File::findFiles, false).size() == 1, "and leaves no temporary files behind");
    proc.rollNewGroove();
    const auto rolled = proc.writeMidiClip (folder);
    check (rolled.getFileNameWithoutExtension() == StutterProcessor::cleanGrooveName (proc.getGrooveInfo().name) && rolled.existsAsFile(),
           "a rolled groove's clip takes its rolled name");
    const auto blocker = folder.getChildFile ("not a folder");
    blocker.replaceWithText ("x");
    check (proc.writeMidiClip (blocker) == juce::File(), "an unwritable folder gives no file rather than a broken one");
    check (StutterProcessor::midiClipFolder().isAChildOf (juce::File::getSpecialLocation (juce::File::tempDirectory)),
           "clips go to the temp directory");
    folder.deleteRecursively();
}

int main()
{
    // Pure console: no GUI initialiser, MessageManager, NSApplication or editor.
    // APVTS copyState() flushes parameter values synchronously; its UI timer isn't needed here.
    check (juce::MessageManager::getInstanceWithoutCreating() == nullptr, "console tests start without a GUI/message runtime");
    testTiming();
    testEnvelope();
    testGenerator();
    testProcessor();
    testSlap();
    testSubSafeCompatibility();
    testConcurrentState();
    testState();
    testFactoryBank();
    testUserGrooves();
    testRandomGroove();
    testMidiClip();
    check (juce::MessageManager::getInstanceWithoutCreating() == nullptr, "DSP/state tests never create a GUI/message runtime");
    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
