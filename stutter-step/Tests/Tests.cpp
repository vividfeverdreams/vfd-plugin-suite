// STUTTER-STEP console tests: groove math, generator, processor renders, slap, state, MIDI clips.
#include "Groove.h"
#include "MidiClip.h"
#include "SlapDelay.h"
#include "StutterProcessor.h"
#include <juce_events/juce_events.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <thread>
#include <juce_cryptography/juce_cryptography.h>
#include "SubSafeReference.h"
#include "Compat043.h"

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
        if (g.family == Family::hats) ++bad; // ALL never rolls HATS
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
    check (bad == 0, "density 5-12, ties follow a sounding step, and ALL never rolls HATS", bad, 0);
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
        const auto g = generate (seed * 2246822519u, (int) (seed % 10) - 2, &current);
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
    // 0.5.0 adds to the state (the TIMING parameter, the edit layer, send levels when any is below full); states saved
    // by 0.4.3 load and sound identical (testCompat043).
    {
        const auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
        const auto* g = xml != nullptr ? xml->getChildByName ("GROOVE") : nullptr;
        bool timing = false;
        if (xml != nullptr)
            for (const auto* param : xml->getChildWithTagNameIterator ("PARAM")) timing = timing || param->getStringAttribute ("id") == "timing";
        check (g != nullptr && g->getIntAttribute ("schema") == 2 && timing && g->hasAttribute ("editLayer") && ! g->hasAttribute ("sendLevels"),
               "states keep schema 2 and add TIMING and the edit layer; full sends write no levels");
    }
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

    // 0.5.0: dotted and triplet steps, and nudged grooves whose held notes wrap the loop. A held note carries on from
    // one cycle into the next with no new attack; the clip only starts again where it loops itself.
    int timedMismatches = 0, timedBad = 0, wrapped = 0, retriggers = 0;
    cases = 0;
    for (int g = 0; g < numFactoryGrooves; ++g) {
        const auto& f = factoryGroove (g);
        const auto original = parsePattern (f.cells, f.slap);
        for (const int timing : { 0, 1, 2 })
            for (const int by : { 0, -1, 3 }) {
                ++cases;
                const auto pattern = rotated (original, by);
                const double stepBeats = StutterProcessor::stepBeats (f.rate, timing), ticksPerStep = stepBeats * clipTicksPerQuarter;
                const int repeats = clipRepeats (stepBeats), cycle = juce::roundToInt (numSteps * ticksPerStep);
                const auto m = readClip (makeMidiClip (pattern, f.swing, f.length, stepBeats, 120.0, f.name));
                const bool wraps = pattern[0].v == Step::tie && pattern[15].v != Step::off;
                wrapped += wraps ? 1 : 0;
                bool ok = m.badNotes == 0 && m.orderedOffBeforeOn && std::abs (m.end - cycle * repeats) <= 1
                       && std::abs (std::fmod (m.end / (4.0 * clipTicksPerQuarter), 1.0)) < 1.0e-6;
                for (const auto& n : m.notes) ok = ok && n.off > n.on && n.on >= 0 && n.off <= m.end;
                if (! ok) { ++timedBad; continue; }
                Envelope gate;
                gate.build (pattern, f.swing, f.length, 0.002, 0.0f, false);
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
                    if ((level > 0.1f) != (in != nullptr)) ++timedMismatches;
                }
                // Inside the clip, a note never restarts at a cycle boundary when the gate stays open across it.
                for (int r = 1; r < repeats && wraps; ++r)
                    for (const auto& n : m.notes)
                        if (std::abs (n.on - r * cycle) <= 1 && gate.eval (numSteps - 0.001) > 0.5f) ++retriggers;
            }
    }
    check (timedBad == 0, "dotted, triplet and nudged clips are well formed, whole 4/4 bars", timedBad, 0);
    check (timedMismatches == 0, "their notes sound exactly where the gate is open", timedMismatches, 0);
    check (wrapped > 10 && retriggers == 0, "held notes that wrap the loop carry on inside the clip", wrapped, 0);
    check (cases == numFactoryGrooves * 9, "nine timings and nudges per factory groove", cases, numFactoryGrooves * 9);

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

// ---- compatibility: projects and .ssgroove files saved by 0.4.3 must load and sound identical ----
namespace compat {
struct Run { double bpm; juce::int64 start; bool playing; };

// Three bars of a held reese through a loaded processor; the SHA-256 of every output sample, both channels.
static juce::String renderHash (StutterProcessor& proc, const Run& run)
{
    FakePlayHead head;
    head.bpm = run.bpm; head.sample = run.start; head.playing = run.playing;
    proc.setPlayHead (&head);
    proc.prepareToPlay (48000, 512);
    const int total = (int) std::lround (48000.0 * 60.0 / run.bpm * 12.0);
    const auto bass = reese (total);
    std::vector<float> data;
    data.reserve ((size_t) total * 2);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    for (int done = 0; done < total; done += 512) {
        const int n = std::min (512, total - done);
        buffer.setSize (2, n, false, false, true);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < n; ++i) buffer.setSample (ch, i, bass[(size_t) (done + i)]);
        proc.processBlock (buffer, midi);
        for (int i = 0; i < n; ++i) { data.push_back (buffer.getSample (0, i)); data.push_back (buffer.getSample (1, i)); }
        head.sample += n;
    }
    proc.setPlayHead (nullptr);
    return juce::SHA256 (data.data(), data.size() * sizeof (float)).toHexString();
}

