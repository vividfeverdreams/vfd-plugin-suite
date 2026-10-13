#include "StutterProcessor.h"
#include <cstdlib>
#if ! STUTTER_HEADLESS
 #include "StutterEditor.h"
#endif

using namespace stutter;

namespace {
const char* const paramIds[16] { "swing", "rate", "length", "smooth", "depth", "slapOn", "slapLock", "slapTime",
                                 "slapFeedback", "slapTone", "slapMix", "slapGaps", "slapWide", "subSafe", "output", "bypass" };
constexpr float subSafeChoices[] { 0.0f, 80.0f, 120.0f, 200.0f };

std::uint8_t pack (Cell c) noexcept { return (std::uint8_t) ((std::uint8_t) c.v | (c.slap ? 4 : 0)); }
Cell unpack (std::uint8_t b) noexcept { return { (Step) (b & 3), (b & 4) != 0 }; }

juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi, [] (float a, float b, float t) { return a * std::pow (b / a, t); },
             [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
             [] (float, float, float v) { return v; } };
}
}

double StutterProcessor::Snapshot::stepSeconds() const noexcept { return 60.0 / juce::jmax (1.0, bpm) * rateBeats (rate); }
double StutterProcessor::Snapshot::slapSeconds() const noexcept
{
    return slapLock ? juce::jlimit (0.01, maxLockSeconds, 2.0 * swing * stepSeconds()) : slapTimeMs / 1000.0;
}

juce::AudioProcessorValueTreeState::ParameterLayout StutterProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    const auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + "%"; });
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "swing", 1 }, "Swing", NormalisableRange<float> (50.0f, 75.0f, 0.1f), 60.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + "%"; })));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "rate", 1 }, "Rate", StringArray { "1/4", "1/8", "1/16", "1/32" }, 2));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "length", 1 }, "Length", NormalisableRange<float> (5.0f, 100.0f, 0.1f), 60.0f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "smooth", 1 }, "Smooth", logRange (1.0f, 40.0f), 6.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, v < 10 ? 1 : 0) + " ms"; })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "depth", 1 }, "Depth", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, pct));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "slapOn", 1 }, "Slap", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "slapLock", 1 }, "Slap Lock", true));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "slapTime", 1 }, "Slap Time", logRange (10.0f, 250.0f), 90.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ms"; })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "slapFeedback", 1 }, "Slap Feedback", NormalisableRange<float> (0.0f, 75.0f, 0.1f), 22.0f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "slapTone", 1 }, "Slap Tone", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 45.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
            const auto hz = StutterProcessor::toneHz (v / 100.0);
            return hz >= 1000 ? String (hz / 1000.0f, 1) + " kHz" : String (roundToInt (hz)) + " Hz"; })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "slapMix", 1 }, "Slap Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 40.0f, pct));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "slapGaps", 1 }, "Slap Gaps", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "slapWide", 1 }, "Slap Wide", true));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "subSafe", 1 }, "Sub Safe", StringArray { "Off", "80 Hz", "120 Hz", "200 Hz" }, 2));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));
    return layout;
}

StutterProcessor::StutterProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STUTTER", createLayout())
{
    for (int i = 0; i < 16; ++i) p[i] = apvts.getRawParameterValue (paramIds[i]);
    // A fresh instance opens on the first factory groove; a host's saved state replaces it.
    loadFactory (0);
}

bool StutterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void StutterProcessor::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    slap.prepare (sampleRate);
    smoothCoeff = (float) (1.0 - std::exp (-1.0 / (0.0005 * sampleRate))); // 0.5 ms: catches envelope rebuild jumps
    paramCoeff = (float) (1.0 - std::exp (-1.0 / (0.02 * sampleRate)));
    bypassStep = (float) (1.0 / (0.005 * sampleRate));
    built = {};
    bypassMix = p[bypassP]->load() > 0.5f ? 1.0f : 0.0f;
    gainSmooth = 1.0f; sendSmooth = 0.0f; gapsSmooth = 1.0f;
    outGain = juce::Decibels::decibelsToGain (p[outputP]->load());
    mixSmooth = p[slapMixP]->load() / 100.0f;
}

Pattern StutterProcessor::getPattern() const noexcept
{
    Pattern out {};
    for (int i = 0; i < numSteps; ++i) out[(size_t) i] = unpack (cells[(size_t) i].load (std::memory_order_relaxed));
    return out;
}

