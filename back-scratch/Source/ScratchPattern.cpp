#include "ScratchPattern.h"

#include <algorithm>
#include <cmath>

namespace backScratch {
namespace {

constexpr std::array<FactoryPreset, factoryPresetCount> presets {{
    {"Sub Tug", "BASS", "Two roomy push-pull scratches, leaving the downbeat intact."},
    {"Rubber Teeth", "BASS", "Three accelerating rubbery pulls with unequal note lengths."},
    {"Half-Time Drag", "BASS", "An open first half, a long reverse growl, and a short release."},
    {"Triplet Growl", "BASS", "Three swung growls answer a clear opening beat."},
    {"Tape Throat", "BASS", "A descending tape bend ending in a clipped backward bark."},
    {"Switchblade", "BASS", "Sharp crossfader cuts across two asymmetric push-pulls."},
    {"Snarl & Release", "BASS", "A reverse snarl followed by two progressively longer throws."},
    {"Offbeat Chew", "BASS", "Small offbeat bites that keep the main bass rhythm readable."},
    {"Two-Bar Beast", "BASS", "A sparse two-bar phrase with a dense triplet turnaround."},
    {"Kickback", "DRUMS", "A tiny pickup and a longer backspin, with room for the kick."},
    {"Snare Snatch", "DRUMS", "Backward snare accents and a late pickup into the downbeat."},
    {"Pocket Chirp", "DRUMS", "Short gated chirps tucked between the main drum hits."},
    {"Breakspin", "DRUMS", "An open break that develops into a rolling scratch fill."},
    {"Triplet Chopper", "DRUMS", "Three tightly cut scratches in the final beat."},
    {"Last-Beat Flare", "DRUMS", "Three clean beats, then a doubled crossfader flourish."},
    {"Rewind Roll", "DRUMS", "Repeating sixteenth-note material falls into a reverse throw."},
    {"Two-Bar Turn", "DRUMS", "A two-bar turnaround combining chirps, a roll, and a pullback."},
    {"Word Flip", "VOCALS", "A backward word answered by a longer forward release."},
    {"Stutter Talk", "VOCALS", "A fast repeated syllable opens into a gated baby scratch."},
    {"Baby Talk", "VOCALS", "Two rounded, unequal baby scratches for spoken phrases."},
    {"Triplet Syllables", "VOCALS", "A syllable triplet and one broad rounded answer."},
    {"Echo Scratch", "VOCALS", "A delayed word repeats before a broad push-pull answer."},
    {"Breath Catch", "VOCALS", "A breathy tape catch, a rounded pull, and a little final chirp."},
    {"Call / Reply", "VOCALS", "A short call and a longer chopped reply over two bars."},
    {"Vocal Guillotine", "VOCALS", "Syncopated word cuts build into a rapid final scratch."},
    {"Concrete Swing", "BASS", "A lopsided bass tug answers a clipped three-hit pickup."},
    {"Quicksand", "BASS", "A spacious two-bar drag sinks into a slow tape catch."},
    {"Rattlesnake", "BASS", "A tight rattling fragment snaps into a gated reverse bite."},
    {"Bent Rebar", "BASS", "A broad rounded bend meets two hard-edged late scratches."},
    {"Sewer Step", "BASS", "Low offbeat pulls climb into a short reversed turnaround."},
    {"Elastic Jaw", "BASS", "An uneven pair of elastic scratches stretches the second answer."},
    {"Ghost Note Growl", "BASS", "Quiet spaces surround a triplet pickup and a long backward growl."},
    {"Quarterback", "BASS", "Two short calls lead to a wide, open two-bar pullback."},
    {"Broken Spring", "BASS", "A tape catch breaks into short, unequal sprung rebounds."},
    {"Triple Threat", "BASS", "Three broad triplet strokes close with a small clipped tail."},
    {"Gravity Drop", "BASS", "A repeated bass hit drops through a brake into a final reverse."},
    {"Ripcord", "BASS", "A sudden reverse tug releases into two compact forward throws."},
    {"Sludge Shuffle", "BASS", "Swung rounded pulls leave deliberate air around a heavier last note."},
    {"Aftershock", "BASS", "One broad call and a separated double reply frame a two-bar fill."},
    {"Split Tongue", "BASS", "Alternating smooth and gated strokes form a syncopated bass answer."},
    {"Low Rider", "BASS", "A laid-back long reverse cruises into a short rubbery rebound."},
    {"Crate Skid", "DRUMS", "A clipped offbeat skid opens into a wider snare-length backspin."},
    {"Ghost Snare", "DRUMS", "Tiny snare fragments lead to a late, rounded scratch accent."},
    {"Boomerang Break", "DRUMS", "A broad backward break returns through two unequal forward strokes."},
    {"Hi-Hat Hitch", "DRUMS", "Short high-hat grains stumble into a small gated pullback."},
    {"Amen Corner", "DRUMS", "A two-bar break stays open before a descending chopped turnaround."},
    {"Rimshot Relay", "DRUMS", "Separated rimshot-sized cuts pass into a triplet release."},
    {"Crab Walk", "DRUMS", "Short crossfader-like cuts alternate with a single longer backspin."},
    {"Snare Staircase", "DRUMS", "An increasing train of repeated fragments ends with a reverse snare."},
    {"Loose Change", "DRUMS", "Unequal offbeat chirps scatter around a roomy center beat."},
    {"Brake Check", "DRUMS", "A compact tape slowdown is answered by a fast, double-cut scratch."},
    {"Skip Proof", "DRUMS", "Two stuttering fragments and a rounded pull form a half-time fill."},
    {"Tripwire", "DRUMS", "Two displaced triplet groups cross into a clipped ending."},
    {"Sidechain Shuffle", "DRUMS", "The downbeats breathe while broad offbeat scratches pull against them."},
    {"Breakbeat Telegraph", "DRUMS", "Short and long cuts exchange a rhythmic call across two bars."},
    {"Pocket Lurch", "DRUMS", "A late reverse lean resolves through a small clean push-pull."},
    {"Double Clutch", "DRUMS", "Two different tape catches interrupt a running break."},
    {"Final Countdown", "DRUMS", "A final-beat fill accelerates from one broad cut to two tight chirps."},
    {"Syllable Slingshot", "VOCALS", "A clipped syllable repetition launches a long rounded word throw."},
    {"Tongue Twister", "VOCALS", "Unequal word reversals trade places with sharp little crossfader cuts."},
    {"Broken Sentence", "VOCALS", "A held-back word, an open pause, and a chopped closing phrase."},
    {"Mouthful", "VOCALS", "A dense early syllable cluster clears space for one broad reply."},
    {"Backchat", "VOCALS", "Two short backward words lead into a longer gated answer."},
    {"Question Mark", "VOCALS", "A slow questioning bend is answered by a crisp late word flip."},
    {"Name Drop", "VOCALS", "A repeated name-sized fragment falls into a descending tape catch."},
    {"Cut Up Poetry", "VOCALS", "Separated short word cuts develop into a two-bar rolling reply."},
    {"Rhyme Rewind", "VOCALS", "A long reverse rhyme snaps forward into two unequal baby scratches."},
    {"Breathless", "VOCALS", "Compact triplet syllables spill into a final gated word."},
    {"Word Elastic", "VOCALS", "A small rounded syllable and a long elastic answer share the phrase."},
    {"Hiccup Hook", "VOCALS", "Two isolated hiccups leave a hook intact before a quick backward catch."},
    {"Chopped Confession", "VOCALS", "A two-bar vocal call returns as repeated fragments and a soft reverse."},
    {"Telltale Stutter", "VOCALS", "Two unequal stutters introduce a rounded, stretched last syllable."},
    {"Reverse Whisper", "VOCALS", "A gentle tape bend leads to a long uncut backward word."},
    {"Cross Talk", "VOCALS", "A clipped call and smooth reply alternate over a swinging rhythm."},
    {"Mic Drop", "VOCALS", "An open phrase closes with a word repeat, tape stop, and final chirp."}
}};

// Every recipe is made once when its selection/seed changes. The audio callback
// evaluates this fixed array; it never builds a container or generates randomness.
class Recipe {
public:
    explicit Recipe(double beats, std::uint32_t seed, int version) noexcept
        : random(seed), varying(seed != 0), smoothMotion(version >= 2)
    {
        pattern.beats = beats;
    }

