// STUTTER-STEP console tests: groove math, generator, processor renders, slap, state.
#include "Groove.h"
#include "SlapDelay.h"
#include "StutterProcessor.h"
#include <juce_events/juce_events.h>
#include <cmath>
#include <cstdio>
#include <set>

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
    StutterProcessor b;
    b.setStateInformation (block.getData(), (int) block.getSize());
    check (a.getPattern() == b.getPattern(), "pattern round-trips");
    check (b.groove.name == a.groove.name && b.groove.edited && b.groove.presetIndex == 3, "groove identity round-trips");
    near (b.apvts.getRawParameterValue ("slapMix")->load(), 77.0, 0.11, "parameters round-trip");
    near (b.apvts.getRawParameterValue ("swing")->load(), factoryGroove (3).swing * 100.0, 0.11, "factory swing restored");
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
    check (rolled.groove.presetIndex < 0 && factoryIndexOf (rolled.groove.name.toStdString()) < 0, "NEW GROOVE is not a preset");
    rolled.groove.name = "Rubber Shuffle";
    juce::MemoryBlock rolledBlock;
    rolled.getStateInformation (rolledBlock);
    StutterProcessor reopenedRoll;
    reopenedRoll.setStateInformation (rolledBlock.getData(), (int) rolledBlock.getSize());
    check (reopenedRoll.groove.presetIndex < 0, "rolled grooves reopen as rolled, not as the preset with that name");
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
    check (reopened.groove.presetIndex == factoryIndexOf ("Syrup Drag"), "old projects find their preset by name");
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
    check (a.groove.user == "My Groove" && a.groove.name == "My Groove" && ! a.groove.edited && a.groove.presetIndex == -1,
           "the saved groove becomes the USER groove");
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
    check (b.groove.user == "My Groove" && b.groove.presetIndex == -1, "a loaded USER groove knows its name");

    // A different groove never replaces a saved one; the loaded USER groove updates its own file.
    b.rollNewGroove();
    check (b.saveUserGroove ("My Groove").wasOk() && b.groove.name == "My Groove 2", "a taken name gets a number");
    StutterProcessor reread;
    check (reread.loadUserGroove (file) && reread.getPattern() == saved, "the first groove is untouched");
    a.setCell (0, { Step::hit, false });
    check (a.groove.edited, "editing a USER groove marks it");
    check (a.saveUserGroove ("my groove").wasOk() && StutterProcessor::userGrooveFiles().size() == 2 && ! a.groove.edited,
           "saving the loaded USER groove under its own name updates it");
    check (a.groove.user == "My Groove" && file.existsAsFile(), "an update keeps the groove's name");
    check (reread.loadUserGroove (file) && reread.getPattern() == a.getPattern(), "the update reached the file");
    check (a.saveUserGroove (" ... ").failed() && a.saveUserGroove ("").failed(), "an empty name is refused");
    check (! StutterProcessor::cleanGrooveName ("../a/b:c").containsAnyOf ("/:\\") && ! StutterProcessor::cleanGrooveName ("..x").startsWithChar ('.'),
           "names can't leave the USER folder");

    // The arrows run through the factory grooves, then USER, and wrap.
    StutterProcessor c;
    c.loadFactory (numFactoryGrooves - 1);
    c.stepGroove (1);
    check (c.groove.user == "My Groove", "after the last factory groove comes the first USER groove");
    c.stepGroove (1);
    check (c.groove.user == "My Groove 2", "then the next USER groove");
    c.stepGroove (1);
    check (c.groove.presetIndex == 0 && c.groove.user.isEmpty(), "then back to the first factory groove");
    c.stepGroove (-1);
    check (c.groove.user == "My Groove 2", "and backwards from it to the last USER groove");
    root.getChildFile (juce::String ("Broken") + StutterProcessor::grooveExtension).replaceWithText ("not a groove");
    check (! c.loadUserGroove (root.getChildFile (juce::String ("Broken") + StutterProcessor::grooveExtension)), "an unreadable file doesn't load");
    c.loadFactory (numFactoryGrooves - 1);
    c.stepGroove (1);
    check (c.groove.user == "Broken" || c.groove.user == "My Groove", "stepping skips an unreadable file");

    // Projects remember a USER groove.
    juce::MemoryBlock block;
    a.getStateInformation (block);
    StutterProcessor restored;
    restored.setStateInformation (block.getData(), (int) block.getSize());
    check (restored.groove.user == "My Groove" && restored.groove.presetIndex == -1 && restored.getPattern() == a.getPattern(),
           "a project reopens on its USER groove");
    restored.loadFactory (2);
    restored.getStateInformation (block);
    StutterProcessor factory;
    factory.setStateInformation (block.getData(), (int) block.getSize());
    check (factory.groove.user.isEmpty() && factory.groove.presetIndex == 2, "a factory groove isn't marked USER");
    root.deleteRecursively();
    unsetenv ("STUTTER_STEP_PRESET_ROOT");
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    testTiming();
    testEnvelope();
    testGenerator();
    testProcessor();
    testSlap();
    testState();
    testFactoryBank();
    testUserGrooves();
    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