static juce::String clipsHash()
{
    juce::MemoryOutputStream all;
    for (int g = 0; g < numFactoryGrooves043; ++g) {
        const auto& f = factoryGroove (g);
        for (int rate = 0; rate < 4; ++rate)
            makeMidiClip (parsePattern (f.cells, f.slap), f.swing, f.length, StutterProcessor::rateBeats (rate), 140.0, f.name).writeTo (all, 0);
    }
    return juce::SHA256 (all.getData(), all.getDataSize()).toHexString();
}

// Writes Tests/Compat043.h. Run on the unmodified 0.4.3 Source: STUTTER-STEP Tests --write-compat <header>.
static int writeFixtures (const juce::File& header)
{
    juce::String out;
    out << "#pragma once\n"
        << "// Saved by the unmodified 0.4.3 processor (STUTTER-STEP Tests --write-compat, 2026-11-03, macOS arm64):\n"
        << "// host states and .ssgroove files (base64), with the SHA-256 of each one's render in compat::renderHash.\n"
        << "// libm may round differently elsewhere; the fingerprints target macOS arm64.\n"
        << "struct CompatState { const char* name; const char* state; double bpm; long long start; bool playing; const char* sha256; };\n"
        << "struct CompatGroove { const char* name; const char* file; double bpm; const char* sha256; };\n"
        << "inline const CompatState compatStates[] {\n";
    const auto addBlock = [&] (const juce::String& name, const juce::MemoryBlock& block, Run run) {
        StutterProcessor fresh;
        fresh.setStateInformation (block.getData(), (int) block.getSize());
        out << "    { \"" << name << "\", \"" << block.toBase64Encoding() << "\", " << juce::String (run.bpm, 1) << ", "
            << juce::String (run.start) << ", " << (run.playing ? "true" : "false") << ", \"" << renderHash (fresh, run) << "\" },\n";
    };
    const auto addState = [&] (const juce::String& name, StutterProcessor& from, Run run) {
        juce::MemoryBlock block;
        from.getStateInformation (block);
        addBlock (name, block, run);
    };
    const double tempos[] { 140.0, 90.0, 174.0 };
    for (int i = 0; i < numFactoryGrooves; ++i) {
        StutterProcessor p;
        p.loadFactory (i);
        addState (juce::String ("factory ") + factoryGroove (i).name, p, { tempos[i % 3], (juce::int64) i * 7919, true });
    }
    {   // Edited: a ghost send, a tie, 1/32, free slap time, GAPS, no WIDE, partial depth, SUB SAFE 200, quieter output.
        StutterProcessor p;
        p.loadFactory (5);
        p.setCell (3, { Step::ghost, true }); p.setCell (9, { Step::tie, false }); p.setCell (12, { Step::hit, true });
        setParam (p, "rate", 3); setParam (p, "swing", 71); setParam (p, "slapLock", 0); setParam (p, "slapTime", 37);
        setParam (p, "slapGaps", 1); setParam (p, "slapWide", 0); setParam (p, "depth", 70); setParam (p, "smooth", 12);
        setParam (p, "subSafe", 3); setParam (p, "output", -3);
        addState ("edited factory groove", p, { 174.0, 12345, true });
    }
    {   // Sends on an off step and on tie continuations (the engine ignores them), at 1/4.
        StutterProcessor p;
        p.setPattern (parsePattern ("X--.g.X...X-.gX.", ".**.*.....*.**.*"));
        setParam (p, "rate", 0); setParam (p, "slapFeedback", 60); setParam (p, "slapMix", 80);
        addState ("ignored sends at 1/4", p, { 90.0, 0, true });
    }
    {   StutterProcessor p;
        p.rollNewGroove();
        setParam (p, "rate", 1);
        addState ("rolled groove at 1/8", p, { 128.0, 48000, true });
    }
    {   StutterProcessor p;
        p.loadFactory (12);
        setParam (p, "slapOn", 0);
        addState ("slap off", p, { 140.0, 0, true });
    }
    {   StutterProcessor p;
        p.loadFactory (20);
        setParam (p, "bypass", 1);
        addState ("bypassed", p, { 140.0, 0, true });
    }
    {   StutterProcessor p;
        p.loadFactory (30);
        addState ("stopped transport", p, { 100.0, 0, false });
    }
    {   // Schema 1 (0.1/0.2): RATE stored as 0 = 1/8, 1 = 1/16, 2 = 1/32.
        StutterProcessor p;
        p.loadFactory (7);
        juce::MemoryBlock block;
        p.getStateInformation (block);
        auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
        xml->getChildByName ("GROOVE")->setAttribute ("schema", 1);
        for (auto* param : xml->getChildWithTagNameIterator ("PARAM"))
            if (param->getStringAttribute ("id") == "rate") param->setAttribute ("value", 0);
        juce::MemoryBlock patched;
        juce::AudioProcessor::copyXmlToBinary (*xml, patched);
        addBlock ("schema 1 project", patched, { 140.0, 0, true });
    }
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("stutter-compat-fixtures");
    root.deleteRecursively();
    setenv ("STUTTER_STEP_PRESET_ROOT", root.getFullPathName().toRawUTF8(), 1);
    {   StutterProcessor p;
        p.rollNewGroove();
        p.saveUserGroove ("Compat User");
        addState ("USER groove project", p, { 140.0, 0, true });
    }
    out << "};\ninline const CompatGroove compatGrooves[] {\n";
    const auto addGroove = [&] (const juce::String& name, StutterProcessor& from, double bpm) {
        from.saveUserGroove (name);
        const auto file = root.getChildFile (name + StutterProcessor::grooveExtension);
        juce::MemoryBlock text;
        file.loadFileAsData (text);
        StutterProcessor fresh;
        fresh.loadUserGroove (file);
        out << "    { \"" << name << "\", \"" << text.toBase64Encoding() << "\", " << juce::String (bpm, 1) << ", \""
            << renderHash (fresh, { bpm, 0, true }) << "\" },\n";
    };
    {   StutterProcessor p;
        p.loadFactory (17);
        p.setCell (2, { Step::ghost, true });
        setParam (p, "slapLock", 0); setParam (p, "slapTime", 61); setParam (p, "depth", 85);
        addGroove ("Compat Edited", p, 140.0);
    }
    {   StutterProcessor p;
        p.rollNewGroove();
        setParam (p, "rate", 0); setParam (p, "slapGaps", 1);
        addGroove ("Compat Quarter", p, 90.0);
    }
    {   StutterProcessor p;
        p.loadFactory (40);
        setParam (p, "rate", 3); setParam (p, "swing", 55);
        addGroove ("Compat Thirty Second", p, 174.0);
    }
    out << "};\n";
    root.deleteRecursively();
    unsetenv ("STUTTER_STEP_PRESET_ROOT");
    out << "// Every factory groove's DRAG MIDI clip at each RATE (140 BPM), written back to back.\n"
        << "inline const char* const compatClipsSha256 = \"" << clipsHash() << "\";\n";
    return header.replaceWithText (out) ? 0 : 1;
}
} // namespace compat

