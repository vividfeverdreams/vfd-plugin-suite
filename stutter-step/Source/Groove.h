#pragma once
#include <array>
#include <cstdint>
#include <string>

// The groove model shared by the audio thread, the screen and the tests.
// Nothing in this file allocates after construction, so the processor can rebuild envelopes in the callback.
namespace stutter {

constexpr int numSteps = 16;
constexpr float ghostLevel = 0.38f; // about -8.4 dB
constexpr int fullSend = 20;        // slap send levels run 1..20, 5% to 100%; every send before 0.5.0 is full

enum class Step : std::uint8_t { off = 0, hit = 1, ghost = 2, tie = 3 };

struct Cell
{
    Step v = Step::off;
    bool slap = false;
    std::uint8_t send = fullSend;   // how much of the step reaches the slap; kept while the send is off
    bool operator== (const Cell&) const = default;
};
using Pattern = std::array<Cell, numSteps>;
inline float sendGain (const Cell& c) noexcept { return (float) c.send / (float) fullSend; }

// HATS (0.5.0) is only rolled when ROLL FROM picks it; ALL never does.
enum class Family : int { shuffle = 0, bounce, skank, drag, gallop, wonk, straight, hats, count };

// TIMING (0.5.0) scales RATE's step: straight, dotted (x 1.5) or triplet (x 2/3).
enum class Timing : int { straight = 0, dotted, triplet };
inline double timingFactor (int timing) noexcept { return timing == 1 ? 1.5 : timing == 2 ? 2.0 / 3.0 : 1.0; }
const char* familyName (Family);
Family familyFromName (const std::string&); // unknown names give shuffle

// Pattern text: X hit, g ghost, - tie, . off; slap text marks sends with '*'.
Pattern parsePattern (const char* cells, const char* slap);
std::string cellsToString (const Pattern&);
std::string slapToString (const Pattern&);
// Send levels as percentages, one per step ("100 100 45 ..."); empty when every send is full, as before 0.5.0.
std::string sendLevelsToString (const Pattern&);
void applySendLevels (Pattern&, const std::string&); // missing or unreadable entries stay full

// The pattern moved by steps (positive = later), wrapping: each cell keeps its gate, ghost, tie and send together.
Pattern rotated (const Pattern&, int steps);
// The cell whose slap send governs the sound at step i: the first cell of its event (a held tie belongs to the hit
// it continues, around the loop too). -1 where the step is silent.
int eventOwner (const Pattern&, int step);

// Swung start of step i, in steps (i == 16 gives 16). Swing is 0.5 (straight) to 0.75.
inline double stepStart (int i, double swing)
{
    if (i >= numSteps) return numSteps;
    return (i >> 1) * 2.0 + ((i & 1) ? 2.0 * swing : 0.0);
}
inline double slotLength (int i, double swing) { return stepStart (i + 1, swing) - stepStart (i, swing); }
int stepAt (double position, double swing); // the step whose slot contains position (0..16)

// Piecewise envelope over one 16-step cycle, evaluated cyclically. A tie on the first step continues a note that is
// still sounding on the last step, so a groove nudged around the loop keeps its held notes (0.5.0; the editor
// could never put a tie on the first step before, so older grooves are unchanged).
class Envelope
{
public:
    struct Point { double t; float v; };

    // gate: all events, closed level between them. slapOnly: only events whose first cell sends to the slap, closed 0.
    void build (const Pattern&, double swing, double length, double rampSteps, float closed, bool slapOnly);
    void setConstant (float v) noexcept { count = 0; constant = v; extCount = 0; }
    // Gaps envelope: 1 where the gate is closed, 0 where it is fully open.
    void buildInverseOf (const Envelope& gate, float closed);

    float eval (double position) const noexcept; // any position; wraps every 16 steps
    bool isConstant() const noexcept { return count == 0; }
    int size() const noexcept { return count; }
    const Point& point (int i) const noexcept { return pts[(size_t) i]; }

private:
    void extend();
    static constexpr int maxPoints = 4 * numSteps + 4;
    std::array<Point, maxPoints> pts {};
    std::array<Point, maxPoints * 3> ext {};
    int count = 0, extCount = 0;
    float constant = 0.0f;
};

// Factory grooves (hand-authored) and the generator behind NEW GROOVE.
struct FactoryGroove
{
    const char* folder;
    const char* name;
    Family family;
    const char* cells;
    const char* slap;
    float swing;     // 0.5 .. 0.75
    int rate;        // 0 = 1/4, 1 = 1/8, 2 = 1/16, 3 = 1/32
    float length;    // 0..1
    float smoothMs, depth;
    bool lock; float timeMs, feedback, tone, mix; bool gaps, wide;
    int timing = 0;  // 0 straight, 1 dotted, 2 triplet
};
constexpr int numFactoryGrooves043 = 43; // the 0.4.3 bank; SIMPLE and HATS follow it, so old indices still hold
constexpr int numFactoryGrooves = 75;
const FactoryGroove& factoryGroove (int index);
int factoryIndexOf (const std::string& name); // -1 if no factory groove has this name

struct Generated
{
    Pattern pattern;
    Family family = Family::shuffle;
    float swing = 0.6f, length = 0.6f;
    std::string name;
};
// familyFilter < 0 rolls from every family (weighted so most rolls swing). The rhythm is composed
// fresh: at least four steps away from every factory groove and from *avoid (the groove it replaces).
Generated generate (std::uint32_t seed, int familyFilter, const Pattern* avoid = nullptr);

} // namespace stutter
