#pragma once
#include <array>
#include <cstdint>
#include <string>

// The groove model shared by the audio thread, the screen and the tests.
// Nothing in this file allocates after construction, so the processor can rebuild envelopes in the callback.
namespace stutter {

constexpr int numSteps = 16;
constexpr float ghostLevel = 0.38f; // about -8.4 dB

enum class Step : std::uint8_t { off = 0, hit = 1, ghost = 2, tie = 3 };

struct Cell
{
    Step v = Step::off;
    bool slap = false;
    bool operator== (const Cell&) const = default;
};
using Pattern = std::array<Cell, numSteps>;

enum class Family : int { shuffle = 0, bounce, skank, drag, gallop, wonk, straight, count };
const char* familyName (Family);
Family familyFromName (const std::string&); // unknown names give shuffle

// Pattern text: X hit, g ghost, - tie, . off; slap text marks sends with '*'.
Pattern parsePattern (const char* cells, const char* slap);
std::string cellsToString (const Pattern&);
std::string slapToString (const Pattern&);

// Swung start of step i, in steps (i == 16 gives 16). Swing is 0.5 (straight) to 0.75.
inline double stepStart (int i, double swing)
{
    if (i >= numSteps) return numSteps;
    return (i >> 1) * 2.0 + ((i & 1) ? 2.0 * swing : 0.0);
}
inline double slotLength (int i, double swing) { return stepStart (i + 1, swing) - stepStart (i, swing); }
int stepAt (double position, double swing); // the step whose slot contains position (0..16)

// Piecewise envelope over one 16-step cycle, evaluated cyclically.
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
};
constexpr int numFactoryGrooves = 43;
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