// ---- 0.5.0 ----
static void testCompat043()
{
    std::puts ("0.4.3 compatibility");
    // The 0.4.3 bank keeps its indices and names; SIMPLE and HATS follow it.
    int moved = 0;
    for (int i = 0; i < numFactoryGrooves043; ++i)
        if (juce::String ("factory ") + factoryGroove (i).name != compatStates[i].name) ++moved;
    check (moved == 0, "the 43 grooves of 0.4.3 keep their place and name", moved, 0);
    check (compat::clipsHash() == compatClipsSha256, "every 0.4.3 groove's DRAG MIDI clip is byte-identical at every RATE");
   #if JUCE_MAC && JUCE_ARM
    int states = 0, sameSound = 0, sameIntoDirty = 0, straight = 0, gateLayer = 0;
    for (const auto& fixture : compatStates) {
        juce::MemoryBlock block;
        block.fromBase64Encoding (fixture.state);
        const compat::Run run { fixture.bpm, (juce::int64) fixture.start, fixture.playing };
        StutterProcessor fresh;
        fresh.setStateInformation (block.getData(), (int) block.getSize());
        straight += juce::roundToInt (fresh.apvts.getRawParameterValue ("timing")->load()) == 0 ? 1 : 0;
        gateLayer += fresh.editLayer.load() == 0 ? 1 : 0;
        sameSound += compat::renderHash (fresh, run) == fixture.sha256 ? 1 : 0;
        // An instance already set to triplet, the SLAP layer and quieter sends opens the old project the same way.
        StutterProcessor dirty;
        setParam (dirty, "timing", 2);
        dirty.editLayer = 1;
        auto pattern = dirty.getPattern();
        for (auto& c : pattern) { c.slap = true; c.send = 7; }
        dirty.setPattern (pattern);
        dirty.setStateInformation (block.getData(), (int) block.getSize());
        straight += juce::roundToInt (dirty.apvts.getRawParameterValue ("timing")->load()) == 0 ? 1 : 0;
        gateLayer += dirty.editLayer.load() == 0 ? 1 : 0;
        sameIntoDirty += compat::renderHash (dirty, run) == fixture.sha256 ? 1 : 0;
        ++states;
    }
    std::printf ("  %d of %d 0.4.3 projects render byte-identical\n", sameSound, states);
    check (sameSound == states, "projects saved by 0.4.3 render byte-identical (factory, edited, rolled, USER, schema 1, slap off, bypass, stopped)", sameSound, states);
    check (sameIntoDirty == states, "and identical when loaded over an instance on triplet timing with other sends", sameIntoDirty, states);
    check (straight == 2 * states && gateLayer == 2 * states, "old projects open straight, on the GATE layer, whatever the instance had",
           straight, 2 * states);

    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("stutter-compat-grooves");
    root.deleteRecursively();
    root.createDirectory();
    int grooves = 0, sameGroove = 0;
    for (const auto& fixture : compatGrooves) {
        juce::MemoryBlock text;
        text.fromBase64Encoding (fixture.file);
        const auto file = root.getChildFile (juce::String (fixture.name) + StutterProcessor::grooveExtension);
        file.replaceWithData (text.getData(), text.getSize());
        StutterProcessor dirty;
        setParam (dirty, "timing", 1);
        check (dirty.loadUserGroove (file), "a 0.4.3 .ssgroove loads");
        sameGroove += compat::renderHash (dirty, { fixture.bpm, 0, true }) == fixture.sha256 ? 1 : 0;
        ++grooves;
    }
    check (sameGroove == grooves, ".ssgroove files saved by 0.4.3 load straight and render byte-identical", sameGroove, grooves);
    root.deleteRecursively();
   #else
    std::puts ("  render fingerprints skipped: captured on macOS arm64");
   #endif
}

