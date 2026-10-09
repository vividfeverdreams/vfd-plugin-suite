#include "Groove.h"
#include <algorithm>
#include <cmath>

namespace stutter {

namespace {
constexpr const char* familyNames[] { "SHUFFLE", "BOUNCE", "SKANK", "DRAG", "GALLOP", "WONK", "STRAIGHT" };

struct FamilyRecipe
{
    int weight;
    float swingLow, swingHigh, length, ghost, tie;
    std::array<float, numSteps> hit; // probability of a hit per step; WONK builds its own
    bool groupings;
};

const std::array<FamilyRecipe, (size_t) Family::count> recipes { {
    { 24, 0.58f, 0.66f, 0.60f, 0.45f, 0.18f, { 1,.1f,.55f,.6f, .85f,.15f,.5f,.55f, .95f,.1f,.55f,.6f, .8f,.15f,.45f,.7f }, false },
    { 20, 0.55f, 0.62f, 0.55f, 0.35f, 0.15f, { 1,.1f,.15f,.9f, .2f,.15f,.85f,.2f, .9f,.1f,.2f,.85f, .2f,.2f,.8f,.3f }, false },
    { 14, 0.56f, 0.64f, 0.42f, 0.30f, 0.05f, { .45f,.1f,.95f,.25f, .1f,.1f,.95f,.3f, .4f,.1f,.95f,.25f, .1f,.15f,.95f,.4f }, false },
    { 14, 0.64f, 0.72f, 0.82f, 0.50f, 0.40f, { 1,.05f,.2f,.4f, .15f,.1f,.5f,.2f, .7f,.1f,.25f,.45f, .2f,.1f,.55f,.35f }, false },
    { 12, 0.55f, 0.62f, 0.50f, 0.25f, 0.05f, { 1,.85f,.8f,.1f, .9f,.8f,.75f,.1f, .95f,.85f,.8f,.1f, .9f,.75f,.7f,.2f }, false },
    { 11, 0.66f, 0.75f, 0.62f, 0.40f, 0.20f, {}, true },
    {  5, 0.50f, 0.52f, 0.55f, 0.30f, 0.10f, { 1,.2f,.8f,.2f, .9f,.2f,.8f,.3f, 1,.2f,.8f,.2f, .9f,.3f,.75f,.4f }, false },
} };

const std::array<std::array<const char*, 6>, (size_t) Family::count> adjectives { {
    { "Rubber", "Lazy", "Greasy", "Velvet", "Loose", "Bent" },
    { "Wet", "Springy", "Fat", "Jelly", "Bouncy", "Rubber" },
    { "Mud", "Dub", "Choppy", "Rusty", "Dusty", "Hollow" },
    { "Heavy", "Sleepy", "Syrup", "Molasses", "Slow", "Sticky" },
    { "Pony", "Rodeo", "Clip", "Trot", "Dusty", "Rocking" },
    { "Drunk", "Wobbly", "Tipsy", "Woozy", "Crooked", "Lopsided" },
    { "Clean", "Stiff", "Square", "Metro", "Steel", "Even" },
} };
const std::array<std::array<const char*, 4>, (size_t) Family::count> nouns { {
    { "Shuffle", "Strut", "Two-Step", "Shuffle" },
    { "Bounce", "Hop", "Pogo", "Spring" },
    { "Skank", "Upstroke", "Chop", "Skank" },
    { "Drag", "Limp", "Crawl", "Lean" },
    { "Gallop", "Canter", "Trot", "Gallop" },
    { "Wonk", "Stumble", "Lurch", "Wobble" },
    { "Pump", "Chop", "Grid", "Pump" },
} };

// Folders in browsing order. Built for 140 halftime, riddim and left-field bass; see PRESETS.md.
const std::array<FactoryGroove, numFactoryGrooves> factory { {
    { "HALFTIME", "Lean Back", Family::drag, "X-..g.X...X-.gX.", "......*.......*.", 0.68f, 1, 0.8f, 8.0f, 1.0f, true, 120.0f, 0.3f, 0.35f, 0.42f, true, true },
    { "HALFTIME", "Tar Pit", Family::drag, "X--.X.g.X--..gX.", "....*.........*.", 0.7f, 0, 0.85f, 14.0f, 0.95f, true, 140.0f, 0.32f, 0.3f, 0.38f, true, true },
    { "HALFTIME", "Syrup Drag", Family::drag, "X--.g.X-..gX-.g.", "......*.........", 0.68f, 1, 0.85f, 12.0f, 0.92f, false, 140.0f, 0.28f, 0.32f, 0.36f, false, true },
    { "HALFTIME", "Sub Stomp", Family::bounce, "X..X..X...X..X..", "......*......*..", 0.54f, 1, 0.5f, 3.0f, 1.0f, false, 70.0f, 0.15f, 0.5f, 0.3f, false, true },
    { "HALFTIME", "Wide Load", Family::shuffle, "X-.X-.X-..X-.X-.", "...*.........*..", 0.64f, 1, 0.75f, 6.0f, 1.0f, true, 90.0f, 0.25f, 0.45f, 0.45f, false, true },
    { "HALFTIME", "Slow Motion", Family::drag, "X.g.X-.gX.g.X--.", "........*.......", 0.62f, 0, 0.7f, 10.0f, 0.9f, true, 90.0f, 0.4f, 0.28f, 0.42f, true, true },
    { "RIDDIM", "Riddim Rattle", Family::bounce, "X..X..X..X..X.X.", ".........*....*.", 0.58f, 1, 0.45f, 2.5f, 1.0f, true, 90.0f, 0.2f, 0.55f, 0.32f, false, false },
    { "RIDDIM", "Chainsaw Lean", Family::gallop, "XXg.XXg.XXg.XgXg", ".........*....*.", 0.62f, 1, 0.42f, 2.0f, 1.0f, true, 90.0f, 0.18f, 0.6f, 0.3f, false, true },
    { "RIDDIM", "Tank Tread", Family::gallop, "XXX.XXX.XXX.XX.X", "..*.......*.....", 0.55f, 1, 0.4f, 2.0f, 1.0f, false, 60.0f, 0.1f, 0.5f, 0.28f, false, false },
    { "RIDDIM", "Head Nod", Family::bounce, "X..X.XX..X..X.X.", ".....*........*.", 0.6f, 1, 0.5f, 3.0f, 1.0f, true, 90.0f, 0.25f, 0.45f, 0.38f, false, true },
    { "RIDDIM", "Robot Shuffle", Family::shuffle, "X.XgX.XgX.XgX.Xg", "......*.......*.", 0.64f, 1, 0.45f, 2.0f, 1.0f, true, 90.0f, 0.2f, 0.55f, 0.3f, true, true },
    { "RIDDIM", "Clamp Down", Family::bounce, "XX.XX.XX.XX.XX.X", ".......*.......*", 0.57f, 1, 0.38f, 1.5f, 1.0f, true, 90.0f, 0.15f, 0.62f, 0.28f, true, false },
    { "TRIPLET", "Triplet Wob", Family::shuffle, "XgXgXgXgXgXgXgXg", "......*.......*.", 0.665f, 1, 0.55f, 4.0f, 0.95f, true, 90.0f, 0.25f, 0.5f, 0.3f, false, true },
    { "TRIPLET", "Yoyo", Family::shuffle, "XgXgX.X.XgXgX.X.", "............*...", 0.665f, 2, 0.6f, 2.0f, 1.0f, true, 90.0f, 0.3f, 0.6f, 0.3f, false, true },
    { "TRIPLET", "Rubber Band", Family::drag, "X-gX-gX-gX-gX.Xg", "............*...", 0.665f, 1, 0.7f, 6.0f, 1.0f, true, 90.0f, 0.3f, 0.4f, 0.4f, true, true },
    { "TRIPLET", "Neck Snap", Family::bounce, "X..X..XX..X..X.X", "......*......*..", 0.67f, 1, 0.4f, 2.0f, 1.0f, true, 90.0f, 0.22f, 0.55f, 0.35f, false, true },
    { "TRIPLET", "Spring Loaded", Family::bounce, "X.XgX.XgX.Xg.gXg", "....*.........*.", 0.665f, 0, 0.6f, 5.0f, 1.0f, true, 90.0f, 0.3f, 0.42f, 0.36f, false, true },
    { "LEFT-FIELD", "Woozy Wonk", Family::wonk, "X..Xg.g.X..Xg.g.", "...*.......*....", 0.71f, 1, 0.62f, 7.0f, 1.0f, false, 115.0f, 0.4f, 0.42f, 0.45f, true, true },
    { "LEFT-FIELD", "Broken Escalator", Family::wonk, "X..X..X..X..X.g.", "......*.....*...", 0.73f, 1, 0.55f, 4.0f, 1.0f, false, 120.0f, 0.35f, 0.4f, 0.45f, true, true },
    { "LEFT-FIELD", "Lopsided", Family::wonk, "X.g..X.g..X.g.g.", ".....*....*.....", 0.7f, 1, 0.65f, 7.0f, 1.0f, true, 90.0f, 0.4f, 0.38f, 0.45f, true, true },
    { "LEFT-FIELD", "Tipsy Robot", Family::wonk, "XgX.X.gXX.gX.XgX", "....*........*..", 0.69f, 1, 0.45f, 3.0f, 1.0f, false, 95.0f, 0.3f, 0.5f, 0.4f, false, true },
    { "LEFT-FIELD", "Glass Stairs", Family::wonk, "X.X.X.g.X.X.X.g.", "..*...*...*...*.", 0.75f, 1, 0.35f, 1.5f, 1.0f, true, 90.0f, 0.4f, 0.75f, 0.5f, true, true },
    { "LEFT-FIELD", "Crooked Smile", Family::wonk, "X--.Xg.XX..gX.g.", "....*......*....", 0.72f, 1, 0.7f, 8.0f, 1.0f, true, 90.0f, 0.3f, 0.35f, 0.4f, false, true },
    { "LEFT-FIELD", "Seasick", Family::wonk, "X.gX.g.gX..X.gX.", "...*.......*....", 0.74f, 0, 0.6f, 9.0f, 0.95f, false, 160.0f, 0.45f, 0.33f, 0.45f, true, true },
    { "DUB", "Mud Skank", Family::skank, "..X.g.X...X.g.Xg", "..*.......*.....", 0.6f, 1, 0.42f, 4.0f, 1.0f, true, 90.0f, 0.35f, 0.38f, 0.5f, true, true },
    { "DUB", "Sound System", Family::skank, "..X...X.g.X...Xg", "......*.......*.", 0.56f, 1, 0.4f, 4.0f, 1.0f, true, 90.0f, 0.5f, 0.28f, 0.55f, true, true },
    { "DUB", "Echo Chamber", Family::skank, "X.....X...X.....", "*.....*...*.....", 0.62f, 1, 0.45f, 5.0f, 1.0f, true, 90.0f, 0.55f, 0.32f, 0.55f, true, true },
    { "DUB", "Dread Steps", Family::bounce, "X..gX..gX..gX..g", "....*.......*...", 0.6f, 1, 0.55f, 5.0f, 0.9f, true, 90.0f, 0.35f, 0.35f, 0.4f, false, true },
    { "DUB", "Rubadub", Family::skank, "X.gX..X.g.X..gX.", "...*......*.....", 0.63f, 1, 0.5f, 4.0f, 1.0f, true, 90.0f, 0.4f, 0.4f, 0.45f, true, true },
    { "SHUFFLE", "Rubber Shuffle", Family::shuffle, "X.gX.gX-X.gX.Xg.", "...*.......*....", 0.62f, 1, 0.6f, 6.0f, 1.0f, true, 90.0f, 0.22f, 0.45f, 0.4f, false, true },
    { "SHUFFLE", "Velvet Strut", Family::shuffle, "X.gXX.gXX.gXXgX.", ".......*......*.", 0.665f, 0, 0.7f, 10.0f, 1.0f, true, 90.0f, 0.2f, 0.4f, 0.35f, false, true },
    { "SHUFFLE", "Jelly Bounce", Family::bounce, "X..X..X.X..X..Xg", "......*.......*.", 0.58f, 1, 0.55f, 5.0f, 1.0f, true, 90.0f, 0.3f, 0.5f, 0.42f, false, true },
    { "SHUFFLE", "Pony Gallop", Family::gallop, "XXX.XXX.XXX.XgX.", "..*.......*.....", 0.57f, 1, 0.5f, 4.0f, 1.0f, true, 90.0f, 0.18f, 0.55f, 0.34f, false, false },
    { "SHUFFLE", "Two-Step Lean", Family::shuffle, "X..X..g.X.X...g.", "...*......*.....", 0.64f, 1, 0.55f, 4.0f, 1.0f, true, 90.0f, 0.25f, 0.5f, 0.35f, false, true },
    { "SHUFFLE", "Shuffle Shoes", Family::shuffle, "XgX.gXX.XgX.gX.X", "......*......*..", 0.62f, 1, 0.5f, 3.0f, 1.0f, true, 90.0f, 0.2f, 0.55f, 0.3f, false, true },
    { "SUB", "Sub Pulse", Family::straight, "X--.X--.X--.X-.g", "................", 0.6f, 1, 0.85f, 14.0f, 0.92f, true, 90.0f, 0.2f, 0.4f, 0.2f, false, false },
    { "SUB", "Breathing Sub", Family::drag, "X--gX--gX--gX-g.", "................", 0.62f, 0, 0.9f, 25.0f, 0.8f, true, 90.0f, 0.2f, 0.4f, 0.2f, false, false },
    { "SUB", "Heartbeat", Family::bounce, "X.g.....X.g.....", "..*.......*.....", 0.6f, 1, 0.6f, 10.0f, 0.9f, true, 90.0f, 0.25f, 0.3f, 0.3f, true, true },
    { "SUB", "Metro Pump", Family::straight, ".gXX.gXX.gXX.gXX", "...............*", 0.5f, 1, 0.92f, 8.0f, 0.85f, false, 75.0f, 0.15f, 0.5f, 0.3f, false, true },
    { "STUTTER", "Stutter Step", Family::shuffle, "X.X.XXXXX.X.XgXg", "........*.......", 0.64f, 2, 0.5f, 1.5f, 1.0f, true, 90.0f, 0.3f, 0.6f, 0.35f, false, true },
    { "STUTTER", "Machine Lean", Family::gallop, "XXXXXXXX..XX..X.", "..............*.", 0.66f, 2, 0.45f, 1.2f, 1.0f, true, 90.0f, 0.35f, 0.65f, 0.4f, true, true },
    { "STUTTER", "Fill Me In", Family::straight, "X.......XXXXXXXX", "...............*", 0.6f, 1, 0.45f, 2.0f, 1.0f, true, 90.0f, 0.45f, 0.5f, 0.45f, true, true },
    { "STUTTER", "Hiccup", Family::wonk, "X..XX...X..XX.g.", "....*.......*...", 0.68f, 1, 0.35f, 1.5f, 1.0f, false, 45.0f, 0.2f, 0.7f, 0.4f, false, true },
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

Generated generate (std::uint32_t seed, int familyFilter)
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

    auto tmpl = recipe.hit;
    if (recipe.groupings) {
        static const std::array<std::array<int, 5>, 6> groupings { { { 3,5,3,5,0 }, { 5,3,5,3,0 }, { 3,3,3,3,4 },
                                                                    { 5,5,6,0,0 }, { 3,3,5,5,0 }, { 4,3,3,3,3 } } };
        const auto& g = groupings[(size_t) rng.below ((int) groupings.size())];
        tmpl.fill (0.16f);
        int pos = 0;
        for (const int len : g) {
            if (len == 0 || pos >= numSteps) break;
            tmpl[(size_t) pos] = 1.0f;
            if (len >= 4 && pos + 2 < numSteps) tmpl[(size_t) pos + 2] = 0.45f;
            pos += len;
        }
    }

    Generated out;
    auto& steps = out.pattern;
    for (int i = 0; i < numSteps; ++i) {
        Step v = Step::off;
        if (rng.next() < tmpl[(size_t) i]) v = Step::hit;
        else if (rng.next() < recipe.ghost * ((i % 2) ? 1.0 : 0.55)) v = Step::ghost;
        if (v == Step::hit && i > 0 && steps[(size_t) i - 1].v != Step::off && rng.next() < recipe.tie) v = Step::tie;
        steps[(size_t) i] = { v, false };
    }
    if (fam != Family::skank || rng.next() < 0.4) steps[0].v = Step::hit;

    const auto onCount = [&] { return (int) std::count_if (steps.begin(), steps.end(), [] (const Cell& c) { return c.v != Step::off; }); };
    for (int guard = 0; onCount() < 5 && guard < 32; ++guard) {
        int best = -1; double bestScore = -1;
        for (int i = 0; i < numSteps; ++i) {
            const double score = tmpl[(size_t) i] + rng.next() * 0.2;
            if (steps[(size_t) i].v == Step::off && score > bestScore) { bestScore = score; best = i; }
        }
        if (best >= 0) steps[(size_t) best].v = Step::hit;
    }
    for (int guard = 0; onCount() > 12 && guard < 32; ++guard) {
        int worst = -1; double worstScore = 9;
        for (int i = 1; i < numSteps; ++i) {
            const double score = tmpl[(size_t) i] + rng.next() * 0.2;
            if (steps[(size_t) i].v != Step::off && score < worstScore) { worstScore = score; worst = i; }
        }
        if (worst >= 0) steps[(size_t) worst].v = Step::off;
    }
    // Never leave more than four empty steps in a row, around the loop too.
    for (int i = 0, run = 0; i < 2 * numSteps; ++i) {
        const int j = i % numSteps;
        if (steps[(size_t) j].v == Step::off) {
            // A ghost two steps back leaves two empty steps (j-1, j) in the current run.
            if (++run > 4) { steps[(size_t) ((j + 14) % numSteps)].v = Step::ghost; run = 2; }
        } else {
            run = 0;
        }
    }
    for (int i = 0; i < numSteps; ++i)
        if (steps[(size_t) i].v == Step::tie && (i == 0 || steps[(size_t) i - 1].v == Step::off)) steps[(size_t) i].v = Step::hit;

    // Slap sends go to the hits followed by the longest gaps, so the echo has room to speak.
    struct Candidate { int i; double score; };
    std::array<Candidate, numSteps> candidates {};
    int numCandidates = 0;
    for (int i = 0; i < numSteps; ++i) {
        if (steps[(size_t) i].v != Step::hit) continue;
        int gap = 0;
        for (int k = 1; k < 8 && steps[(size_t) ((i + k) % numSteps)].v == Step::off; ++k) ++gap;
        candidates[(size_t) numCandidates++] = { i, gap + rng.next() * 0.9 };
    }
    std::sort (candidates.begin(), candidates.begin() + numCandidates, [] (auto& x, auto& y) { return x.score > y.score; });
    const int sends = std::min (numCandidates, 2 + (int) (rng.next() * 3));
    for (int k = 0; k < sends; ++k) steps[(size_t) candidates[(size_t) k].i].slap = true;

    out.swing = std::round ((recipe.swingLow + (recipe.swingHigh - recipe.swingLow) * (float) rng.next()) * 200.0f) / 200.0f;
    out.length = std::clamp (recipe.length + ((float) rng.next() - 0.5f) * 0.12f, 0.2f, 1.0f);
    out.family = fam;
    const auto& adj = adjectives[(size_t) fam];
    const auto& noun = nouns[(size_t) fam];
    out.name = std::string (adj[(size_t) rng.below ((int) adj.size())]) + " " + noun[(size_t) rng.below ((int) noun.size())];
    return out;
}

} // namespace stutter