    void baby(double start, double length, double strength = 0.8,
              bool rounded = false, bool cut = false) noexcept
    {
        vary(start, length, strength);
        dryTo(start);
        // The longer forward stroke puts the source back at the live input
        // without an upward speed spike. Smoothstep's derivative peaks at 1.5.
        const double rise = length * 0.25;
        const double fall = length - rise;
        const bool curve = smoothMotion || rounded;
        const double depth = length * (smoothMotion || (rounded && !cut) ? 0.5 : 0.75) * strength;
        if (cut) {
            cutRamp(rise, 0.0, depth, false, smoothMotion);
            cutRamp(fall, depth, 0.0, true, smoothMotion);
        } else {
            section(rise, 0.0, depth, 1.0f, 1.0f, curve);
            section(fall, depth, 0.0, 1.0f, 1.0f, curve);
        }
    }

    void reverse(double start, double length, double strength = 0.75,
                 bool cut = false) noexcept
    {
        vary(start, length, strength);
        dryTo(start);
        // A readhead moving backward at roughly 0.5-1x keeps bass and speech
        // recognizable. The final jump returns to dry/live audio, with the
        // engine providing its short transition crossfade.
        const double depth = std::min(length * (1.35 + strength * 0.8),
                                      (start + length) * 0.99) / (smoothMotion ? 1.5 : 1.0);
        if (cut) cutRamp(length, 0.0, depth, true, smoothMotion);
        else section(length, 0.0, depth, 1.0f, 1.0f, smoothMotion);
    }

    void brake(double start, double length) noexcept
    {
        double strength = 1.0;
        vary(start, length, strength);
        dryTo(start);
        if (smoothMotion) {
            // One acceleration envelope, clipped only to articulate the final
            // gate: its speed moves continuously from +1 through zero to a
            // gentle reverse. Restarting an easing curve for each gate would
            // create a new pitch scoop at every cut.
            const double depth = length * (1.7 * (0.9 + 0.1 * strength) / 0.75);
            section(length * 0.75, 0.0, depth, 1.0f, 1.0f, true, 1.0f, 0.0, 0.375);
            section(length * 0.25, 0.0, depth, 1.0f, 0.25f, true, 1.0f, 0.375, 0.5);
            return;
        }
        const double step = length * 0.25;
        double delay = 0.0;
        // A descending sequence of actual playback speeds, crossing through
        // zero into a soft reversal. This is a time bend, not a pitch shifter.
        for (const double slope : {0.2, 0.6, 1.1, 1.7}) {
            const double next = delay + step * slope * (0.9 + 0.1 * strength);
            section(step, delay, next, 1.0f, slope > 1.5 ? 0.25f : 1.0f);
            delay = next;
        }
    }

    void repeat(double start, double grain, int count, bool cut = false) noexcept
    {
        dryTo(start);
        const double firstDelay = grain * (varying && (next() & 1u) ? 2.0 : 1.0);
        // Each segment re-reads the same past grain at its original pitch.
        // The final segment can be gated for a crossfader-like syllable cut.
        for (int index = 0; index < count; ++index) {
            const double delay = firstDelay + grain * index;
            if (cut && (index & 1)) {
                section(grain * 0.75, delay, delay);
                section(grain * 0.25, delay, delay, 0.0f, 0.0f);
            } else {
                section(grain, delay, delay);
            }
        }
    }

    ScratchPattern finish() noexcept
    {
        dryTo(pattern.beats);
        return pattern;
    }

private:
    ScratchPattern pattern;
    double cursor = 0.0;
    std::uint32_t random;
    bool varying, smoothMotion;

    std::uint32_t next() noexcept
    {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        return random;
    }

    void vary(double& start, double& length, double& strength) noexcept
    {
        if (!varying) return;
        const auto value = next();
        strength *= 0.78 + 0.22 * static_cast<double>(value & 65535u) / 65535.0;
        // A seed may move a stroke one tiny grid step late, never off its
        // existing end or into its neighbour. This preserves the recipe.
        if (value & 65536u) {
            const double nudge = std::min(1.0 / 32.0, length * 0.125);
            start += nudge;
            length -= nudge;
        }
    }