static void testSendLevels()
{
    std::puts ("send levels");
    auto p = parsePattern ("X...g...X-......", "*...*...*.......");
    check (sendLevelsToString (p).empty(), "full sends write no levels (as before 0.5.0)");
    p[0].send = 10; p[4].send = 1; p[13].send = 13;
    const auto text = sendLevelsToString (p);
    Pattern back = parsePattern ("X...g...X-......", "*...*...*.......");
    applySendLevels (back, text);
    check (back == p, "levels round-trip through text", 0, 0);
    Pattern junk = parsePattern ("X...............", "*...............");
    applySendLevels (junk, "50 x9 abc 250 -3");
    check (junk[0].send == 10 && junk[1].send == fullSend && junk[3].send == fullSend && junk[5].send == fullSend,
           "unreadable levels stay full, numbers are clamped");

    Envelope send;
    send.build (p, 0.5, 0.5, 0.05, 0.0f, true);
    near (send.eval (0.25), 0.5, 1e-6, "a 50% send sends half the hit");
    near (send.eval (4.25), ghostLevel * 0.05, 1e-6, "a 5% send on a ghost");
    near (send.eval (8.25), 1.0, 1e-6, "a full send is unchanged");
    Envelope gate;
    gate.build (p, 0.5, 0.5, 0.05, 0.0f, false);
    near (gate.eval (0.25), 1.0, 1e-6, "send levels never touch the gate");

    StutterProcessor proc;
    proc.setPattern (p);
    check (proc.getPattern() == p, "the processor keeps send levels");
    juce::MemoryBlock block;
    proc.getStateInformation (block);
    StutterProcessor reopened;
    reopened.setStateInformation (block.getData(), (int) block.getSize());
    check (reopened.getPattern() == p, "projects keep send levels");

    // How much echo: half the send, half the slap (linear), dry unchanged.
    const auto wet = [] (int level) {
        StutterProcessor q;
        FakePlayHead head;
        q.setPlayHead (&head);
        setParam (q, "depth", 0); setParam (q, "slapMix", 100); setParam (q, "slapGaps", 0); setParam (q, "subSafe", 0);
        auto pat = parsePattern ("X...............", "*...............");
        pat[0].send = (std::uint8_t) level;
        q.setPattern (pat);
        q.prepareToPlay (48000, 512);
        const auto out = render (q, head, 24000);
        double e = 0;
        for (size_t i = 6000; i < out.size(); ++i) e += (out[i] - 0.5) * (out[i] - 0.5);
        return std::sqrt (e);
    };
    const double full = wet (fullSend), half = wet (fullSend / 2);
    near (half / full, 0.5, 0.01, "a 50% send gives half the echo");

    // Who owns a step's send.
    const auto q = parsePattern ("-X--..-.g-......", "");
    check (eventOwner (q, 0) == 0 && eventOwner (q, 2) == 1 && eventOwner (q, 3) == 1 && eventOwner (q, 4) == -1
               && eventOwner (q, 6) == 6 && eventOwner (q, 9) == 8, "a held step belongs to the note it continues");
    const auto wrap = parsePattern ("--..X.........X-", "");
    check (eventOwner (wrap, 0) == 14 && eventOwner (wrap, 1) == 14 && eventOwner (wrap, 15) == 14, "around the loop too");
    check (eventOwner (parsePattern ("----------------", ""), 9) == 0, "every step tied: the first step starts it");
}