void StutterProcessor::setPattern (const Pattern& pattern)
{
    for (int i = 0; i < numSteps; ++i) cells[(size_t) i].store (pack (pattern[(size_t) i]), std::memory_order_relaxed);
    patternVersion.fetch_add (1, std::memory_order_release);
    grooveRevision.fetch_add (1);
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

void StutterProcessor::setCell (int index, Cell cell)
{
    if (index < 0 || index >= numSteps) return;
    auto pattern = getPattern();
    pattern[(size_t) index] = cell;
    groove.edited = true;
    setPattern (pattern);
}

void StutterProcessor::setParamValue (const juce::String& id, float value)
{
    if (auto* parameter = apvts.getParameter (id)) {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        parameter->endChangeGesture();
    }
}

void StutterProcessor::applyGroove (const Pattern& pattern, float swing, float length)
{
    if (! lockSwing) setParamValue ("swing", swing * 100.0f);
    setParamValue ("length", length * 100.0f);
    setPattern (pattern);
}

void StutterProcessor::loadFactory (int index)
{
    const int i = ((index % numFactoryGrooves) + numFactoryGrooves) % numFactoryGrooves;
    const auto& f = factoryGroove (i);
    setParamValue ("rate", (float) f.rate);
    setParamValue ("smooth", f.smoothMs);
    setParamValue ("depth", f.depth * 100.0f);
    setParamValue ("slapLock", f.lock ? 1.0f : 0.0f);
    setParamValue ("slapTime", f.timeMs);
    setParamValue ("slapFeedback", f.feedback * 100.0f);
    setParamValue ("slapTone", f.tone * 100.0f);
    setParamValue ("slapMix", f.mix * 100.0f);
    setParamValue ("slapGaps", f.gaps ? 1.0f : 0.0f);
    setParamValue ("slapWide", f.wide ? 1.0f : 0.0f);
    groove = { f.name, f.family, i, 0, false, {} };
    applyGroove (parsePattern (f.cells, f.slap), f.swing, f.length);
    grooveRevision.fetch_add (1);
}

namespace {
// What a user groove recalls besides its pattern, swing and length: the settings a factory groove sets.
const char* const userGrooveParameters[] { "rate", "smooth", "depth", "slapLock", "slapTime", "slapFeedback",
                                           "slapTone", "slapMix", "slapGaps", "slapWide" };
}

juce::File StutterProcessor::userGrooveRoot()
{
    if (const char* overridden = std::getenv ("STUTTER_STEP_PRESET_ROOT"); overridden != nullptr && *overridden != 0)
        return juce::File (juce::String::fromUTF8 (overridden));
   #if JUCE_MAC
    // $HOME, so a redirected home (validators, tests) never reaches the real folder.
    const char* home = std::getenv ("HOME");
    const juce::File base = home != nullptr && *home == '/' ? juce::File (juce::String::fromUTF8 (home))
                                                            : juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    return base.getChildFile ("Library").getChildFile ("Application Support").getChildFile ("STUTTER-STEP").getChildFile ("Presets");
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("STUTTER-STEP").getChildFile ("Presets");
   #endif
}

juce::Array<juce::File> StutterProcessor::userGrooveFiles()
{
    auto files = userGrooveRoot().findChildFiles (juce::File::findFiles, false, juce::String ("*") + grooveExtension);
    struct ByName { static int compareElements (const juce::File& a, const juce::File& b)
        { return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()); } };
    ByName order;
    files.sort (order);
    return files;
}

juce::String StutterProcessor::cleanGrooveName (const juce::String& typed)
{
    auto name = juce::File::createLegalFileName (typed.trim()).trim();
    while (name.startsWithChar ('.')) name = name.substring (1).trimStart();
    return name.substring (0, 48).trimEnd();
}