    void section(double duration, double from, double to,
                 float gateFrom = 1.0f, float gateTo = 1.0f,
                 bool curved = false, float wet = 1.0f,
                 double curveStart = 0.0, double curveEnd = 1.0) noexcept
    {
        if (duration <= 1.0e-12 || pattern.count >= ScratchPattern::maximumSegments) return;
        const double end = std::min(pattern.beats, cursor + duration);
        if (end <= cursor) return;
        pattern.segments[static_cast<std::size_t>(pattern.count++)] =
            {cursor, end, from, to, gateFrom, gateTo, wet, curved, curveStart, curveEnd};
        cursor = end;
    }

    void dryTo(double beat) noexcept
    {
        if (beat > cursor + 1.0e-12)
            section(beat - cursor, 0.0, 0.0, 1.0f, 1.0f, false, 0.0f);
    }

    void cutRamp(double length, double from, double to, bool doubleCut, bool curved = false) noexcept
    {
        if (!doubleCut) {
            if (curved) {
                section(length * 0.75, from, to, 1.0f, 1.0f, true, 1.0f, 0.0, 0.75);
                section(length * 0.25, from, to, 0.0f, 0.0f, true, 1.0f, 0.75, 1.0);
                return;
            }
            const double middle = from + (to - from) * 0.75;
            section(length * 0.75, from, middle);
            section(length * 0.25, middle, to, 0.0f, 0.0f);
            return;
        }
        constexpr std::array<double, 5> cuts {0.0, 0.375, 0.5, 0.875, 1.0};
        for (int part = 0; part < 4; ++part) {
            const double left = cuts[static_cast<std::size_t>(part)];
            const double right = cuts[static_cast<std::size_t>(part + 1)];
            const float gain = (part & 1) ? 0.0f : 1.0f;
            if (curved)
                section(length * (right - left), from, to, gain, gain, true, 1.0f, left, right);
            else
                section(length * (right - left), from + (to - from) * left,
                        from + (to - from) * right, gain, gain);
        }
    }
};

double safePhraseLength(double beats) noexcept
{
    return std::isfinite(beats) && beats > 0.0 ? std::clamp(beats, 0.25, 256.0) : 4.0;
}

// The composer chooses a rhythm and a related call/reply vocabulary, then
// writes fresh strokes in every bar. It does not select or mutate a preset.
class Composer {
public:
    Composer(std::uint32_t seed, double beats, double beatsPerBar, int version) noexcept
        : random(seed), smoothMotion(version >= 2)
    {
        pattern.beats = safePhraseLength(beats);
        barBeats = std::isfinite(beatsPerBar) && beatsPerBar > 0.0 ? std::clamp(beatsPerBar, 0.25, 16.0) : 4.0;
        barBeats = std::min(barBeats, pattern.beats);
        // At most sixteen compositional bars keep storage and construction
        // bounded even if a caller supplies mismatched meter/phrase values.
        barBeats = std::max(barBeats, pattern.beats / 16.0);
    }

    ScratchPattern compose() noexcept
    {
        const int call = choose(7);
        const int reply = (call + 1 + choose(5)) % 7;
        const bool rounded = choose(3) != 0;
        const bool syncopated = choose(2) != 0;
        const int bars = static_cast<int>(std::ceil(pattern.beats / barBeats));
        const double grid = barBeats * 0.25;
        bool first = true;
        for (int bar = 0; bar < bars; ++bar) {
            const double origin = barBeats * bar;
            const bool turnaround = (bar + 1) % 4 == 0 || bar == bars - 1;
            for (int half = 0; half < 2; ++half) {
                if (half == 0 && !first && choose(5) == 0) continue;
                const double offset = half == 0 ? 0.5 : (syncopated ? 0.25 : 0.5);
                const double start = origin + (half * 2.0 + offset + choose(3) * 0.25) * grid;
                const double last = std::min(origin + (half * 2.0 + 1.875) * grid, pattern.beats - 0.125 * grid);
                if (last - start < 0.25 * grid) continue;
                const double length = std::min(last - start, (0.5 + choose(4) * 0.25) * grid);
                const bool triplet = half == 1 && turnaround && length >= grid && choose(3) == 0;
                const int hits = triplet ? 3 : (length >= grid && choose(2) == 0 ? 2 : 1);
                const double spacing = length / hits;
                for (int hit = 0; hit < hits; ++hit) {
                    // Short gaps form crossfader-like articulation without
                    // stretching a two-beat gesture across a sixteen-bar loop.
                    const double rest = hits > 1 && !triplet ? 0.0625 * grid : 0.0;
                    const int motif = half == 0 ? call : reply;
                    const int kind = first ? 0 : (choose(4) == 0 ? choose(7)
                                              : (motif + bar % 2 + hit) % 7);
                    gesture(start + hit * spacing, spacing - rest, kind,
                            rounded && choose(4) != 0, choose(4) == 0);
                    first = false;
                }
            }
        }
        dryTo(pattern.beats);
        return pattern;
    }

private:
    ScratchPattern pattern;
    std::uint32_t random;
    bool smoothMotion;
    double cursor = 0.0;
    double barBeats = 4.0;

    std::uint32_t next() noexcept
    {
        // A counter mixer accepts every seed, including zero, without a
        // degenerate random stream. All arithmetic is deterministic uint32.
        std::uint32_t value = (random += 0x9e3779b9u);
        value = (value ^ (value >> 16)) * 0x21f0aaadu;
        value = (value ^ (value >> 15)) * 0x735a2d97u;
        return value ^ (value >> 15);
    }

    int choose(int count) noexcept { return static_cast<int>(next() % static_cast<std::uint32_t>(count)); }
    double unit() noexcept { return static_cast<double>(next() >> 8) / 16777216.0; }

    void section(double duration, double from, double to, bool curved = false,
                 float gateFrom = 1.0f, float gateTo = 1.0f, float wet = 1.0f,
                 double curveStart = 0.0, double curveEnd = 1.0) noexcept
    {
        // Reserve one slot for a complete dry tail even if the grammar grows.
        if (duration <= 1.0e-12 || pattern.count >= ScratchPattern::maximumSegments - 1) return;
        const double end = std::min(cursor + duration, pattern.beats);
        if (end <= cursor) return;
        pattern.segments[static_cast<std::size_t>(pattern.count++)] =
            {cursor, end, from, to, gateFrom, gateTo, wet, curved, curveStart, curveEnd};
        cursor = end;
    }