static void testNudge()
{
    std::puts ("nudge");
    int identity = 0;
    for (int i = 0; i < numFactoryGrooves; ++i) {
        auto p = parsePattern (factoryGroove (i).cells, factoryGroove (i).slap);
        p[3].send = 9;
        auto r = p;
        for (int k = 0; k < numSteps; ++k) r = rotated (r, 1);
        identity += rotated (rotated (p, -1), 1) == p && r == p && rotated (p, 5) == rotated (rotated (p, 2), 3) ? 1 : 0;
    }
    check (identity == numFactoryGrooves, "nudging back undoes a nudge; 16 nudges come home", identity, numFactoryGrooves);
    const auto p = parsePattern ("X-..g.X...X-.gX.", "......*.......*.");
    const auto r = rotated (p, 1);
    check (r[0] == p[15] && r[7] == p[6] && r[7].slap && r[12].v == Step::tie, "each step keeps its gate, tie and send");

    // Moving a groove is moving its sound: the gate (and the sends) shifted by whole steps, held notes held across the
    // loop. Straight swing moves by one step; with swing, two steps keep every hit on its swung place.
    int shiftedWrong = 0, cases = 0;
    for (int i = 0; i < numFactoryGrooves; ++i) {
        auto pat = parsePattern (factoryGroove (i).cells, factoryGroove (i).slap);
        for (const auto& [swing, by] : { std::pair { 0.5, 1 }, { 0.5, -1 }, { 0.62, 2 }, { 0.71, -2 } }) {
            ++cases;
            Envelope a, b, sa, sb;
            const double length = factoryGroove (i).length;
            a.build (pat, swing, length, 0.03, 0.1f, false);
            b.build (rotated (pat, by), swing, length, 0.03, 0.1f, false);
            sa.build (pat, swing, length, 0.03, 0.0f, true);
            sb.build (rotated (pat, by), swing, length, 0.03, 0.0f, true);
            float worst = 0;
            for (double t = 0; t < 16; t += 0.0137)
                worst = std::max ({ worst, std::abs (b.eval (t + by) - a.eval (t)), std::abs (sb.eval (t + by) - sa.eval (t)) });
            if (worst > 1e-4f) { ++shiftedWrong; std::printf ("  %s nudged %d at swing %.2f: %.5f\n", factoryGroove (i).name, by, swing, worst); }
        }
    }
    check (shiftedWrong == 0, "every factory groove nudged plays the same gate and sends, shifted in time", shiftedWrong, 0);
    check (cases == numFactoryGrooves * 4, "four nudges per groove");

    // The processor: edited, the host hears it, saves and DRAG MIDI follow.
    StutterProcessor proc;
    HostCounter host;
    proc.addListener (&host);
    proc.loadFactory (factoryIndexOf ("Lean Back"));
    const auto before = proc.getPattern();
    const auto revision = proc.grooveRevision.load();
    const int changes = host.changes;
    proc.nudge (-1);
    check (proc.getPattern() == rotated (before, -1) && proc.getGrooveInfo().edited && proc.getGrooveInfo().name == "Lean Back",
           "NUDGE moves the groove and marks it edited, like any edit");
    check (proc.grooveRevision.load() != revision && host.changes > changes, "the screen and the host hear about it");
    proc.removeListener (&host);
    juce::MemoryBlock block;
    proc.getStateInformation (block);
    StutterProcessor reopened;
    reopened.setStateInformation (block.getData(), (int) block.getSize());
    check (reopened.getPattern() == proc.getPattern() && reopened.getGrooveInfo().edited, "projects keep the nudged groove");
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("stutter-nudge-" + juce::String (juce::Time::currentTimeMillis()));
    proc.uiBpm = 140.0;
    const auto file = proc.writeMidiClip (folder);
    juce::MemoryBlock written;
    file.loadFileAsData (written);
    const auto s = proc.snapshot();
    juce::MemoryOutputStream want;
    makeMidiClip (rotated (before, -1), s.swing, s.length, StutterProcessor::stepBeats (s.rate, s.timing), 140.0, "Lean Back").writeTo (want, 0);
    check (written == want.getMemoryBlock(), "DRAG MIDI writes the nudged groove");
    folder.deleteRecursively();

    // In the processor, at 125 BPM (a 1/16 step is 5760 samples): the nudged render is the original, one step late.
    const auto renderOf = [] (const Pattern& pat) {
        StutterProcessor q;
        FakePlayHead head;
        head.bpm = 125.0;
        q.setPlayHead (&head);
        q.loadFactory (factoryIndexOf ("Lean Back"));
        setParam (q, "slapOn", 0); setParam (q, "swing", 50); setParam (q, "rate", 2);
        q.setPattern (pat);
        q.prepareToPlay (48000, 512);
        return render (q, head, 48000 * 3);
    };
    const auto original = renderOf (before), later = renderOf (rotated (before, 1));
    float worst = 0;
    for (size_t n = 5760 + 1000; n < later.size(); ++n) worst = std::max (worst, std::abs (later[n] - original[n - 5760]));
    check (worst < 1e-4f, "nudged one step later sounds exactly one step later", worst, 0);
}

