#include "HarvestLights.h"
#include <cmath>

namespace energy::ui {
namespace {
const juce::Colour pink(0xffff52be), pearl(0xffffedf8), hot(0xffffc4e9), unlit(0xff6d244c);
constexpr float twoPi = juce::MathConstants<float>::twoPi;
constexpr int segments = 40, rotorHeads = 3;
constexpr double riseSeconds = .12, fallSeconds = .32, powerDownSeconds = .9, shockSeconds = .85;
constexpr float restingCharge = .2f;  // a stopped deck keeps a faint standby glow while HARVEST stays on

float smooth(float x) noexcept { x = juce::jlimit(0.f, 1.f, x); return x * x * (3 - 2 * x); }
float follow(float value, float target, double seconds, double tau) noexcept {
    return value + (target - value) * float(1 - std::exp(-seconds / tau));
}
juce::Path polyline(const std::vector<juce::Point<float>>& points, float dx = 0) {
    juce::Path path;
    for (size_t i = 0; i < points.size(); ++i) {
        const auto p = points[i].translated(dx, 0);
        if (i == 0) path.startNewSubPath(p); else path.lineTo(p);
    }
    return path;
}
void stroke(juce::Graphics& g, const juce::Path& path, juce::Colour colour, float width) {
    g.setColour(colour);
    g.strokePath(path, juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
juce::Path arc(float radius, float from, float to) {
    juce::Path path;
    path.addCentredArc(HarvestLights::centreX, HarvestLights::centreY, radius, radius, 0, from, to, true);
    return path;
}
juce::Path circle(float radius) {
    juce::Path path;
    path.addEllipse(HarvestLights::centreX - radius, HarvestLights::centreY - radius, radius * 2, radius * 2);
    return path;
}
}

HarvestLights::HarvestLights() {
    // Routes follow copper already printed on the board art (traced from DeviceShell.png), so the light
    // reads as current through the PCB under the resin rather than lines painted on top of it.
    const std::vector<juce::Point<float>> left{{70, 278}, {79, 298}, {126, 346}, {127, 350}, {127, 420}, {142, 437}};
    const std::vector<juce::Point<float>> bus{{468, 272}, {444, 297}, {444, 322}, {428, 340}, {428, 362},
                                              {415, 376}, {415, 396}, {407, 404}};
    traces_.push_back({polyline(left), 0, .00f});
    for (int i = 0; i < 3; ++i) traces_.push_back({polyline(bus, (i - 1) * 3.6f), 0, .21f + .17f * float(i)});
    for (auto& trace : traces_) trace.length = trace.path.getLength();

    // Soft additive-looking halo for packet heads, rendered once at 2x for Retina.
    halo_ = juce::Image(juce::Image::ARGB, 64, 64, true);
    juce::Graphics g(halo_);
    juce::ColourGradient glow(pearl, 32, 32, pink.withAlpha(0.f), 32, 0, true);
    glow.addColour(.10, pearl.withAlpha(.95f));
    glow.addColour(.24, hot.withAlpha(.70f));
    glow.addColour(.52, pink.withAlpha(.22f));
    g.setGradientFill(glow);
    g.fillEllipse(0, 0, 64, 64);
}

void HarvestLights::reset() noexcept {
    charge_ = fadeFrom_ = fadeTarget_ = speed_ = level_ = fill_ = 0;
    fadeElapsed_ = rotor_ = flow_ = 0;
    shockAge_ = igniteAge_ = 10;
    lastState_ = 0; lastGate_ = false;
}

float HarvestLights::tapeRatio(int state, float progress) noexcept {
    // Mirrors HarvestEngine::recipe(): playback ratio 2^(-2(1-s)) with s the smoothstep of progress.
    if (state == 2) return 1;
    if (state != 1) return 0;
    return std::pow(2.f, -2 * (1 - smooth(progress)));
}

void HarvestLights::advance(double seconds, const LightInputs& in) noexcept {
    seconds = juce::jlimit(0.0, .1, seconds);
    still_ = in.reducedMotion || in.bypassed || in.dimmed;
    dim_ = in.dimmed; bypassed_ = in.bypassed;

    // A stop that has come to rest powers the board down softly instead of throwing a shockwave.
    const bool resting = in.gate && in.state == 4;
    const float target = in.gate ? (resting ? restingCharge : 1.f) : 0.f;
    if (std::abs(target - fadeTarget_) > .01f) { fadeFrom_ = charge_; fadeTarget_ = target; fadeElapsed_ = 0; }
    const double duration = target > fadeFrom_ ? riseSeconds : resting ? powerDownSeconds : fallSeconds;
    fadeElapsed_ = std::min(duration, fadeElapsed_ + seconds);
    charge_ = fadeFrom_ + (target - fadeFrom_) * smooth(float(fadeElapsed_ / duration));

    // The rotor follows the engine's playback ratio while ON (stopping for a stop, reversing for a rewind) and
    // coasts down like a platter when switched OFF.
    const float ratio = in.gate ? (std::isfinite(in.ratio) ? juce::jlimit(-4.f, 4.f, in.ratio) : 1.f) : 0.f;
    speed_ = follow(speed_, ratio, seconds, std::abs(ratio) > std::abs(speed_) ? .035 : .22);
    fill_ = follow(fill_, in.gate ? (in.state == 1 ? in.progress : 1.f) : fill_, seconds, .05);

    // Audio level breathes the glow; silence still harvests at the base brightness.
    const float db = 20 * std::log10(std::max(1.0e-5f, std::isfinite(in.level) ? in.level : 0.f));
    const float norm = juce::jlimit(0.f, 1.f, (db + 42) / 36);
    level_ = follow(level_, norm, seconds, norm > level_ ? .018 : .20);

    if (in.gate && !lastGate_) igniteAge_ = 0;
    if (in.gate && lastState_ == 1 && in.state == 2) shockAge_ = 0;
    lastGate_ = in.gate; lastState_ = in.state;

    if (!still_) {
        rotor_ = std::fmod(rotor_ + seconds * speed_ * twoPi * 1.15, double(twoPi));
        if (rotor_ < 0) rotor_ += twoPi;
        flow_ += seconds * (.18 * (speed_ < 0 ? -1 : 1) + .72 * speed_);
        flow_ -= std::floor(flow_);
        shockAge_ += seconds;
        igniteAge_ += seconds;
    } else {
        shockAge_ = igniteAge_ = 10;
    }
    if (charge_ <= .0005f && !in.gate) { speed_ = 0; fill_ = 0; }
}

bool HarvestLights::needsFrames() const noexcept {
    return !still_ && (charge_ > .0005f || std::abs(speed_) > .001f || shockAge_ < shockSeconds);
}

float HarvestLights::intensity() const noexcept {
    const float flash = still_ ? 0.f : float(std::exp(-igniteAge_ / .12) * .5 + std::exp(-shockAge_ / .16) * .8);
    const float pump = still_ ? .9f : .78f + .42f * level_;
    const float scale = bypassed_ ? .18f : dim_ ? .12f : 1.f;
    return charge_ * pump * (1 + flash) * scale;
}

void HarvestLights::paintTrace(juce::Graphics& g, const Trace& trace, float lit) const {
    stroke(g, trace.path, pink.withAlpha(juce::jmin(1.f, .08f * lit)), 9);
    stroke(g, trace.path, pink.withAlpha(juce::jmin(1.f, .30f * lit)), 3.6f);
    stroke(g, trace.path, hot.withAlpha(juce::jmin(1.f, .62f * lit)), 1.3f);
}

void HarvestLights::paintPackets(juce::Graphics& g, const Trace& trace, float lit) const {
    constexpr int perTrace = 2, tail = 14;
    for (int k = 0; k < perTrace; ++k) {
        const float u = float(std::fmod(flow_ + trace.phase + k / double(perTrace), 1.0));
        // Fade in leaving the source pad and out arriving at the far end, so packets never pop.
        const float edge = smooth(u / .08f) * smooth((1 - u) / .10f);
        const float alpha = juce::jmin(1.f, lit * edge);
        if (alpha < .01f) continue;
        // Trails stream behind the head in the direction of travel (towards the source while rewinding).
        const float head = u * trace.length, step = 2.6f * (.55f + .45f * std::abs(speed_)) * (speed_ < 0 ? -1.f : 1.f);
        for (int j = tail; j >= 1; --j) {
            const float at = head - float(j) * step;
            if (at < 0 || at > trace.length) continue;
            const auto p = trace.path.getPointAlongPath(at);
            const float fade = 1 - float(j) / float(tail + 1), r = .5f + 1.1f * fade;
            g.setColour(hot.withAlpha(alpha * fade * fade * .85f));
            g.fillEllipse(p.x - r, p.y - r, r * 2, r * 2);
        }
        const auto p = trace.path.getPointAlongPath(head);
        g.setOpacity(alpha);
        g.drawImage(halo_, {p.x - 11, p.y - 11, 22, 22});
        g.setOpacity(1);
    }
}

void HarvestLights::paintBoard(juce::Graphics& g) const {
    const float i = intensity();
    if (i < .002f && shockAge_ >= shockSeconds) return;

    // Light from the core diffusing through the translucent resin.
    {
        juce::ColourGradient bloom(hot.withAlpha(juce::jmin(1.f, .13f * i)), centreX, centreY,
                                   pink.withAlpha(0.f), centreX, centreY + 250, true);
        bloom.addColour(.42, pink.withAlpha(juce::jmin(1.f, .06f * i)));
        g.setGradientFill(bloom);
        g.fillEllipse(centreX - 250, centreY - 250, 500, 500);
    }

    const float traceLit = i * (.30f + .70f * fill_);
    for (const auto& trace : traces_) paintTrace(g, trace, traceLit);
    if (!still_) for (const auto& trace : traces_) paintPackets(g, trace, i * (.45f + .55f * fill_));

    // One shockwave when the tape lands at speed; it travels under the panels, which the caller clips out.
    if (!still_ && shockAge_ < shockSeconds) {
        const float a = float(shockAge_ / shockSeconds), eased = 1 - (1 - a) * (1 - a) * (1 - a);
        const float fade = (1 - a) * (1 - a) * juce::jmax(charge_, .25f);
        const auto ring = circle(lipRadius + 4 + 300 * eased);
        const float width = 1 + 1.6f * (1 - a);
        stroke(g, ring, pink.withAlpha(.16f * fade), 34 * width);
        stroke(g, ring, pink.withAlpha(.42f * fade), 9 * width);
        stroke(g, ring, pearl.withAlpha(.95f * fade), 2.2f * width);
    }
}

void HarvestLights::paintRing(juce::Graphics& g, float progress) const {
    const float i = intensity();
    if (i > .002f) {
        const auto lip = circle(lipRadius);
        stroke(g, lip, pink.withAlpha(juce::jmin(1.f, .07f * i)), 14);
        stroke(g, lip, pink.withAlpha(juce::jmin(1.f, .26f * i)), 5);
        stroke(g, lip, hot.withAlpha(juce::jmin(1.f, .60f * i)), 1.4f);
        const auto rim = circle(capRadius + 1.5f);
        stroke(g, rim, pink.withAlpha(juce::jmin(1.f, .22f * i)), 9);
        stroke(g, rim, pearl.withAlpha(juce::jmin(1.f, .55f * i)), 1.6f);
    }

    // Three rotor heads, one per blade of the HARVEST mark, sweep the LED segments like a jog-wheel ring.
    const float rotorGain = still_ ? 0.f : juce::jmin(1.f, i) * juce::jlimit(0.f, 1.f, .25f + std::abs(speed_) * 1.2f);
    const float tailLength = .55f + .9f * juce::jmin(1.5f, std::abs(speed_));
    const bool reverse = speed_ < 0;
    const float spacing = twoPi / segments, sector = twoPi / rotorHeads;
    for (int s = 0; s < segments; ++s) {
        // Arc angles run clockwise from 12 o'clock, so progress fills from the top.
        const float a = float(s) * spacing;
        const auto segment = arc(segmentRadius, a + .025f, a + .125f);
        float highlight = 0;
        if (rotorGain > .001f) {
            float behind = std::fmod(reverse ? (a + .075f) - float(rotor_) : float(rotor_) - (a + .075f), sector);
            if (behind < 0) behind += sector;
            highlight = rotorGain * std::exp(-behind / tailLength) * (behind < sector * .92f ? 1.f : 0.f);
        }
        // Idle colours match the original ring exactly; while charged the filled segments drop to a deeper
        // pink so the pearl rotor heads read clearly against them.
        const bool filled = float(s) < progress * segments;
        const float deepen = still_ ? 0.f : juce::jmin(1.f, charge_) * .55f;
        const juce::Colour base = filled ? pink.brighter(.4f).interpolatedWith(juce::Colour(0xffd8399c), deepen)
                                         : unlit.interpolatedWith(juce::Colour(0xff4a1734), deepen);
        if (highlight > .01f) {
            stroke(g, segment, pink.withAlpha(juce::jmin(1.f, .62f * highlight)), 11);
            stroke(g, segment, hot.withAlpha(juce::jmin(1.f, .55f * highlight)), 5);
        }
        g.setColour(base.interpolatedWith(pearl, juce::jmin(1.f, highlight * 1.25f)));
        g.strokePath(segment, juce::PathStrokeType(3));
    }
}

}