juce::Result StutterProcessor::saveUserGroove (const juce::String& typed)
{
    const auto clean = cleanGrooveName (typed);
    if (clean.isEmpty()) return juce::Result::fail ("Type a name for the groove.");
    const auto root = userGrooveRoot();
    if (const auto made = root.createDirectory(); made.failed()) return made;
    const bool updating = groove.presetIndex < 0 && groove.user.equalsIgnoreCase (clean);
    auto name = updating ? groove.user : clean; // an update keeps the file's name
    for (int n = 2; ! updating && root.getChildFile (name + grooveExtension).exists(); ++n)
        name = clean + " " + juce::String (n);
    const auto pattern = getPattern();
    juce::ValueTree t ("STUTTERGROOVE");
    t.setProperty ("schema", 1, nullptr);
    t.setProperty ("family", juce::String (familyName (groove.family)), nullptr);
    t.setProperty ("cells", juce::String (cellsToString (pattern)), nullptr);
    t.setProperty ("slap", juce::String (slapToString (pattern)), nullptr);
    t.setProperty ("swing", p[swingP]->load(), nullptr);
    t.setProperty ("length", p[lengthP]->load(), nullptr);
    for (const auto* id : userGrooveParameters)
        t.setProperty (id, apvts.getRawParameterValue (id)->load(), nullptr);
    const auto file = root.getChildFile (name + grooveExtension);
    const auto xml = t.createXml();
    if (xml == nullptr || ! xml->writeTo (file)) return juce::Result::fail ("Couldn't write " + file.getFullPathName());
    groove.name = name;
    groove.user = name;
    groove.presetIndex = -1;
    groove.edited = false;
    grooveRevision.fetch_add (1);
    return juce::Result::ok();
}

bool StutterProcessor::loadUserGroove (const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("STUTTERGROOVE")) return false;
    const auto t = juce::ValueTree::fromXml (*xml);
    const auto steps = t["cells"].toString(), slapSends = t["slap"].toString();
    if (steps.length() != numSteps || ! steps.containsOnly ("Xg-.") || ! slapSends.containsOnly ("*.")) return false;
    for (const auto* id : userGrooveParameters)
        if (t.hasProperty (id)) setParamValue (id, (float) (double) t[id]);
    const auto name = file.getFileNameWithoutExtension();
    groove = { name, familyFromName (t["family"].toString().toStdString()), -1, 0, false, name };
    applyGroove (parsePattern (steps.toRawUTF8(), slapSends.toRawUTF8()),
                 (float) (double) t.getProperty ("swing", 60.0) / 100.0f, (float) (double) t.getProperty ("length", 60.0) / 100.0f);
    grooveRevision.fetch_add (1);
    return true;
}

int StutterProcessor::userGrooveIndex() const
{
    if (groove.presetIndex >= 0 || groove.user.isEmpty()) return -1;
    const auto files = userGrooveFiles();
    for (int i = 0; i < files.size(); ++i)
        if (files[i].getFileNameWithoutExtension().equalsIgnoreCase (groove.user)) return i;
    return -1;
}

void StutterProcessor::stepGroove (int direction)
{
    const auto files = userGrooveFiles();
    const int total = numFactoryGrooves + files.size();
    const int user = userGrooveIndex();
    int at = groove.presetIndex >= 0 ? groove.presetIndex : user >= 0 ? numFactoryGrooves + user : (direction > 0 ? -1 : total);
    for (int tries = 0; tries < total; ++tries) {
        at = ((at + direction) % total + total) % total;
        if (at < numFactoryGrooves) { loadFactory (at); return; }
        if (loadUserGroove (files[at - numFactoryGrooves])) return; // an unreadable file is skipped
    }
}

void StutterProcessor::rollNewGroove()
{
    const auto seed = (std::uint32_t) juce::Random::getSystemRandom().nextInt64();
    const auto current = getPattern();
    const auto g = generate (seed, rollFrom, &current);
    groove = { juce::String (g.name), g.family, -1, seed % 100000u, false, {} };
    applyGroove (g.pattern, g.swing, g.length);
    grooveRevision.fetch_add (1);
}

StutterProcessor::Snapshot StutterProcessor::snapshot() const
{
    Snapshot s;
    s.pattern = getPattern();
    s.swing = p[swingP]->load() / 100.0;
    s.length = p[lengthP]->load() / 100.0;
    s.smoothMs = p[smoothP]->load();
    s.depth = p[depthP]->load() / 100.0;
    s.rate = juce::jlimit (0, 3, (int) p[rateP]->load());
    s.bpm = uiBpm.load();
    s.slapOn = p[slapOnP]->load() > 0.5f;
    s.slapLock = p[slapLockP]->load() > 0.5f;
    s.slapTimeMs = p[slapTimeP]->load();
    s.feedback = p[slapFbP]->load() / 100.0;
    s.tone = p[slapToneP]->load() / 100.0;
    s.mix = p[slapMixP]->load() / 100.0;
    s.gaps = p[slapGapsP]->load() > 0.5f;
    s.wide = p[slapWideP]->load() > 0.5f;
    s.bypassed = p[bypassP]->load() > 0.5f;
    return s;
}

void StutterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    for (int ch = getTotalNumInputChannels(); ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, numSamples);
    if (numSamples == 0 || numChannels == 0) return;

    // ---- clock: song position while playing, a free-running clock at the last tempo while stopped ----
    double bpm = uiBpm.load (std::memory_order_relaxed), beat0 = freeBeat;
    bool playing = false;
    if (auto* playHead = getPlayHead()) {
        if (const auto position = playHead->getPosition()) {
            if (const auto hostBpm = position->getBpm()) bpm = juce::jlimit (20.0, 999.0, *hostBpm);
            playing = position->getIsPlaying();
            if (playing)
                if (const auto ppq = position->getPpqPosition()) beat0 = *ppq;
        }
    }
    const int rate = juce::jlimit (0, 3, (int) p[rateP]->load());
    const double stepsPerSample = bpm / 60.0 / sampleRate / rateBeats (rate);
    const double startSteps = beat0 / rateBeats (rate);
    const double stepSeconds = 60.0 / bpm * rateBeats (rate);

    // ---- rebuild the envelopes when anything that shapes them moves ----
    const float swing = p[swingP]->load() / 100.0f, length = p[lengthP]->load() / 100.0f;
    const float smooth = p[smoothP]->load(), depth = p[depthP]->load() / 100.0f;
    const bool slapOn = p[slapOnP]->load() > 0.5f, gaps = p[slapGapsP]->load() > 0.5f;
    BuildKey key { patternVersion.load (std::memory_order_acquire), swing, length, smooth, depth, rate,
                   std::round (bpm * 100.0) / 100.0, slapOn, gaps, false };
    if (! (key == built)) {
        const auto pattern = getPattern();
        const double ramp = juce::jlimit (0.002, 0.45, (smooth / 1000.0) / stepSeconds);
        const float closed = closedGain (depth);
        gateEnv.build (pattern, swing, length, ramp, closed, false);
        if (slapOn) sendEnv.build (pattern, swing, length, ramp, 0.0f, true); else sendEnv.setConstant (0.0f);
        if (gaps) gapsEnv.buildInverseOf (gateEnv, closed); else gapsEnv.setConstant (1.0f);
        built = key;
    }

    // ---- slap settings ----
    const bool lock = p[slapLockP]->load() > 0.5f;
    const double slapSeconds = lock ? juce::jlimit (0.01, maxLockSeconds, 2.0 * swing * stepSeconds) : p[slapTimeP]->load() / 1000.0;
    SlapDelay::Settings settings;
    settings.timeLeft = (float) slapSeconds;
    settings.timeRight = (float) slapSeconds + (p[slapWideP]->load() > 0.5f ? 0.014f : 0.0f);
    settings.feedback = p[slapFbP]->load() / 100.0f;
    settings.toneHz = toneHz (p[slapToneP]->load() / 100.0);
    settings.subSafeHz = subSafeChoices[juce::jlimit (0, 3, (int) p[subSafeP]->load())];
    slap.setSettings (settings);

    const float mixTarget = p[slapMixP]->load() / 100.0f;
    const float outTarget = juce::Decibels::decibelsToGain (p[outputP]->load());
    const float bypassTarget = p[bypassP]->load() > 0.5f ? 1.0f : 0.0f;

    auto* left = buffer.getWritePointer (0);
    auto* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;
    float peakL = 0, peakR = 0;
    for (int n = 0; n < numSamples; ++n) {
        const double pos = startSteps + n * stepsPerSample;
        gainSmooth += (gateEnv.eval (pos) - gainSmooth) * smoothCoeff;
        sendSmooth += (sendEnv.eval (pos) - sendSmooth) * smoothCoeff;
        gapsSmooth += (gapsEnv.eval (pos) - gapsSmooth) * smoothCoeff;
        mixSmooth += (mixTarget - mixSmooth) * paramCoeff;
        outGain += (outTarget - outGain) * paramCoeff;
        bypassMix = bypassTarget > bypassMix ? juce::jmin (bypassTarget, bypassMix + bypassStep)
                                             : juce::jmax (bypassTarget, bypassMix - bypassStep);

        const float inL = left[n], inR = right != nullptr ? right[n] : inL;
        float wetL = 0, wetR = 0;
        slap.process ((inL + inR) * 0.5f * sendSmooth, wetL, wetR);
        const float slapGain = mixSmooth * gapsSmooth;
        float outL = (inL * gainSmooth + wetL * slapGain) * outGain;
        float outR = (inR * gainSmooth + wetR * slapGain) * outGain;
        if (bypassMix > 0.0f) { outL += (inL - outL) * bypassMix; outR += (inR - outR) * bypassMix; }
        if (right != nullptr) {
            left[n] = outL; right[n] = outR;
        } else {
            left[n] = 0.5f * (outL + outR);
        }
        peakL = juce::jmax (peakL, std::abs (outL));
        peakR = juce::jmax (peakR, std::abs (outR));
    }

    freeBeat = beat0 + numSamples * (bpm / 60.0 / sampleRate);
    const double endPos = std::fmod (startSteps + numSamples * stepsPerSample, (double) numSteps);
    uiPosition.store (endPos < 0 ? endPos + numSteps : endPos, std::memory_order_relaxed);
    uiBpm.store (bpm, std::memory_order_relaxed);
    uiPlaying.store (playing, std::memory_order_relaxed);
    uiGate.store (gainSmooth, std::memory_order_relaxed);
    uiPeakLeft.store (juce::jmax (peakL, uiPeakLeft.load (std::memory_order_relaxed)), std::memory_order_relaxed);
    uiPeakRight.store (juce::jmax (peakR, uiPeakRight.load (std::memory_order_relaxed)), std::memory_order_relaxed);
}

void StutterProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    juce::ValueTree g ("GROOVE");
    const auto pattern = getPattern();
    g.setProperty ("schema", 2, nullptr);
    g.setProperty ("cells", juce::String (cellsToString (pattern)), nullptr);
    g.setProperty ("slap", juce::String (slapToString (pattern)), nullptr);
    g.setProperty ("name", groove.name, nullptr);
    g.setProperty ("family", juce::String (familyName (groove.family)), nullptr);
    g.setProperty ("preset", groove.presetIndex, nullptr);
    g.setProperty ("seed", (juce::int64) groove.seed, nullptr);
    g.setProperty ("edited", groove.edited, nullptr);
    g.setProperty ("user", groove.user, nullptr);
    g.setProperty ("lockSwing", lockSwing, nullptr);
    g.setProperty ("rollFrom", rollFrom, nullptr);
    g.setProperty ("editorScale", editorScale, nullptr);
    state.removeChild (state.getChildWithName ("GROOVE"), nullptr);
    state.appendChild (g, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void StutterProcessor::setStateInformation (const void* data, int size)
{
    const auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.hasType (apvts.state.getType())) return;
    const auto g = state.getChildWithName ("GROOVE");
    state.removeChild (g, nullptr);
    // Schema 1 (0.1 and 0.2) had RATE as 1/8, 1/16, 1/32; 1/4 now comes first.
    if (! g.isValid() || (int) g.getProperty ("schema", 1) < 2)
        for (auto param : state)
            if (param.hasType ("PARAM") && param["id"].toString() == "rate")
                param.setProperty ("value", juce::jlimit (0, 2, (int) (double) param["value"]) + 1, nullptr);
    apvts.replaceState (state);
    if (g.isValid()) {
        setPattern (parsePattern (g["cells"].toString().toRawUTF8(), g["slap"].toString().toRawUTF8()));
        groove.name = g["name"].toString();
        groove.family = familyFromName (g["family"].toString().toStdString());
        // Factory grooves are found by name, so projects keep their preset when the bank grows or reorders.
        // A rolled groove (saved as preset -1) stays rolled whatever its name.
        const int byName = factoryIndexOf (groove.name.toStdString());
        groove.presetIndex = (int) g["preset"] >= 0 && byName >= 0 ? byName : -1;
        groove.seed = (std::uint32_t) (juce::int64) g["seed"];
        groove.edited = g["edited"];
        groove.user = groove.presetIndex < 0 ? g["user"].toString() : juce::String();
        lockSwing = g["lockSwing"];
        rollFrom = juce::jlimit (-1, (int) Family::count - 1, (int) g["rollFrom"]);
        editorScale = g["editorScale"];
    }
    grooveRevision.fetch_add (1);
}

juce::AudioProcessorEditor* StutterProcessor::createEditor()
{
#if STUTTER_HEADLESS
    return nullptr;
#else
    return new StutterEditor (*this);
#endif
}

bool StutterProcessor::hasEditor() const
{
#if STUTTER_HEADLESS
    return false;
#else
    return true;
#endif
}

#if ! STUTTER_HEADLESS
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new StutterProcessor(); }
#endif