static void testDottedTriplet()
{
    std::puts ("dotted and triplet");
    for (int rate = 0; rate < 4; ++rate) {
        const double timed = StutterProcessor::stepBeats (rate, 0), plain = StutterProcessor::rateBeats (rate);
        check (std::memcmp (&timed, &plain, sizeof timed) == 0, "straight is RATE exactly");
        near (StutterProcessor::stepBeats (rate, 1), 1.5 * StutterProcessor::rateBeats (rate), 1e-12, "dotted is 1.5 times RATE");
        near (StutterProcessor::stepBeats (rate, 2), 2.0 / 3.0 * StutterProcessor::rateBeats (rate), 1e-12, "triplet is two thirds of RATE");
    }
    StutterProcessor timingParam;
    auto* parameter = timingParam.apvts.getParameter ("timing");
    check (parameter != nullptr && parameter->getCurrentValueAsText() == "Straight" && timingParam.getParameters().getLast() == parameter,
           "TIMING is a host parameter, appended last, default straight");

    // The gate opens on the timed grid, with swing, at several tempos, across the loop.
    int late = 0, wrongState = 0, onsets = 0;
    for (const double bpm : { 90.0, 140.0, 174.0 })
        for (const int timing : { 1, 2 })
            for (const int rate : { 1, 2, 3 }) {
                StutterProcessor proc;
                FakePlayHead head;
                head.bpm = bpm;
                proc.setPlayHead (&head);
                setParam (proc, "slapOn", 0); setParam (proc, "length", 50); setParam (proc, "smooth", 1); setParam (proc, "depth", 100);
                setParam (proc, "swing", 62); setParam (proc, "rate", (float) rate); setParam (proc, "timing", (float) timing);
                proc.setPattern (parsePattern ("XX..X...X.X.X..X", ""));
                proc.prepareToPlay (48000, 512);
                const double stepSamples = 48000.0 * 60.0 / bpm * StutterProcessor::stepBeats (rate, timing);
                const int cycles = 2;
                const auto out = render (proc, head, (int) std::ceil (stepSamples * 16 * cycles) + 512);
                const auto pat = proc.getPattern();
                for (int c = 0; c < cycles; ++c)
                    for (int i = 0; i < 16; ++i) {
                        const double start = (c * 16 + stepStart (i, 0.62)) * stepSamples, slot = slotLength (i, 0.62) * stepSamples;
                        const bool sounding = pat[(size_t) i].v == Step::hit;
                        const float mid = out[(size_t) std::lround (start + slot * 0.25)];
                        if (sounding != (mid > 0.45f)) ++wrongState;
                        if (! sounding || start < 1) continue;
                        int cross = -1;
                        for (int n = (int) start - 200; n < (int) start + 200; ++n) if (out[(size_t) n] >= 0.25f) { cross = n; break; }
                        ++onsets;
                        if (std::abs (cross - start) > 30) ++late;
                    }
            }
    check (wrongState == 0, "dotted and triplet steps open and close on their grid, swing included (90, 140, 174 BPM)", wrongState, 0);
    check (late == 0, "every onset lands within 30 samples of its timed grid line, past the loop too", late, onsets);

    // LOCK follows the timed step; the playhead the screen draws stays on the gate's clock.
    StutterProcessor proc;
    auto snap = proc.snapshot();
    snap.swing = 0.62; snap.bpm = 140; snap.rate = 2; snap.slapLock = true; snap.timing = 2;
    near (snap.slapSeconds(), 2.0 * 0.62 * 60.0 / 140.0 * 0.25 * 2.0 / 3.0, 1e-9, "LOCK lands the echo on the swung triplet off-step");
    snap.timing = 1;
    near (snap.slapSeconds(), 2.0 * 0.62 * 60.0 / 140.0 * 0.25 * 1.5, 1e-9, "and on the dotted one");
    FakePlayHead head;
    head.bpm = 132.0;
    proc.setPlayHead (&head);
    setParam (proc, "rate", 2); setParam (proc, "timing", 1);
    proc.prepareToPlay (48000, 512);
    render (proc, head, 100000);
    near (proc.uiPosition.load(), std::fmod (100000.0 / 48000.0 * 132.0 / 60.0 / 0.375, 16.0), 1e-6, "the playhead runs on dotted steps");

    // No clicks: the host loops every two bars while dotted and triplet cycles run across the bar line, and each
    // jump back is smoothed like any gate edge (held input, SMOOTH 1 ms).
    float worstStep = 0;
    for (const int timing : { 0, 1, 2 }) {
        StutterProcessor q;
        FakePlayHead h;
        h.bpm = 140.0;
        q.setPlayHead (&h);
        q.loadFactory (factoryIndexOf ("Lean Back"));
        q.nudge (-1); // a tie now wraps the loop
        setParam (q, "slapOn", 0); setParam (q, "smooth", 1); setParam (q, "timing", (float) timing);
        q.prepareToPlay (48000, 256);
        const int loop = (int) std::lround (48000.0 * 60.0 / 140.0 * 8.0);
        std::vector<float> out;
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        for (int done = 0; done < loop * 3; done += 256) {
            h.sample = done % loop; // the host's loop: the song position jumps back every two bars
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 256; ++i) buffer.setSample (ch, i, 0.5f);
            q.processBlock (buffer, midi);
            for (int i = 0; i < 256; ++i) out.push_back (buffer.getSample (0, i));
        }
        for (size_t i = 1; i < out.size(); ++i) worstStep = std::max (worstStep, std::abs (out[i] - out[i - 1]));
    }
    const float smoothing = 1.0f - std::exp (-1.0f / (0.0005f * 48000.0f));
    check (worstStep <= 0.5f * smoothing * 1.02f, "loop jumps and cycle wraps never step the gain faster than its smoother", worstStep, 0.5 * smoothing);

    // DRAG MIDI writes what is heard: the clip's onsets are the rendered onsets.
    for (const int timing : { 1, 2 }) {
        StutterProcessor q;
        FakePlayHead h;
        h.bpm = 140.0;
        q.setPlayHead (&h);
        setParam (q, "slapOn", 0); setParam (q, "length", 50); setParam (q, "smooth", 1); setParam (q, "swing", 58);
        setParam (q, "rate", 2); setParam (q, "timing", (float) timing);
        q.setPattern (parsePattern ("X..X..g.X.X...X.", ""));
        q.prepareToPlay (48000, 512);
        const auto out = render (q, h, 48000 * 4);
        const auto s = q.snapshot();
        const auto clip = makeMidiClip (s.pattern, s.swing, s.length, StutterProcessor::stepBeats (s.rate, s.timing), 140.0, "t");
        const auto& track = *clip.getTrack (0);
        int matched = 0, notes = 0;
        for (int e = 0; e < track.getNumEvents(); ++e) {
            const auto& m = track.getEventPointer (e)->message;
            if (! m.isNoteOn()) continue;
            const double sample = m.getTimeStamp() / clipTicksPerQuarter * 60.0 / 140.0 * 48000.0;
            if (sample > out.size() - 200) continue;
            ++notes;
            const float level = m.getVelocity() == hitVelocity ? 1.0f : ghostLevel;
            int cross = -1;
            for (int n = juce::jmax (0, (int) sample - 200); n < (int) sample + 200; ++n) if (out[(size_t) n] >= 0.25f * level) { cross = n; break; }
            matched += std::abs (cross - sample) <= 30 || sample < 1 ? 1 : 0;
        }
        check (notes > 6 && matched == notes, timing == 1 ? "dotted clip notes start where the gate opens" : "triplet clip notes start where the gate opens", matched, notes);
    }
    const std::pair<double, int> lengths[] { { 0.25 * 1.5, 2 }, { 0.5 * 1.5, 1 }, { 1.5, 1 }, { 0.125 * 1.5, 4 },
                                             { 0.25 * 2 / 3.0, 3 }, { 0.5 * 2 / 3.0, 3 }, { 2 / 3.0, 3 }, { 0.125 * 2 / 3.0, 3 },
                                             { 0.125, 2 }, { 0.25, 1 }, { 0.5, 1 }, { 1.0, 1 } };
    int wrongRepeats = 0;
    for (const auto& [beats, repeats] : lengths) wrongRepeats += clipRepeats (beats) == repeats ? 0 : 1;
    check (wrongRepeats == 0, "clips are whole bars: dotted 1/16 twice (3 bars), triplet 1/16 three times (2 bars)", wrongRepeats, 0);

    // Grooves own their timing as they own RATE; NEW GROOVE keeps what you set; USER grooves save it.
    StutterProcessor g;
    g.loadFactory (factoryIndexOf ("Triplet Rolls"));
    check (juce::roundToInt (g.apvts.getRawParameterValue ("timing")->load()) == 2, "a triplet factory groove sets triplet timing");
    g.loadFactory (0);
    check (juce::roundToInt (g.apvts.getRawParameterValue ("timing")->load()) == 0, "and the older grooves set straight");
    setParam (g, "timing", 1); setParam (g, "rate", 3);
    for (const int family : { -1, (int) Family::hats }) {
        g.rollFrom = family;
        g.rollNewGroove();
        check (juce::roundToInt (g.apvts.getRawParameterValue ("timing")->load()) == 1 && juce::roundToInt (g.apvts.getRawParameterValue ("rate")->load()) == 3,
               "NEW GROOVE keeps your timing and RATE");
    }
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("stutter-timing-grooves");
    root.deleteRecursively();
    setenv ("STUTTER_STEP_PRESET_ROOT", root.getFullPathName().toRawUTF8(), 1);
    auto withLevels = g.getPattern();
    withLevels[0].slap = true; withLevels[0].send = 11;
    g.setPattern (withLevels);
    check (g.saveUserGroove ("Dotted One").wasOk(), "a dotted groove saves");
    StutterProcessor other;
    setParam (other, "timing", 2);
    check (other.loadUserGroove (root.getChildFile (juce::String ("Dotted One") + StutterProcessor::grooveExtension))
               && juce::roundToInt (other.apvts.getRawParameterValue ("timing")->load()) == 1 && other.getPattern() == withLevels,
           "USER grooves keep their timing and send levels");
    root.deleteRecursively();
    unsetenv ("STUTTER_STEP_PRESET_ROOT");

    // The swing padlock holds through every groove, the new ones included, and through rolls.
    StutterProcessor locked;
    locked.lockSwing = true;
    setParam (locked, "swing", 73.0f);
    bool held = true;
    for (int i = 0; i < numFactoryGrooves; ++i) { locked.loadFactory (i); held = held && std::abs (locked.apvts.getRawParameterValue ("swing")->load() - 73.0f) < 0.05f; }
    locked.rollFrom = (int) Family::hats;
    locked.rollNewGroove();
    locked.nudge (3);
    held = held && std::abs (locked.apvts.getRawParameterValue ("swing")->load() - 73.0f) < 0.05f;
    check (held, "the swing padlock holds through all 75 grooves, a HATS roll and a nudge");
}