    void dryTo(double beat) noexcept
    {
        const double end = std::min(beat, pattern.beats);
        if (end <= cursor + 1.0e-12) return;
        if (pattern.count > 0) {
            auto& previous = pattern.segments[static_cast<std::size_t>(pattern.count - 1)];
            if (previous.wet == 0.0f) {
                previous.end = end;
                cursor = end;
                return;
            }
        }
        if (pattern.count >= ScratchPattern::maximumSegments) return;
        pattern.segments[static_cast<std::size_t>(pattern.count++)] =
            {cursor, end, 0.0, 0.0, 1.0f, 1.0f, 0.0f, false};
        cursor = end;
    }

    void ramp(double duration, double from, double to, bool rounded, bool cut) noexcept
    {
        if (!cut) {
            section(duration, from, to, rounded);
            return;
        }
        // Cuts subdivide a straight trajectory without restarting a curved
        // easing function or introducing an unreported read-speed spike.
        const double fraction = choose(2) == 0 ? 0.625 : 0.75;
        if (smoothMotion && rounded) {
            section(duration * fraction, from, to, true, 1.0f, 1.0f, 1.0f, 0.0, fraction);
            section(duration * (1.0 - fraction), from, to, true, 0.0f, 0.0f, 1.0f, fraction, 1.0);
            return;
        }
        const double middle = from + (to - from) * fraction;
        section(duration * fraction, from, middle);
        section(duration * (1.0 - fraction), middle, to, false, 0.0f, 0.0f);
    }

    void baby(double length, double history, bool rounded, bool cut) noexcept
    {
        const double rise = length * (0.22 + unit() * 0.06);
        const double fall = length - rise;
        const bool curve = smoothMotion || (rounded && !cut);
        const double derivativePeak = curve ? 1.5 : 1.0;
        const double backwards = 0.45 + unit() * 0.9;
        // Choose delay depth from the actual derivative bound. A curving
        // forward return never exceeds 2x, and reverse stays below that too.
        const double depth = std::min({rise * (1.0 + backwards) / derivativePeak,
                                      fall / derivativePeak * 0.97, history, 3.0});
        ramp(rise, 0.0, depth, curve, cut);
        ramp(fall, depth, 0.0, curve, cut);
    }

