#include "Groove.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

namespace stutter {

namespace {
constexpr const char* familyNames[] { "SHUFFLE", "BOUNCE", "SKANK", "DRAG", "GALLOP", "WONK", "STRAIGHT" };

// How each family feels. NEW GROOVE composes from the family's one-beat cells (one beat at 1/16,
// set in a phrase form such as A A A B or A B A C and lightly varied) or, more often for BOUNCE and
// WONK, from an accent grouping that runs across the beats (3+3+2, 3-5-3-5 ...). Cells: X hit,
// g ghost, - tie, . off.
struct FamilyRecipe
{
    int weight;
    float swingLow, swingHigh, length;
    float groupChance;     // chance of an accent grouping instead of beat cells
    float groupTie, groupGhost;
    std::array<const char*, 10> cells;
};

const std::array<FamilyRecipe, (size_t) Family::count> recipes { {
    { 24, 0.58f, 0.66f, 0.60f, 0.12f, 0.20f, 0.50f, { "X.gX", "X.X.", "X.Xg", "Xg.X", "X..X", ".gX.", "X-gX", "X.g.", "g.X.", "X..g" } },
    { 20, 0.55f, 0.62f, 0.55f, 0.45f, 0.15f, 0.40f, { "X..X", "..X.", ".X..", "X.X.", "X..g", "..Xg", "X.gX", ".gX.", "X...", "g..X" } },
    { 14, 0.56f, 0.64f, 0.42f, 0.10f, 0.05f, 0.30f, { "..X.", "..Xg", "g.X.", "..X-", ".gX.", "X.X.", "..XX", "g.Xg", "X.X-", "...X" } },
    { 14, 0.64f, 0.72f, 0.82f, 0.15f, 0.60f, 0.40f, { "X--.", "X-..", "X-.g", "X---", "..g.", "X.g.", "X--g", "X...", "g-..", "X-X-" } },
    { 12, 0.55f, 0.62f, 0.50f, 0.10f, 0.10f, 0.30f, { "XXX.", "XX.X", "X.XX", "XXg.", "XgX.", "X.X.", "XXXg", "gXX.", "X.Xg", "XX.." } },
    { 11, 0.66f, 0.75f, 0.62f, 0.85f, 0.25f, 0.45f, { "X..X", "g.X.", "X.g.", ".X.g", "X-.X", "..Xg", "Xg..", "X...", ".gX-", "X..g" } },
    {  5, 0.50f, 0.52f, 0.55f, 0.05f, 0.30f, 0.20f, { "X.X.", "X-X-", "XXXX", "X-.X", ".XXX", "X...", "X-X.", "XX.X", "X.XX", "X--X" } },
} };

// Names for rolled grooves. None of them is a factory name (generate() makes sure), so a rolled
// groove never reads as, or reopens as, a preset.
const std::array<std::array<const char*, 8>, (size_t) Family::count> adjectives { {
    { "Slick", "Lazy", "Greasy", "Silk", "Loose", "Swagger", "Easy", "Satin" },
    { "Springy", "Hopping", "Pogo", "Elastic", "Bubble", "Trampoline", "Kangaroo", "Coil" },
    { "Dub", "Choppy", "Dusty", "Hollow", "Yard", "Basement", "Bass Bin", "Skanky" },
    { "Heavy", "Sleepy", "Molasses", "Swamp", "Sludge", "Lead", "Groggy", "Tired" },
    { "Rodeo", "Stampede", "Clip", "Bronco", "Derby", "Saddle", "Spur", "Hoof" },
    { "Drunk", "Wobbly", "Bent", "Warped", "Squiffy", "Off-Kilter", "Sideways", "Cockeyed" },
    { "Clean", "Square", "Steel", "Even", "Laser", "Quartz", "Chrome", "Metro" },
} };
const std::array<std::array<const char*, 6>, (size_t) Family::count> nouns { {
    { "Shuffle", "Strut", "Saunter", "Sway", "Glide", "Stroll" },
    { "Bounce", "Hop", "Spring", "Skip", "Rebound", "Boing" },
    { "Skank", "Chop", "Upstroke", "Step", "Riddim", "Lick" },
    { "Drag", "Limp", "Crawl", "Trudge", "Slump", "Lurch" },
    { "Gallop", "Canter", "Trot", "Stomp", "Charge", "Dash" },
    { "Wonk", "Stumble", "Lurch", "Wobble", "Stagger", "Tumble" },
    { "Pump", "Chop", "Pulse", "Grid", "March", "Tick" },
} };

// Folders in browsing order. Built for 140 halftime, riddim and left-field bass; see PRESETS.md.
const std::array<FactoryGroove, numFactoryGrooves> factory { {
    { "HALFTIME", "Lean Back", Family::drag, "X-..g.X...X-.gX.", "......*.......*.", 0.68f, 2, 0.8f, 8.0f, 1.0f, true, 120.0f, 0.3f, 0.35f, 0.42f, true, true },
    { "HALFTIME", "Tar Pit", Family::drag, "X--.X.g.X--..gX.", "....*.........*.", 0.7f, 1, 0.85f, 14.0f, 0.95f, true, 140.0f, 0.32f, 0.3f, 0.38f, true, true },
    { "HALFTIME", "Syrup Drag", Family::drag, "X--.g.X-..gX-.g.", "......*.........", 0.68f, 2, 0.85f, 12.0f, 0.92f, false, 140.0f, 0.28f, 0.32f, 0.36f, false, true },
    { "HALFTIME", "Sub Stomp", Family::bounce, "X..X..X...X..X..", "......*......*..", 0.54f, 2, 0.5f, 3.0f, 1.0f, false, 70.0f, 0.15f, 0.5f, 0.3f, false, true },
    { "HALFTIME", "Wide Load", Family::shuffle, "X-.X-.X-..X-.X-.", "...*.........*..", 0.64f, 2, 0.75f, 6.0f, 1.0f, true, 90.0f, 0.25f, 0.45f, 0.45f, false, true },
    { "HALFTIME", "Slow Motion", Family::drag, "X.g.X-.gX.g.X--.", "........*.......", 0.62f, 1, 0.7f, 10.0f, 0.9f, true, 90.0f, 0.4f, 0.28f, 0.42f, true, true },
    { "RIDDIM", "Riddim Rattle", Family::bounce, "X..X..X..X..X.X.", ".........*....*.", 0.58f, 2, 0.45f, 2.5f, 1.0f, true, 90.0f, 0.2f, 0.55f, 0.32f, false, false },
    { "RIDDIM", "Chainsaw Lean", Family::gallop, "XXg.XXg.XXg.XgXg", ".........*....*.", 0.62f, 2, 0.42f, 2.0f, 1.0f, true, 90.0f, 0.18f, 0.6f, 0.3f, false, true },
    { "RIDDIM", "Tank Tread", Family::gallop, "XXX.XXX.XXX.XX.X", "..*.......*.....", 0.55f, 2, 0.4f, 2.0f, 1.0f, false, 60.0f, 0.1f, 0.5f, 0.28f, false, false },
    { "RIDDIM", "Head Nod", Family::bounce, "X..X.XX..X..X.X.", ".....*........*.", 0.6f, 2, 0.5f, 3.0f, 1.0f, true, 90.0f, 0.25f, 0.45f, 0.38f, false, true },
    { "RIDDIM", "Robot Shuffle", Family::shuffle, "X.XgX.XgX.XgX.Xg", "......*.......*.", 0.64f, 2, 0.45f, 2.0f, 1.0f, true, 90.0f, 0.2f, 0.55f, 0.3f, true, true },
    { "RIDDIM", "Clamp Down", Family::bounce, "XX.XX.XX.XX.XX.X", ".......*.......*", 0.57f, 2, 0.38f, 1.5f, 1.0f, true, 90.0f, 0.15f, 0.62f, 0.28f, true, false },
    { "TRIPLET", "Triplet Wob", Family::shuffle, "XgXgXgXgXgXgXgXg", "......*.......*.", 0.665f, 2, 0.55f, 4.0f, 0.95f, true, 90.0f, 0.25f, 0.5f, 0.3f, false, true },
    { "TRIPLET", "Yoyo", Family::shuffle, "XgXgX.X.XgXgX.X.", "............*...", 0.665f, 3, 0.6f, 2.0f, 1.0f, true, 90.0f, 0.3f, 0.6f, 0.3f, false, true },
    { "TRIPLET", "Rubber Band", Family::drag, "X-gX-gX-gX-gX.Xg", "............*...", 0.665f, 2, 0.7f, 6.0f, 1.0f, true, 90.0f, 0.3f, 0.4f, 0.4f, true, true },
    { "TRIPLET", "Neck Snap", Family::bounce, "X..X..XX..X..X.X", "......*......*..", 0.67f, 2, 0.4f, 2.0f, 1.0f, true, 90.0f, 0.22f, 0.55f, 0.35f, false, true },
    { "TRIPLET", "Spring Loaded", Family::bounce, "X.XgX.XgX.Xg.gXg", "....*.........*.", 0.665f, 1, 0.6f, 5.0f, 1.0f, true, 90.0f, 0.3f, 0.42f, 0.36f, false, true },
    { "LEFT-FIELD", "Woozy Wonk", Family::wonk, "X..Xg.g.X..Xg.g.", "...*.......*....", 0.71f, 2, 0.62f, 7.0f, 1.0f, false, 115.0f, 0.4f, 0.42f, 0.45f, true, true },
    { "LEFT-FIELD", "Broken Escalator", Family::wonk, "X..X..X..X..X.g.", "......*.....*...", 0.73f, 2, 0.55f, 4.0f, 1.0f, false, 120.0f, 0.35f, 0.4f, 0.45f, true, true },
    { "LEFT-FIELD", "Lopsided", Family::wonk, "X.g..X.g..X.g.g.", ".....*....*.....", 0.7f, 2, 0.65f, 7.0f, 1.0f, true, 90.0f, 0.4f, 0.38f, 0.45f, true, true },
    { "LEFT-FIELD", "Tipsy Robot", Family::wonk, "XgX.X.gXX.gX.XgX", "....*........*..", 0.69f, 2, 0.45f, 3.0f, 1.0f, false, 95.0f, 0.3f, 0.5f, 0.4f, false, true },
    { "LEFT-FIELD", "Glass Stairs", Family::wonk, "X.X.X.g.X.X.X.g.", "..*...*...*...*.", 0.75f, 2, 0.35f, 1.5f, 1.0f, true, 90.0f, 0.4f, 0.75f, 0.5f, true, true },
    { "LEFT-FIELD", "Crooked Smile", Family::wonk, "X--.Xg.XX..gX.g.", "....*......*....", 0.72f, 2, 0.7f, 8.0f, 1.0f, true, 90.0f, 0.3f, 0.35f, 0.4f, false, true },
    { "LEFT-FIELD", "Seasick", Family::wonk, "X.gX.g.gX..X.gX.", "...*.......*....", 0.74f, 1, 0.6f, 9.0f, 0.95f, false, 160.0f, 0.45f, 0.33f, 0.45f, true, true },
    { "DUB", "Mud Skank", Family::skank, "..X.g.X...X.g.Xg", "..*.......*.....", 0.6f, 2, 0.42f, 4.0f, 1.0f, true, 90.0f, 0.35f, 0.38f, 0.5f, true, true },
    { "DUB", "Sound System", Family::skank, "..X...X.g.X...Xg", "......*.......*.", 0.56f, 2, 0.4f, 4.0f, 1.0f, true, 90.0f, 0.5f, 0.28f, 0.55f, true, true },
    { "DUB", "Echo Chamber", Family::skank, "X.....X...X.....", "*.....*...*.....", 0.62f, 2, 0.45f, 5.0f, 1.0f, true, 90.0f, 0.55f, 0.32f, 0.55f, true, true },
    { "DUB", "Dread Steps", Family::bounce, "X..gX..gX..gX..g", "....*.......*...", 0.6f, 2, 0.55f, 5.0f, 0.9f, true, 90.0f, 0.35f, 0.35f, 0.4f, false, true },
    { "DUB", "Rubadub", Family::skank, "X.gX..X.g.X..gX.", "...*......*.....", 0.63f, 2, 0.5f, 4.0f, 1.0f, true, 90.0f, 0.4f, 0.4f, 0.45f, true, true },
    { "SHUFFLE", "Rubber Shuffle", Family::shuffle, "X.gX.gX-X.gX.Xg.", "...*.......*....", 0.62f, 2, 0.6f, 6.0f, 1.0f, true, 90.0f, 0.22f, 0.45f, 0.4f, false, true },
    { "SHUFFLE", "Velvet Strut", Family::shuffle, "X.gXX.gXX.gXXgX.", ".......*......*.", 0.665f, 1, 0.7f, 10.0f, 1.0f, true, 90.0f, 0.2f, 0.4f, 0.35f, false, true },
    { "SHUFFLE", "Jelly Bounce", Family::bounce, "X..X..X.X..X..Xg", "......*.......*.", 0.58f, 2, 0.55f, 5.0f, 1.0f, true, 90.0f, 0.3f, 0.5f, 0.42f, false, true },
    { "SHUFFLE", "Pony Gallop", Family::gallop, "XXX.XXX.XXX.XgX.", "..*.......*.....", 0.57f, 2, 0.5f, 4.0f, 1.0f, true, 90.0f, 0.18f, 0.55f, 0.34f, false, false },
    { "SHUFFLE", "Two-Step Lean", Family::shuffle, "X..X..g.X.X...g.", "...*......*.....", 0.64f, 2, 0.55f, 4.0f, 1.0f, true, 90.0f, 0.25f, 0.5f, 0.35f, false, true },
    { "SHUFFLE", "Shuffle Shoes", Family::shuffle, "XgX.gXX.XgX.gX.X", "......*......*..", 0.62f, 2, 0.5f, 3.0f, 1.0f, true, 90.0f, 0.2f, 0.55f, 0.3f, false, true },
    { "SUB", "Sub Pulse", Family::straight, "X--.X--.X--.X-.g", "................", 0.6f, 2, 0.85f, 14.0f, 0.92f, true, 90.0f, 0.2f, 0.4f, 0.2f, false, false },
    { "SUB", "Breathing Sub", Family::drag, "X--gX--gX--gX-g.", "................", 0.62f, 1, 0.9f, 25.0f, 0.8f, true, 90.0f, 0.2f, 0.4f, 0.2f, false, false },
    { "SUB", "Heartbeat", Family::bounce, "X.g.....X.g.....", "..*.......*.....", 0.6f, 2, 0.6f, 10.0f, 0.9f, true, 90.0f, 0.25f, 0.3f, 0.3f, true, true },
    { "SUB", "Metro Pump", Family::straight, ".gXX.gXX.gXX.gXX", "...............*", 0.5f, 2, 0.92f, 8.0f, 0.85f, false, 75.0f, 0.15f, 0.5f, 0.3f, false, true },
    { "STUTTER", "Stutter Step", Family::shuffle, "X.X.XXXXX.X.XgXg", "........*.......", 0.64f, 3, 0.5f, 1.5f, 1.0f, true, 90.0f, 0.3f, 0.6f, 0.35f, false, true },
    { "STUTTER", "Machine Lean", Family::gallop, "XXXXXXXX..XX..X.", "..............*.", 0.66f, 3, 0.45f, 1.2f, 1.0f, true, 90.0f, 0.35f, 0.65f, 0.4f, true, true },
    { "STUTTER", "Fill Me In", Family::straight, "X.......XXXXXXXX", "...............*", 0.6f, 2, 0.45f, 2.0f, 1.0f, true, 90.0f, 0.45f, 0.5f, 0.45f, true, true },
    { "STUTTER", "Hiccup", Family::wonk, "X..XX...X..XX.g.", "....*.......*...", 0.68f, 2, 0.35f, 1.5f, 1.0f, false, 45.0f, 0.2f, 0.7f, 0.4f, false, true },
} };

// mulberry32, matching the prototype's generator.
struct Rng
{
    std::uint32_t a;
    double next() noexcept
    {
        a += 0x6D2B79F5u;
        std::uint32_t t = (a ^ (a >> 15)) * (1u | a);
        t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
        return (double) (t ^ (t >> 14)) / 4294967296.0;
    }
    int below (int n) noexcept { return std::min (n - 1, (int) (next() * n)); }
};

inline float raisedCosine (double f) noexcept { return (float) (0.5 - 0.5 * std::cos (3.14159265358979323846 * f)); }
} // namespace

const char* familyName (Family f) { return familyNames[std::clamp ((int) f, 0, (int) Family::count - 1)]; }

Family familyFromName (const std::string& name)
{
    for (int i = 0; i < (int) Family::count; ++i)
        if (name == familyNames[i]) return (Family) i;
    return Family::shuffle;
}

Pattern parsePattern (const char* cells, const char* slap)
{
    Pattern p {};
    for (int i = 0; i < numSteps && cells != nullptr && cells[i] != 0; ++i) {
        const char c = cells[i];
        p[(size_t) i].v = c == 'X' ? Step::hit : c == 'g' ? Step::ghost : c == '-' ? Step::tie : Step::off;
    }
    for (int i = 0; i < numSteps && slap != nullptr && slap[i] != 0; ++i)
        p[(size_t) i].slap = slap[i] == '*';
    return p;
}

std::string cellsToString (const Pattern& p)
{
    std::string s;
    for (const auto& c : p) s += c.v == Step::hit ? 'X' : c.v == Step::ghost ? 'g' : c.v == Step::tie ? '-' : '.';
    return s;
}

std::string slapToString (const Pattern& p)
{
    std::string s;
    for (const auto& c : p) s += c.slap ? '*' : '.';
    return s;
}

int stepAt (double position, double swing)
{
    int idx = 0;
    for (int i = 0; i < numSteps; ++i)
        if (stepStart (i, swing) <= position) idx = i;
    return idx;
}

// ---------------------------------------------------------------------------------------------------------------------

void Envelope::build (const Pattern& p, double swing, double length, double a, float closed, bool slapOnly)
{
    struct Event { double s, e; float lvl; bool slap; };
    std::array<Event, numSteps> evs {};
    int n = 0, cur = -1;
    for (int i = 0; i < numSteps; ++i) {
        const auto& c = p[(size_t) i];
        const double s = stepStart (i, swing), e = s + length * slotLength (i, swing);
        if (c.v == Step::hit || c.v == Step::ghost || (c.v == Step::tie && cur < 0)) {
            evs[(size_t) n] = { s, e, c.v == Step::ghost ? ghostLevel : 1.0f, c.slap };
            cur = n++;
        } else if (c.v == Step::tie) {
            evs[(size_t) cur].e = e;
        } else {
            cur = -1;
        }
    }
    if (slapOnly) {
        int kept = 0;
        for (int k = 0; k < n; ++k)
            if (evs[(size_t) k].slap) evs[(size_t) kept++] = evs[(size_t) k];
        n = kept;
    }
    if (n == 0) { setConstant (closed); return; }

    // Levels sit between the closed floor and fully open, so a ghost never dips below DEPTH's floor.
    for (int k = 0; k < n; ++k) evs[(size_t) k].lvl = closed + (1.0f - closed) * evs[(size_t) k].lvl;

    // Centre each attack on its grid line: the gate is half open exactly on the beat.
    for (int k = 0; k < n; ++k) evs[(size_t) k].s -= a * 0.5;

    std::array<double, numSteps> end {};
    std::array<bool, numSteps> merge {};
    const auto nextStart = [&] (int k) { return k == n - 1 ? evs[0].s + numSteps : evs[(size_t) k + 1].s; };
    for (int k = 0; k < n; ++k) {
        const auto& ev = evs[(size_t) k];
        const double eEff = std::max (std::min (ev.e, nextStart (k)), ev.s + a);
        // Too short a gap to close and reopen cleanly: hold open into the next event instead of chattering.
        merge[(size_t) k] = nextStart (k) - eEff < a;
        end[(size_t) k] = std::min (eEff, nextStart (k));
    }
    if (n == 1 && merge[0]) { setConstant (evs[0].lvl); return; }

    count = 0;
    const auto push = [this] (double t, float v) { pts[(size_t) count++] = { t, v }; };
    for (int k = 0; k < n; ++k) {
        const auto& ev = evs[(size_t) k];
        const int prev = (k - 1 + n) % n;
        push (ev.s, merge[(size_t) prev] ? evs[(size_t) prev].lvl : closed);
        push (ev.s + a, ev.lvl);
        if (! merge[(size_t) k]) {
            push (end[(size_t) k], ev.lvl);
            push (end[(size_t) k] + a, closed);
        }
    }
    extend();
}

void Envelope::buildInverseOf (const Envelope& gate, float closed)
{
    const auto inverse = [closed] (float v) {
        if (closed >= 1.0f) return 1.0f;
        return 1.0f - std::clamp ((v - closed) / (1.0f - closed), 0.0f, 1.0f);
    };
    if (gate.isConstant()) { setConstant (inverse (gate.constant)); return; }
    count = gate.count;
    for (int i = 0; i < count; ++i) pts[(size_t) i] = { gate.pts[(size_t) i].t, inverse (gate.pts[(size_t) i].v) };
    extend();
}

void Envelope::extend()
{
    extCount = 0;
    for (const double shift : { -16.0, 0.0, 16.0 })
        for (int i = 0; i < count; ++i)
            ext[(size_t) extCount++] = { pts[(size_t) i].t + shift, pts[(size_t) i].v };
}

float Envelope::eval (double position) const noexcept
{
    if (count == 0) return constant;
    double u = std::fmod (position, (double) numSteps);
    if (u < 0) u += numSteps;
    int lo = 0, hi = extCount - 1;
    while (lo < hi) {
        const int mid = (lo + hi + 1) >> 1;
        if (ext[(size_t) mid].t <= u) lo = mid; else hi = mid - 1;
    }
    const auto& p = ext[(size_t) lo];
    if (lo + 1 >= extCount) return p.v;
    const auto& q = ext[(size_t) lo + 1];
    if (q.t <= p.t || std::abs (q.v - p.v) < 1.0e-9f) return p.v;
    return p.v + (q.v - p.v) * raisedCosine ((u - p.t) / (q.t - p.t));
}

// ---------------------------------------------------------------------------------------------------------------------

int factoryIndexOf (const std::string& name)
{
    for (int i = 0; i < numFactoryGrooves; ++i)
        if (name == factory[(size_t) i].name) return i;
    return -1;
}

const FactoryGroove& factoryGroove (int index)
{
    return factory[(size_t) (((index % numFactoryGrooves) + numFactoryGrooves) % numFactoryGrooves)];
}

namespace {
Step stepOf (char c) noexcept { return c == 'X' ? Step::hit : c == 'g' ? Step::ghost : c == '-' ? Step::tie : Step::off; }

// How strong each step is in the bar: downbeats first, then beats, then the off-steps.
constexpr std::array<float, numSteps> strength { 1.0f, .2f, .5f, .3f, .8f, .2f, .5f, .3f, .9f, .2f, .5f, .3f, .8f, .2f, .5f, .4f };

// One small change to a beat, so repeats in a phrase are related but not stiff.
void vary (Rng& rng, Step* beat)
{
    for (int attempt = 0; attempt < 6; ++attempt) {
        const int i = 1 + rng.below (3); // the beat's first step stays, so the phrase keeps its shape
        auto& v = beat[i];
        switch (rng.below (6)) {
            case 0: if (v == Step::hit && i < 3 && beat[i + 1] == Step::off) { v = Step::off; beat[i + 1] = Step::hit; return; } break;
            case 1: if (v == Step::hit) { v = Step::ghost; return; } break;
            case 2: if (v == Step::off && (i & 1)) { v = Step::ghost; return; } break;
            case 3: if (v == Step::off && beat[i - 1] != Step::off) { v = Step::tie; return; } break;
            case 4: if (v == Step::hit || v == Step::ghost) { v = Step::off; return; } break;
            default: if (v == Step::off) { v = Step::hit; return; } break;
        }
    }
}

std::array<Step, numSteps> composeBeats (Rng& rng, const FamilyRecipe& recipe)
{
    int numCells = 0;
    while (numCells < (int) recipe.cells.size() && recipe.cells[(size_t) numCells] != nullptr) ++numCells;
    static constexpr int forms[][4] { { 0, 0, 0, 1 }, { 0, 1, 0, 2 }, { 0, 0, 1, 1 }, { 0, 1, 0, 1 },
                                      { 0, 1, 2, 1 }, { 0, 0, 1, 2 }, { 0, 1, 1, 2 }, { 0, 1, 2, 3 } };
    const auto& form = forms[rng.below ((int) std::size (forms))];
    std::array<int, 4> cell {};
    for (int k = 0; k < 4; ++k) {
        cell[(size_t) k] = rng.below (numCells);
        for (int guard = 0; guard < 8; ++guard) { // distinct letters get distinct cells
            bool clash = false;
            for (int j = 0; j < k; ++j) clash = clash || cell[(size_t) j] == cell[(size_t) k];
            if (! clash) break;
            cell[(size_t) k] = rng.below (numCells);
        }
    }
    std::array<Step, numSteps> steps {};
    for (int beat = 0; beat < 4; ++beat) {
        const char* text = recipe.cells[(size_t) cell[(size_t) form[beat]]];
        for (int i = 0; i < 4; ++i) steps[(size_t) (beat * 4 + i)] = stepOf (text[i]);
        bool repeat = false;
        for (int j = 0; j < beat; ++j) repeat = repeat || form[j] == form[beat];
        if ((repeat && rng.next() < 0.45) || (beat == 3 && rng.next() < 0.3)) vary (rng, &steps[(size_t) beat * 4]);
    }
    return steps;
}

std::array<Step, numSteps> composeGroups (Rng& rng, const FamilyRecipe& recipe)
{
    static constexpr int groupings[][7] { { 3, 3, 2, 3, 3, 2 }, { 3, 3, 3, 3, 4 }, { 5, 5, 6 }, { 3, 5, 3, 5 },
                                          { 4, 3, 3, 3, 3 }, { 3, 3, 5, 5 }, { 6, 6, 4 }, { 5, 3, 5, 3 },
                                          { 2, 3, 3, 2, 3, 3 }, { 3, 3, 4, 3, 3 }, { 3, 3, 2, 4, 4 }, { 5, 5, 3, 3 } };
    const auto& g = groupings[rng.below ((int) std::size (groupings))];
    std::array<Step, numSteps> steps {};
    int pos = 0;
    for (const int len : g) {
        if (len == 0 || pos >= numSteps) break;
        steps[(size_t) pos] = pos > 0 && rng.next() < 0.15 ? Step::ghost : Step::hit;
        bool tied = rng.next() < recipe.groupTie;
        for (int i = 1; i < len && pos + i < numSteps; ++i) {
            auto& v = steps[(size_t) (pos + i)];
            if (tied) { v = Step::tie; tied = rng.next() < recipe.groupTie * 0.7; continue; }
            if ((i == 2 && len >= 4 && rng.next() < recipe.groupGhost) || (i == len - 1 && rng.next() < recipe.groupGhost * 0.5))
                v = Step::ghost;
        }
        pos += len;
    }
    return steps;
}

int distance (const std::array<Step, numSteps>& a, const Pattern& b) noexcept
{
    int d = 0;
    for (int i = 0; i < numSteps; ++i) d += a[(size_t) i] != b[(size_t) i].v;
    return d;
}
} // namespace

Generated generate (std::uint32_t seed, int familyFilter, const Pattern* avoid)
{
    Rng rng { seed };
    Family fam;
    if (familyFilter < 0 || familyFilter >= (int) Family::count) {
        int total = 0;
        for (const auto& r : recipes) total += r.weight;
        double r = rng.next() * total;
        fam = Family::shuffle;
        for (int i = 0; i < (int) Family::count; ++i) {
            r -= recipes[(size_t) i].weight;
            if (r <= 0) { fam = (Family) i; break; }
        }
    } else {
        fam = (Family) familyFilter;
    }
    const auto& recipe = recipes[(size_t) fam];

    // Compose until the rhythm is new: at least four steps away from every factory groove and from
    // the groove it replaces, so NEW GROOVE never lands on (or next to) a preset.
    std::array<Step, numSteps> steps {};
    for (int attempt = 0; attempt < 24; ++attempt) {
        steps = rng.next() < recipe.groupChance ? composeGroups (rng, recipe) : composeBeats (rng, recipe);

        // A short fill into the next bar now and then.
        if (rng.next() < 0.22) {
            static constexpr const char* fills[] { "Xg", "gX", "XX", "XgXg", "gXgX", "X.XX", "XXXX", "Xggg" };
            const char* fill = fills[rng.below ((int) std::size (fills))];
            const int n = (int) std::strlen (fill);
            for (int i = 0; i < n; ++i) steps[(size_t) (numSteps - n + i)] = stepOf (fill[i]);
        }
        if (fam != Family::skank || rng.next() < 0.4) steps[0] = Step::hit;

        const auto onCount = [&] { return (int) std::count_if (steps.begin(), steps.end(), [] (Step v) { return v != Step::off; }); };
        for (int guard = 0; onCount() < 5 && guard < 32; ++guard) {
            int best = -1; double bestScore = -1;
            for (int i = 0; i < numSteps; ++i) {
                const double score = strength[(size_t) i] + rng.next() * 0.3;
                if (steps[(size_t) i] == Step::off && score > bestScore) { bestScore = score; best = i; }
            }
            if (best >= 0) steps[(size_t) best] = Step::hit;
        }
        for (int guard = 0; onCount() > 12 && guard < 32; ++guard) {
            int worst = -1; double worstScore = 9;
            for (int i = 1; i < numSteps; ++i) {
                const double score = strength[(size_t) i] + rng.next() * 0.3;
                if (steps[(size_t) i] != Step::off && score < worstScore) { worstScore = score; worst = i; }
            }
            if (worst >= 0) steps[(size_t) worst] = Step::off;
        }
        // Never leave more than four empty steps in a row, around the loop too.
        for (int i = 0, run = 0; i < 2 * numSteps; ++i) {
            const int j = i % numSteps;
            if (steps[(size_t) j] == Step::off) {
                // A ghost two steps back leaves two empty steps (j-1, j) in the current run.
                if (++run > 4) { steps[(size_t) ((j + 14) % numSteps)] = Step::ghost; run = 2; }
            } else {
                run = 0;
            }
        }
        for (int i = 0; i < numSteps; ++i)
            if (steps[(size_t) i] == Step::tie && (i == 0 || steps[(size_t) i - 1] == Step::off)) steps[(size_t) i] = Step::hit;

        int nearest = numSteps;
        for (int f = 0; f < numFactoryGrooves; ++f)
            nearest = std::min (nearest, distance (steps, parsePattern (factory[(size_t) f].cells, factory[(size_t) f].slap)));
        if (avoid != nullptr) nearest = std::min (nearest, distance (steps, *avoid));
        if (nearest >= 4) break;
    }

    Generated out;
    for (int i = 0; i < numSteps; ++i) out.pattern[(size_t) i] = { steps[(size_t) i], false };
    auto& pattern = out.pattern;

    // Slap sends go to the hits followed by the longest gaps, so the echo has room to speak.
    struct Candidate { int i; double score; };
    std::array<Candidate, numSteps> candidates {};
    int numCandidates = 0;
    for (int i = 0; i < numSteps; ++i) {
        if (pattern[(size_t) i].v != Step::hit) continue;
        int gap = 0;
        for (int k = 1; k < 8 && pattern[(size_t) ((i + k) % numSteps)].v == Step::off; ++k) ++gap;
        candidates[(size_t) numCandidates++] = { i, gap + rng.next() * 0.9 };
    }
    std::sort (candidates.begin(), candidates.begin() + numCandidates, [] (auto& x, auto& y) { return x.score > y.score; });
    const int sends = std::min (numCandidates, 2 + (int) (rng.next() * 3));
    for (int k = 0; k < sends; ++k) pattern[(size_t) candidates[(size_t) k].i].slap = true;

    out.swing = std::round ((recipe.swingLow + (recipe.swingHigh - recipe.swingLow) * (float) rng.next()) * 200.0f) / 200.0f;
    out.length = std::clamp (recipe.length + ((float) rng.next() - 0.5f) * 0.12f, 0.2f, 1.0f);
    out.family = fam;
    const auto& adj = adjectives[(size_t) fam];
    const auto& noun = nouns[(size_t) fam];
    for (int guard = 0; guard < 16; ++guard) {
        out.name = std::string (adj[(size_t) rng.below ((int) adj.size())]) + " " + noun[(size_t) rng.below ((int) noun.size())];
        if (factoryIndexOf (out.name) < 0) break;
    }
    return out;
}

} // namespace stutter