static void testNewBank()
{
    std::puts ("SIMPLE and HATS");
    int simple = 0, hats = 0, misplaced = 0, hatProblems = 0, simpleProblems = 0, twins = 0;
    for (int i = 0; i < numFactoryGrooves; ++i) {
        const auto& f = factoryGroove (i);
        const juce::String folder (f.folder);
        if (i < numFactoryGrooves043) { misplaced += folder == "SIMPLE" || folder == "HATS" || f.timing != 0 ? 1 : 0; continue; }
        if (folder == "SIMPLE") { ++simple; misplaced += hats > 0 ? 1 : 0; simpleProblems += f.swing > 0.55f || f.smoothMs < 3.0f ? 1 : 0; }
        else if (folder == "HATS") {
            ++hats;
            int sends = 0;
            for (const auto& c : parsePattern (f.cells, f.slap)) sends += c.slap ? 1 : 0;
            hatProblems += f.family != Family::hats || f.length > 0.5f || f.smoothMs > 1.5f || f.depth < 1.0f || sends > 1 || f.mix > 0.2f ? 1 : 0;
        }
        else ++misplaced;
        for (int j = 0; j < i; ++j) {
            const auto& o = factoryGroove (j);
            twins += std::string (o.cells) == f.cells && o.rate == f.rate && o.timing == f.timing && std::abs (o.swing - f.swing) < 0.01f
                      && std::abs (o.length - f.length) < 0.05f ? 1 : 0;
        }
    }
    check (simple == 15 && hats == 17 && misplaced == 0, "SIMPLE (15) then HATS (17) follow the 43 older grooves", simple * 100 + hats, 1517);
    check (simpleProblems == 0, "SIMPLE grooves are straight or lightly swung, with click-free edges", simpleProblems, 0);
    check (hatProblems == 0, "HATS grooves: short LENGTH, crisp SMOOTH, full DEPTH, slap off or one faint send", hatProblems, 0);
    check (twins == 0, "no two grooves are the same rhythm, timing, swing and LENGTH", twins, 0);
    StutterProcessor proc;
    check (proc.getNumPrograms() == 75 && proc.getProgramName (74) == "Dotted Hats" && proc.getProgramName (43) == "Quarter Chug",
           "hosts see 75 programs, the new ones after the old");

    // HATS rolls: busy and accented, short, never on a factory groove, no slap sends.
    int bad = 0;
    for (std::uint32_t seed = 1; seed <= 2000; ++seed) {
        const auto g = generate (seed * 2654435761u, (int) Family::hats);
        int on = 0, sends = 0;
        for (const auto& c : g.pattern) { on += c.v != Step::off; sends += c.slap; }
        bad += g.family != Family::hats || on < 8 || sends != 0 || g.length > 0.4f || g.swing > 0.58f ? 1 : 0;
    }
    check (bad == 0, "HATS rolls: 8-16 steps, short, lightly swung, no sends", bad, 0);
}

int main (int argc, char** argv)
{
    if (argc > 2 && juce::String (argv[1]) == "--write-compat") return compat::writeFixtures (juce::File (juce::String (argv[2])));
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
    testCompat043();
    testSendLevels();
    testNudge();
    testDottedTriplet();
    testNewBank();
    check (juce::MessageManager::getInstanceWithoutCreating() == nullptr, "DSP/state tests never create a GUI/message runtime");
    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