    void gesture(double start, double length, int kind, bool rounded, bool cut) noexcept
    {
        dryTo(start);
        // The deepest point uses history already available at this gesture's
        // start. That also guarantees a causal first pass for curved strokes.
        const double history = std::min(start, 3.0);
        switch (kind) {
            case 0: // Broad push/pull.
                baby(length, history, rounded, cut);
                break;
            case 1: { // A backwards syllable, then a live release.
                const double depth = std::min(history, length * (1.3 + unit() * 0.75))
                                     / (smoothMotion ? 1.5 : 1.0);
                ramp(length, 0.0, depth, smoothMotion, cut);
                break;
            }
            case 2: { // Descending tape bend with a brief backward finish.
                const double step = length * 0.25;
                const double scale = 0.8 + unit() * 0.2;
                if (smoothMotion) {
                    const double depth = std::min({history * 2.0, 3.0, length * (1.8 * scale / 0.75)});
                    section(length, 0.0, depth, true, 1.0f, 1.0f, 1.0f, 0.0, 0.5);
                    break;
                }
                double delay = 0.0;
                for (const double slope : {0.15, 0.55, 1.15, 1.8}) {
                    const double nextDelay = std::min(history, delay + step * slope * scale);
                    section(step, delay, nextDelay);
                    delay = nextDelay;
                }
                break;
            }
            case 3: { // Unequal baby scratches make a call inside the reply.
                const double first = length * (choose(2) == 0 ? 0.375 : 0.625);
                baby(first, history, rounded, false);
                baby(length - first, history, rounded, cut);
                break;
            }
            case 4: { // Hold a fragment, then throw it forward without a pitch spike.
                const double hold = length * (0.25 + unit() * 0.10);
                const double delay = std::min(history, hold * (1.2 + unit() * 0.5))
                                     / (smoothMotion ? 1.5 : 1.0);
                section(hold, 0.0, delay, smoothMotion);
                ramp(length - hold, delay, 0.0, smoothMotion, cut);
                break;
            }
            case 5: { // Two repeats at original pitch, then a backwards response.
                const double grain = length / 3.0;
                const double initialDelay = std::min(history, grain * (0.5 + unit() * 0.5));
                section(grain, initialDelay, initialDelay);
                const double secondDelay = std::min(history, initialDelay + grain);
                section(grain, secondDelay, secondDelay, false, cut ? 0.0f : 1.0f, 1.0f);
                const double depth = std::min(history, grain * (1.4 + unit() * 0.4))
                                     / (smoothMotion ? 1.5 : 1.0);
                ramp(grain, 0.0, depth, smoothMotion, cut);
                break;
            }
            default: { // Forward pickup followed by a rounded backward answer.
                const double first = length * 0.3;
                const double depth = std::min(history, first * (0.5 + unit() * 0.4))
                                     / (smoothMotion ? 1.5 : 1.0);
                section(first, depth, 0.0, smoothMotion);
                baby(length - first, history, rounded, cut);
                break;
            }
        }
    }
};

} // namespace

const std::array<FactoryPreset, factoryPresetCount>& factoryPresets() noexcept
{
    return presets;
}

ScratchPattern makePattern(int factoryIndex, std::uint32_t variationSeed, int motionVersion) noexcept
{
    const int index = std::clamp(factoryIndex, 0, factoryPresetCount - 1);
    const double length = (index == 8 || index == 16 || index == 23 || index == 26
                           || index == 32 || index == 38 || index == 45 || index == 54
                           || index == 65 || index == 70) ? 8.0
                        : (index == 0 || index == 1 || index == 5 || index == 9
                           || index == 11 || index == 13 || index == 17
                           || index == 18 || index == 19 || index == 27 || index == 30
                           || index == 36 || index == 44 || index == 47 || index == 58
                           || index == 62 || index == 69) ? 2.0 : 4.0;
    Recipe recipe(length, variationSeed, motionVersion);
    switch (index) {
        case 0:
            recipe.baby(0.5, 0.5, 0.78);
            recipe.baby(1.5, 0.5, 0.7);
            break;
        case 1:
            recipe.baby(0.5, 0.25, 0.82, true);
            recipe.baby(0.875, 0.375, 0.9, true);
            recipe.baby(1.5, 0.5, 0.86, true);
            break;
        case 2:
            recipe.reverse(2.0, 1.0, 0.4);
            recipe.baby(3.25, 0.5, 0.74, true);
            break;
        case 3:
            for (int i = 0; i < 3; ++i)
                recipe.baby(1.0 + i * (2.0 / 3.0), 0.5, 0.76 + i * 0.05);
            recipe.baby(3.25, 0.75, 0.84, true);
            break;
        case 4:
            recipe.brake(2.5, 1.0);
            recipe.reverse(3.5, 0.25, 0.9, true);
            break;
        case 5:
            recipe.baby(0.5, 0.5, 0.85, false, true);
            recipe.baby(1.25, 0.75, 0.72, false, true);
            break;
        case 6:
            recipe.reverse(1.0, 0.75, 0.68);
            recipe.baby(2.0, 0.75, 0.9, true);
            recipe.baby(3.0, 1.0, 0.8, true);
            break;
        case 7:
            recipe.baby(0.75, 0.25, 0.7);
            recipe.baby(1.5, 0.25, 0.82, false, true);
            recipe.baby(2.75, 0.25, 0.78);
            recipe.baby(3.5, 0.375, 0.86, true);
            break;
        case 8:
            recipe.baby(3.0, 0.5, 0.75, true);
            recipe.reverse(3.5, 0.5, 0.65);
            for (int i = 0; i < 3; ++i)
                recipe.baby(6.0 + i * (2.0 / 3.0), 2.0 / 3.0, 0.9 - i * 0.08, false, true);
            break;
        case 9:
            recipe.baby(0.75, 0.25, 0.68);
            recipe.baby(1.5, 0.5, 0.8, true);
            break;
        case 10:
            recipe.reverse(1.0, 0.25, 0.72);
            recipe.baby(2.75, 0.25, 0.8);
            recipe.reverse(3.0, 0.5, 0.6);
            break;
        case 11:
            recipe.baby(0.5, 0.5, 0.75, false, true);
            recipe.baby(1.5, 0.25, 0.68, false, true);
            break;
        case 12:
            recipe.baby(1.5, 0.5, 0.68, true);
            recipe.reverse(2.5, 0.5, 0.65);
            recipe.baby(3.0, 0.5, 0.82);
            recipe.baby(3.5, 0.5, 0.88, false, true);
            break;
        case 13:
            for (int i = 0; i < 3; ++i)
                recipe.baby(1.0 + i / 3.0, 1.0 / 3.0, 0.72 + i * 0.06, false, true);
            break;
        case 14:
            recipe.baby(3.0, 0.5, 0.8, false, true);
            recipe.baby(3.5, 0.5, 0.92, false, true);
            break;
        case 15:
            recipe.repeat(2.0, 0.25, 3, true);
            recipe.reverse(2.75, 0.5, 0.75);
            recipe.baby(3.5, 0.5, 0.82);
            break;
        case 16:
            recipe.baby(3.5, 0.5, 0.7, false, true);
            recipe.repeat(6.0, 0.125, 4, true);
            recipe.baby(6.5, 0.5, 0.78);
            recipe.reverse(7.0, 0.75, 0.65);
            break;
        case 17:
            recipe.reverse(0.5, 0.5, 0.65);
            recipe.baby(1.5, 0.5, 0.75, true);
            break;
        case 18:
            recipe.repeat(1.0, 0.125, 3, true);
            recipe.baby(1.5, 0.5, 0.82, false, true);
            break;
        case 19:
            recipe.baby(0.5, 0.5, 0.78, true);
            recipe.baby(1.25, 0.75, 0.68, true);
            break;
        case 20:
            for (int i = 0; i < 3; ++i)
                recipe.baby(1.0 + i / 3.0, 1.0 / 3.0, 0.65 + i * 0.07);
            recipe.baby(3.0, 0.75, 0.72, true);
            break;
        case 21:
            recipe.repeat(1.5, 0.5, 2);
            recipe.baby(3.0, 0.75, 0.84);
            break;
        case 22:
            recipe.brake(1.5, 0.5);
            recipe.baby(2.5, 0.5, 0.66, true);
            recipe.baby(3.5, 0.25, 0.7, false, true);
            break;
        case 23:
            recipe.baby(1.0, 0.5, 0.76, true);
            recipe.reverse(4.0, 0.75, 0.6);
            recipe.repeat(5.0, 0.25, 3, true);
            recipe.baby(6.5, 1.0, 0.86, true);
            break;
        case 24:
            recipe.baby(0.75, 0.25, 0.8, false, true);
            recipe.baby(1.5, 0.5, 0.72, false, true);
            recipe.reverse(2.75, 0.5, 0.8, true);
            recipe.baby(3.5, 0.5, 0.9, false, true);
            break;
        case 25: // Concrete Swing.
            recipe.repeat(0.75, 0.125, 3, true);
            recipe.baby(1.5, 0.75, 0.84, false, true);
            recipe.baby(2.75, 1.0, 0.72, true);
            break;
        case 26: // Quicksand.
            recipe.reverse(2.0, 1.25, 0.5);
            recipe.baby(4.5, 0.75, 0.7, true);
            recipe.brake(6.0, 1.5);
            break;
        case 27: // Rattlesnake.
            recipe.repeat(0.5, 0.125, 4, true);
            recipe.reverse(1.25, 0.5, 0.8, true);
            break;
        case 28: // Bent Rebar.
            recipe.baby(0.75, 1.25, 0.82, true);
            recipe.baby(2.5, 0.5, 0.7, false, true);
            recipe.baby(3.25, 0.625, 0.9);
            break;
        case 29: // Sewer Step.
            recipe.baby(0.5, 0.375, 0.74, true);
            recipe.baby(1.25, 0.375, 0.82);
            recipe.baby(2.0, 0.75, 0.9, true);
            recipe.reverse(3.25, 0.5, 0.65, true);
            break;
        case 30: // Elastic Jaw.
            recipe.baby(0.375, 0.375, 0.7, true);
            recipe.baby(1.0, 0.875, 0.88, true);
            break;
        case 31: // Ghost Note Growl.
            for (int hit = 0; hit < 3; ++hit)
                recipe.baby(0.75 + hit / 3.0, 0.25, 0.6 + hit * 0.09);
            recipe.reverse(2.5, 1.25, 0.55);
            break;
        case 32: // Quarterback.
            recipe.baby(1.5, 0.5, 0.75);
            recipe.baby(3.25, 0.625, 0.85, false, true);
            recipe.reverse(5.0, 1.5, 0.65);
            recipe.baby(7.0, 0.75, 0.68, true);
            break;
        case 33: // Broken Spring.
            recipe.brake(0.75, 0.75);
            recipe.baby(1.75, 0.375, 0.9);
            recipe.baby(2.5, 0.5, 0.76, true);
            recipe.baby(3.25, 0.625, 0.84, false, true);
            break;
        case 34: // Triple Threat.
            for (int hit = 0; hit < 3; ++hit)
                recipe.baby(0.75 + hit * 0.875, 0.75, 0.9 - hit * 0.09, true);
            recipe.reverse(3.5, 0.25, 0.7, true);
            break;
        case 35: // Gravity Drop.
            recipe.repeat(0.75, 0.375, 2);
            recipe.brake(2.0, 1.0);
            recipe.reverse(3.25, 0.625, 0.82);
            break;
        case 36: // Ripcord.
            recipe.reverse(0.5, 0.5, 0.84);
            recipe.baby(1.125, 0.375, 0.82, true);
            recipe.baby(1.625, 0.375, 0.7);
            break;
        case 37: // Sludge Shuffle.
            recipe.baby(0.625, 0.375, 0.7, true);
            recipe.baby(1.625, 0.625, 0.78, true);
            recipe.reverse(3.0, 0.875, 0.56);
            break;
        case 38: // Aftershock.
            recipe.baby(1.5, 1.5, 0.86, true);
            recipe.reverse(4.5, 0.75, 0.7);
            recipe.baby(6.0, 0.625, 0.82, false, true);
            recipe.baby(7.0, 0.875, 0.72, true);
            break;
        case 39: // Split Tongue.
            recipe.baby(0.5, 0.5, 0.75, true);
            recipe.baby(1.25, 0.5, 0.9, false, true);
            recipe.baby(2.0, 0.75, 0.8, true);
            recipe.baby(3.25, 0.5, 0.68, false, true);
            break;
        case 40: // Low Rider.
            recipe.reverse(1.0, 1.5, 0.52);
            recipe.baby(3.0, 0.875, 0.76, true);
            break;
        case 41: // Crate Skid.
            recipe.baby(0.75, 0.375, 0.84, false, true);
            recipe.reverse(2.0, 0.75, 0.6);
            recipe.baby(3.375, 0.5, 0.8);
            break;
        case 42: // Ghost Snare.
            recipe.repeat(1.0, 0.125, 2, true);
            recipe.repeat(2.0, 0.125, 3, true);
            recipe.baby(3.0, 0.75, 0.68, true);
            break;
        case 43: // Boomerang Break.
            recipe.reverse(1.0, 1.0, 0.72);
            recipe.baby(2.375, 0.625, 0.76, true);
            recipe.baby(3.125, 0.875, 0.88);
            break;
        case 44: // Hi-Hat Hitch.
            recipe.repeat(0.5, 0.0625, 4, true);
            recipe.repeat(1.0, 0.125, 2);
            recipe.baby(1.5, 0.375, 0.76, false, true);
            break;
        case 45: // Amen Corner.
            recipe.reverse(3.0, 0.5, 0.55);
            recipe.baby(5.0, 1.0, 0.84, false, true);
            recipe.baby(6.25, 0.75, 0.76, false, true);
            recipe.baby(7.25, 0.5, 0.7);
            break;
        case 46: // Rimshot Relay.
            recipe.baby(0.75, 0.25, 0.7, false, true);
            recipe.baby(1.75, 0.375, 0.86, false, true);
            for (int hit = 0; hit < 3; ++hit)
                recipe.baby(2.75 + hit / 3.0, 0.25, 0.82 - hit * 0.06);
            break;
        case 47: // Crab Walk.
            recipe.baby(0.375, 0.25, 0.88, false, true);
            recipe.baby(0.75, 0.25, 0.72, false, true);
            recipe.reverse(1.25, 0.625, 0.7);
            break;
        case 48: // Snare Staircase.
            recipe.repeat(1.0, 0.125, 2);
            recipe.repeat(1.75, 0.125, 3, true);
            recipe.repeat(2.5, 0.125, 4, true);
            recipe.reverse(3.25, 0.625, 0.65);
            break;
        case 49: // Loose Change.
            recipe.baby(0.625, 0.25, 0.75, false, true);
            recipe.baby(1.25, 0.375, 0.68, true);
            recipe.reverse(2.625, 0.375, 0.7, true);
            recipe.baby(3.375, 0.5, 0.86);
            break;
        case 50: // Brake Check.
            recipe.brake(1.0, 0.625);
            recipe.baby(2.5, 0.75, 0.9, false, true);
            recipe.reverse(3.5, 0.375, 0.56);
            break;
        case 51: // Skip Proof.
            recipe.repeat(0.75, 0.25, 2, true);
            recipe.repeat(2.0, 0.1875, 3, true);
            recipe.baby(3.0, 0.875, 0.78, true);
            break;
        case 52: // Tripwire.
            for (int hit = 0; hit < 3; ++hit)
                recipe.baby(0.5 + hit / 3.0, 0.25, 0.66 + hit * 0.06);
            for (int hit = 0; hit < 3; ++hit)
                recipe.baby(2.5 + hit / 3.0, 0.25, 0.88 - hit * 0.07, false, true);
            break;
        case 53: // Sidechain Shuffle.
            recipe.baby(0.5, 0.5, 0.78, true);
            recipe.baby(1.5, 0.5, 0.68, true);
            recipe.reverse(2.5, 0.375, 0.6);
            recipe.baby(3.5, 0.5, 0.86, false, true);
            break;
        case 54: // Breakbeat Telegraph.
            recipe.baby(1.0, 0.375, 0.76, false, true);
            recipe.reverse(2.5, 0.75, 0.68);
            recipe.repeat(4.5, 0.25, 2, true);
            recipe.baby(5.75, 0.5, 0.82);
            recipe.reverse(6.75, 1.0, 0.55, true);
            break;
        case 55: // Pocket Lurch.
            recipe.reverse(1.25, 0.875, 0.5);
            recipe.baby(2.75, 0.625, 0.84, true);
            recipe.baby(3.5, 0.25, 0.68, false, true);
            break;
        case 56: // Double Clutch.
            recipe.brake(0.75, 0.5);
            recipe.baby(1.5, 0.5, 0.76, false, true);
            recipe.brake(2.5, 1.0);
            recipe.reverse(3.625, 0.25, 0.65);
            break;
        case 57: // Final Countdown.
            recipe.baby(3.0, 0.5, 0.76, false, true);
            recipe.baby(3.5, 0.25, 0.9, false, true);
            recipe.baby(3.75, 0.25, 0.7, true);
            break;
        case 58: // Syllable Slingshot.
            recipe.repeat(0.5, 0.125, 3, true);
            recipe.baby(1.0, 0.875, 0.8, true);
            break;
        case 59: // Tongue Twister.
            recipe.reverse(0.5, 0.625, 0.56);
            recipe.baby(1.5, 0.25, 0.85, false, true);
            recipe.reverse(2.25, 0.875, 0.7);
            recipe.baby(3.5, 0.375, 0.72, false, true);
            break;
        case 60: // Broken Sentence.
            recipe.reverse(0.75, 0.75, 0.6);
            recipe.repeat(2.5, 0.25, 3, true);
            recipe.baby(3.5, 0.5, 0.82, true);
            break;
        case 61: // Mouthful.
            recipe.repeat(0.5, 0.125, 4, true);
            recipe.baby(1.25, 0.5, 0.72, false, true);
            recipe.baby(2.75, 1.125, 0.84, true);
            break;
        case 62: // Backchat.
            recipe.reverse(0.375, 0.25, 0.64);
            recipe.reverse(0.875, 0.25, 0.82);
            recipe.baby(1.375, 0.625, 0.78, false, true);
            break;
        case 63: // Question Mark.
            recipe.brake(1.0, 1.0);
            recipe.baby(2.5, 0.5, 0.68, true);
            recipe.reverse(3.25, 0.625, 0.86);
            break;
        case 64: // Name Drop.
            recipe.repeat(0.75, 0.375, 3);
            recipe.brake(2.5, 0.875);
            recipe.baby(3.625, 0.25, 0.8, false, true);
            break;
        case 65: // Cut Up Poetry.
            recipe.baby(1.0, 0.5, 0.68, false, true);
            recipe.baby(2.5, 0.75, 0.82, false, true);
            recipe.repeat(4.5, 0.25, 3, true);
            recipe.reverse(6.0, 0.75, 0.62);
            recipe.baby(7.0, 0.875, 0.86, true);
            break;
        case 66: // Rhyme Rewind.
            recipe.reverse(0.75, 1.0, 0.58);
            recipe.baby(2.25, 0.625, 0.86, true);
            recipe.baby(3.125, 0.875, 0.72);
            break;
        case 67: // Breathless.
            for (int hit = 0; hit < 3; ++hit)
                recipe.baby(1.0 + hit * 0.5, 0.375, 0.72 + hit * 0.07);
            recipe.reverse(3.0, 0.75, 0.68, true);
            break;
        case 68: // Word Elastic.
            recipe.baby(0.75, 0.5, 0.72, true);
            recipe.baby(2.0, 1.75, 0.82, true);
            break;
        case 69: // Hiccup Hook.
            recipe.repeat(0.5, 0.125, 2, true);
            recipe.repeat(1.0, 0.125, 2, true);
            recipe.reverse(1.5, 0.375, 0.6);
            break;
        case 70: // Chopped Confession.
            recipe.baby(1.5, 0.75, 0.78, true);
            recipe.repeat(4.0, 0.375, 3, true);
            recipe.reverse(6.0, 1.0, 0.45);
            recipe.baby(7.25, 0.5, 0.68, true);
            break;
        case 71: // Telltale Stutter.
            recipe.repeat(0.75, 0.125, 3, true);
            recipe.repeat(1.75, 0.25, 2);
            recipe.baby(2.75, 1.125, 0.76, true);
            break;
        case 72: // Reverse Whisper.
            recipe.brake(0.75, 0.75);
            recipe.reverse(2.25, 1.375, 0.46);
            recipe.baby(3.75, 0.25, 0.7, true);
            break;
        case 73: // Cross Talk.
            recipe.baby(0.625, 0.375, 0.84, false, true);
            recipe.baby(1.375, 0.875, 0.7, true);
            recipe.reverse(2.75, 0.5, 0.58, true);
            recipe.baby(3.5, 0.375, 0.82, true);
            break;
        case 74: // Mic Drop.
            recipe.repeat(2.0, 0.25, 2, true);
            recipe.brake(2.75, 0.75);
            recipe.baby(3.625, 0.375, 0.86, false, true);
            break;
        default: break;
    }
    return recipe.finish();
}

ScratchPattern generatePattern(std::uint32_t seed, double phraseBeats, double barBeats, int motionVersion) noexcept
{
    return Composer(seed, phraseBeats, barBeats, motionVersion).compose();
}

ScratchPattern fitPatternToPhrase(const ScratchPattern& source, double phraseBeats) noexcept
{
    ScratchPattern result;
    // A triplet can shorten the smallest host bar to 1/6 beat; a dotted
    // sixteen-bar phrase can reach 384 beats in the largest supported meter.
    // Composition remains bounded separately, then the same drawing retimes.
    result.beats = std::isfinite(phraseBeats) && phraseBeats > 0.0
                       ? std::clamp(phraseBeats, 1.0 / 6.0, 384.0) : 4.0;
    result.gridOffset = wrapPatternGridOffset(source.gridOffset);
    if (!std::isfinite(source.beats) || source.beats <= 0.0 || source.count <= 0
        || source.count > ScratchPattern::maximumSegments) {
        result.count = 1;
        result.segments[0] = {0.0, result.beats, 0.0, 0.0, 1.0f, 1.0f, 0.0f, false};
        return result;
    }

    // A duration knob changes the traversal time of the same drawing. Scaling
    // its horizontal and vertical coordinates by one factor keeps every source
    // speed, gate, curved stroke, and relative position exactly the same. The
    // host provides enough history for the resulting (possibly long) lookback.
    const double scale = result.beats / source.beats;
    result.editingRange = std::isfinite(source.editingRange) && source.editingRange > 0.0
                              ? source.editingRange * scale : 0.0;
    result.count = source.count;
    for (int index = 0; index < result.count; ++index) {
        const auto& original = source.segments[static_cast<std::size_t>(index)];
        auto& part = result.segments[static_cast<std::size_t>(index)];
        part = original;
        part.start *= scale;
        part.end *= scale;
        part.delayStart *= scale;
        part.delayEnd *= scale;
    }
    // Pin the final endpoint to avoid a floating-point gap at the loop edge.
    result.segments[static_cast<std::size_t>(result.count - 1)].end = result.beats;
    return result;
}

int wrapPatternGridOffset(int offset) noexcept
{
    const int wrapped = offset % patternGridDivisions;
    return wrapped < 0 ? wrapped + patternGridDivisions : wrapped;
}

double patternGridStepBeats(const ScratchPattern& pattern) noexcept
{
    return std::isfinite(pattern.beats) && pattern.beats > 0.0
               ? pattern.beats / patternGridDivisions : 0.0;
}

double patternOffsetBeats(const ScratchPattern& pattern) noexcept
{
    return patternGridStepBeats(pattern) * wrapPatternGridOffset(pattern.gridOffset);
}

namespace {
double phaseTolerance(double phraseBeats) noexcept
{
    return std::max(1.0e-12, phraseBeats * 1.0e-12);
}

double wrapPhaseBeats(double phraseBeats, double beat) noexcept
{
    double phase = std::fmod(beat, phraseBeats);
    if (phase < 0.0) phase += phraseBeats;
    const double tolerance = phaseTolerance(phraseBeats);
    return phase < tolerance || phase >= phraseBeats - tolerance ? 0.0 : phase;
}
}

double patternDisplayPhase(const ScratchPattern& pattern, double songBeat) noexcept
{
    if (!std::isfinite(songBeat) || !std::isfinite(pattern.beats) || pattern.beats <= 0.0)
        return 0.0;
    return wrapPhaseBeats(pattern.beats, songBeat) / pattern.beats;
}

double patternPhaseBeats(const ScratchPattern& pattern, double songBeat) noexcept
{
    if (!std::isfinite(songBeat) || !std::isfinite(pattern.beats) || pattern.beats <= 0.0)
        return 0.0;
    // Wrap before subtracting the small offset so distant host positions do
    // not lose extra precision. Avoid moving or splitting curved segments:
    // both sides of a wrapped curve retain their original parameterization.
    return wrapPhaseBeats(pattern.beats,
                         wrapPhaseBeats(pattern.beats, songBeat) - patternOffsetBeats(pattern));
}

PatternSample evaluatePatternSegment(const ScratchPattern& pattern, int segmentIndex, double sourceBeat) noexcept
{
    PatternSample sample;
    if (!std::isfinite(sourceBeat) || segmentIndex < 0 || segmentIndex >= pattern.count
        || segmentIndex >= ScratchPattern::maximumSegments) return sample;
    const auto& segment = pattern.segments[static_cast<std::size_t>(segmentIndex)];
    const double duration = segment.end - segment.start;
    if (!std::isfinite(duration) || duration <= 0.0) return sample;
    const double fraction = std::clamp((sourceBeat - segment.start) / duration, 0.0, 1.0);
    const double curveFraction = segment.curveStart + (segment.curveEnd - segment.curveStart) * fraction;
    const double shape = segment.curved ? curveFraction * curveFraction * (3.0 - 2.0 * curveFraction) : fraction;
    const double derivative = segment.curved
        ? 6.0 * curveFraction * (1.0 - curveFraction) * (segment.curveEnd - segment.curveStart) : 1.0;
    sample.delayBeats = segment.delayStart + (segment.delayEnd - segment.delayStart) * shape;
    sample.playbackRate = 1.0 - (segment.delayEnd - segment.delayStart) * derivative / duration;
    sample.gate = segment.gateStart + (segment.gateEnd - segment.gateStart) * static_cast<float>(fraction);
    sample.wet = segment.wet;
    sample.segment = segmentIndex;
    return sample;
}

PatternSample evaluatePattern(const ScratchPattern& pattern, double songBeat) noexcept
{
    PatternSample sample;
    if (!std::isfinite(songBeat) || !std::isfinite(pattern.beats)
        || pattern.beats <= 0.0 || pattern.count <= 0) return sample;
    const double phase = patternPhaseBeats(pattern, songBeat);
    const double tolerance = phaseTolerance(pattern.beats);
    const int count = std::min(pattern.count, ScratchPattern::maximumSegments);
    // A sixteen-bar generated phrase can contain hundreds of strokes. Binary
    // search keeps the per-sample cost independent of its current bar number.
    int low = 0, high = count;
    while (low < high) {
        const int middle = low + (high - low) / 2;
        // A host's block PPQ plus sample offset can differ from its absolute
        // sample clock by a few ulps. At a discontinuous gate/retrigger that
        // must not select opposite sides for one sample in different buffers.
        if (phase >= pattern.segments[static_cast<std::size_t>(middle)].end - tolerance) low = middle + 1;
        else high = middle;
    }
    if (low < count) {
        const int index = low;
        const auto& segment = pattern.segments[static_cast<std::size_t>(index)];
        if (phase < segment.start - tolerance || phase >= segment.end) return sample;
        const double source = std::abs(phase - segment.start) <= tolerance ? segment.start : phase;
        return evaluatePatternSegment(pattern, index, source);
    }
    return sample;
}

} // namespace backScratch
