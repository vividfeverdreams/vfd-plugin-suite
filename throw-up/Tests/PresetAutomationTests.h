#pragma once
#include "ThrowUpProcessor.h"
#include "ThrowUpEditor.h"
#include "Presets.h"
#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>

namespace throwup::tests {

inline void requireAutomation(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

// Observe the entire AudioProcessor notification stream, not just THROW's value.
// A program-wide refresh can override automation even without changing that value.
class PresetHostObserver final : private juce::AudioProcessorListener
{
public:
    explicit PresetHostObserver(ThrowUpProcessor& p) : processor(p) { processor.addListener(this); }
    ~PresetHostObserver() override { processor.removeListener(this); }
    void clear() { changes.fill(0); begins.fill(0); ends.fill(0); program = metadata = parameterInfo = latency = 0; }
    std::array<int, ThrowUpProcessor::parameterCount> changes {}, begins {}, ends {};
    int program = 0, metadata = 0, parameterInfo = 0, latency = 0;
private:
    void audioProcessorParameterChanged(juce::AudioProcessor*, int index, float) override
    {
        requireAutomation(index >= 0 && index < ThrowUpProcessor::parameterCount, "preset emitted unknown parameter notification");
        ++changes[static_cast<size_t>(index)];
    }
    void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int index) override
    {
        requireAutomation(index >= 0 && index < ThrowUpProcessor::parameterCount, "preset began unknown parameter gesture");
        ++begins[static_cast<size_t>(index)];
    }
    void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int index) override
    {
        requireAutomation(index >= 0 && index < ThrowUpProcessor::parameterCount, "preset ended unknown parameter gesture");
        ++ends[static_cast<size_t>(index)];
    }
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details) override
    {
        program += details.programChanged ? 1 : 0;
        metadata += details.nonParameterStateChanged ? 1 : 0;
        parameterInfo += details.parameterInfoChanged ? 1 : 0;
        latency += details.latencyChanged ? 1 : 0;
    }
    ThrowUpProcessor& processor;
};

inline void runPresetAutomationRegressionTests()
{
    ThrowUpProcessor processor;
    ThrowUpEditor editor(processor);
    PresetHostObserver host(processor);
    constexpr std::array<const char*, 13> expectedIds { "mix", "size", "decay", "predelay", "lowCut", "highCut",
        "diffusion", "motion", "width", "grit", "duck", "output", "bypass" };
    requireAutomation(processor.getParameters().size() == ThrowUpProcessor::parameterCount,
                      "parameter count changed and could break session automation");
    // The 13 parameters of 0.1.5 keep their IDs and order; 0.2.0 parameters are appended.
    for (size_t i = 0; i < expectedIds.size(); ++i)
        requireAutomation(processor.getParameters()[static_cast<int>(i)] == processor.apvts.getParameter(expectedIds[i]),
                          "parameter IDs or ordering changed and could break session automation");
    const auto roomIds = ThrowUpProcessor::roomParameterIds();
    const auto indexOf = [&](const juce::String& id) {
        for (int i = 0; i < processor.getParameters().size(); ++i)
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(processor.getParameters()[i]); ranged && ranged->getParameterID() == id) return i;
        return -1;
    };

    const auto parameterValue = [&](const char* id) { return processor.apvts.getRawParameterValue(id)->load(); };
    const auto setParameter = [&](const char* id, float value) {
        auto* parameter = processor.apvts.getParameter(id);
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    };
    juce::Slider* throwKnob = nullptr;
    juce::ComboBox* presetMenu = nullptr;
    juce::Button* previous = nullptr;
    juce::Button* next = nullptr;
    for (auto* component : editor.getChildren()) {
        if (component->getName() == "THROW") throwKnob = dynamic_cast<juce::Slider*>(component);
        if (component->getName() == "Factory presets") presetMenu = dynamic_cast<juce::ComboBox*>(component);
        if (component->getName() == "Previous preset") previous = dynamic_cast<juce::Button*>(component);
        if (component->getName() == "Next preset") next = dynamic_cast<juce::Button*>(component);
    }
    requireAutomation(throwKnob && presetMenu && previous && next, "preset/THROW UI controls missing");
    setParameter("output", -3.4f);
    setParameter("bypass", 1.0f);

    const auto verifyPresetNotification = [&](int index, float mix, bool metadataChanged) {
        requireAutomation(processor.getCurrentProgram() == index, "preset UI selected wrong room");
        requireAutomation(std::abs(parameterValue("mix") - mix) < .02f
            && std::abs(parameterValue("output") + 3.4f) < .02f && parameterValue("bypass") == 1.0f,
            "preset overwrote THROW, output, or bypass");
        requireAutomation(std::abs(throwKnob->getValue() - mix) < .02, "preset changed the actual THROW control");
        for (const int protectedIndex : { 0, 11, 12 })
            requireAutomation(host.changes[static_cast<size_t>(protectedIndex)] == 0
                && host.begins[static_cast<size_t>(protectedIndex)] == 0
                && host.ends[static_cast<size_t>(protectedIndex)] == 0,
                "preset sent an edit or gesture for THROW, output, or bypass");
        for (size_t i = 0; i < host.changes.size(); ++i)
            requireAutomation(host.changes[i] == host.begins[i] && host.changes[i] == host.ends[i],
                              "room parameter edits did not have matching begin/end gestures");
        for (size_t i = 0; i < host.changes.size(); ++i)
            if (host.changes[i] != 0)
                requireAutomation(roomIds.contains(dynamic_cast<juce::RangedAudioParameter*>(processor.getParameters()[static_cast<int>(i)])->getParameterID()),
                                  "a preset edited a control that is not part of a room");
        requireAutomation(host.program == 0 && host.parameterInfo == 0 && host.latency == 0,
                          "preset emitted global program/parameter refresh that can override THROW automation");
        requireAutomation(host.metadata == (metadataChanged ? 1 : 0), "preset metadata save notification incorrect");
        const auto& r = factoryPresets()[static_cast<size_t>(index)].room;
        const std::array<float, 18> expected { r.size, r.decay, r.predelay, r.lowCut, r.highCut, r.diffusion, r.motion,
            r.width, r.grit, r.duck, static_cast<float>(r.predelaySync), static_cast<float>(r.predelayFeel), r.shimmer,
            static_cast<float>(r.shimmerPitch), static_cast<float>(r.swell), r.duckRelease, r.duckThreshold,
            r.version == SoundVersion::v015 ? 0.0f : 1.0f };
        for (int i = 0; i < roomIds.size(); ++i) {
            requireAutomation(indexOf(roomIds[i]) >= 0, "room parameter missing");
            requireAutomation(std::abs(parameterValue(roomIds[i].toRawUTF8()) - expected[static_cast<size_t>(i)]) < .051f,
                              "preset no longer loads its room controls");
        }
    };

    const int programs = processor.getNumPrograms();
    const auto stepped = [&](int index, int direction) {
        const int start = index >= legacyPresetStart() ? legacyPresetStart() : 0;
        const int count = index >= legacyPresetStart() ? legacyPresetCount : legacyPresetStart();
        return start + (index - start + direction + count) % count;
    };
    for (int i = 0; i < programs; ++i) {
        const float automatedThrow = static_cast<float>(i % 50) * 1.91f;
        setParameter("mix", automatedThrow);
        const int oldProgram = processor.getCurrentProgram();
        host.clear();
        // Re-select through the same callback if the combo is already on this item.
        presetMenu->setSelectedId(i + 1, juce::dontSendNotification);
        presetMenu->onChange();
        verifyPresetNotification(i, automatedThrow, oldProgram != i);
    }
    for (int direction : { -1, 1 }) {
        for (int i = 0; i < programs; ++i) {
            const float automatedThrow = 99.0f - static_cast<float>(i % 50) * 1.91f;
            setParameter("mix", automatedThrow);
            const int expected = stepped(processor.getCurrentProgram(), direction);
            host.clear();
            (direction < 0 ? previous : next)->onClick();
            verifyPresetNotification(expected, automatedThrow, true);
        }
    }
    std::cout << "PASS all " << programs << " preset menu/previous/next choices preserve THROW values and host automation notifications\n";

    // The dice key loads through the same path, never re-picks the loaded room and
    // stays in the bank the arrows browse. (The user-presets root is an empty scratch folder here.)
    juce::Button* dice = nullptr;
    for (auto* component : editor.getChildren())
        if (component->getName() == "Random preset") dice = dynamic_cast<juce::Button*>(component);
    requireAutomation(dice != nullptr && dice->getTitle() == "Random preset" && dice->getTooltip().startsWith("Random preset")
                      && ! dice->getWantsKeyboardFocus(), // suite host-keys rule: keys never take the keyboard
                      "random preset key missing, unnamed, or able to take the keyboard");
    for (const int bankStart : { 0, legacyPresetStart() }) {
        const bool legacyBank = bankStart > 0;
        const int bankSize = legacyBank ? legacyPresetCount : legacyPresetStart();
        processor.loadFactoryPreset(bankStart);
        std::set<int> picked;
        for (int i = 0; i < 400; ++i) {
            const float automatedThrow = static_cast<float>(i % 53) * 1.87f;
            setParameter("mix", automatedThrow);
            const int before = processor.getCurrentProgram();
            host.clear();
            dice->onClick();
            const int after = processor.getCurrentProgram();
            requireAutomation(after != before, "random preset re-picked the room already loaded");
            requireAutomation((after >= legacyPresetStart()) == legacyBank, "random preset left the bank the arrows browse");
            verifyPresetNotification(after, automatedThrow, true);
            requireAutomation(presetMenu->getSelectedId() == after + 1 && processor.getUserPresetName().isEmpty(),
                              "random preset is not shown on the preset display");
            picked.insert(after);
        }
        requireAutomation(static_cast<int>(picked.size()) > bankSize * 3 / 4, "random preset does not draw across the bank");
    }
    std::cout << "PASS 800 random-preset picks load like the menu, keep THROW, never repeat the loaded room and stay in their bank\n";

    host.clear();
    processor.loadFactoryPreset(processor.getCurrentProgram());
    for (size_t i = 0; i < host.changes.size(); ++i)
        requireAutomation(host.changes[i] == 0 && host.begins[i] == 0 && host.ends[i] == 0,
                          "unchanged preset emitted redundant parameter edit");
    requireAutomation(host.metadata == 0 && host.program == 0, "unchanged preset emitted redundant metadata edit");

    for (int i = 0; i < programs; ++i) {
        const int oldProgram = processor.getCurrentProgram();
        host.clear();
        processor.setCurrentProgram(i);
        verifyPresetNotification(i, parameterValue("mix"), oldProgram != i);
    }
    std::cout << "PASS host program changes stay scoped to room controls and no-op presets emit no edits\n";

    // Exercise the slider's real double-click reset handler: manual THROW edits
    // must still send their normal gesture, so DAW automation can be recorded.
    setParameter("mix", 64.0f);
    host.clear();
    const auto now = juce::Time::getCurrentTime();
    const auto point = throwKnob->getLocalBounds().getCentre().toFloat();
    const juce::MouseEvent doubleClick(juce::Desktop::getInstance().getMainMouseSource(), point,
        juce::ModifierKeys::leftButtonModifier, 1.0f, 0, 0, 0, 0, throwKnob, throwKnob,
        now, point, now, 2, false);
    throwKnob->mouseDoubleClick(doubleClick);
    requireAutomation(host.begins[0] == 1 && host.changes[0] == 1 && host.ends[0] == 1
        && parameterValue("mix") == 0.0f, "actual THROW double-click lost its host automation gesture");
    host.clear();
    {
        juce::Slider::ScopedDragNotification drag(*throwKnob);
        throwKnob->setValue(63.25, juce::sendNotificationSync);
        throwKnob->setValue(71.5, juce::sendNotificationSync);
    }
    requireAutomation(host.begins[0] == 1 && host.changes[0] == 2 && host.ends[0] == 1,
                      "actual THROW drag attachment lost its host automation gesture");
    std::cout << "PASS manual THROW double-click and slider drag retain normal automation gestures\n";

    setParameter("mix", 37.42f);
    setParameter("decay", 7.31f);
    juce::MemoryBlock state;
    processor.getStateInformation(state);
    setParameter("mix", 89.0f);
    processor.loadFactoryPreset(0);
    host.clear();
    processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    requireAutomation(processor.getCurrentProgram() == programs - 1 && std::abs(parameterValue("mix") - 37.42f) < .02f
        && std::abs(parameterValue("decay") - 7.31f) < .02f
        && std::abs(throwKnob->getValue() - 37.42) < .02,
        "project restore lost saved THROW, custom room, preset, or knob display");
    for (size_t i = 0; i < host.begins.size(); ++i)
        requireAutomation(host.begins[i] == 0 && host.ends[i] == 0,
                          "project restore echoed a user automation gesture");
    requireAutomation(host.program == 0 && host.metadata == 0 && host.parameterInfo == 0 && host.latency == 0,
                      "project restore echoed an unnecessary global host update");
    std::cout << "PASS full session recall restores saved THROW without emitting user gestures or program refresh\n";
}

} // namespace throwup::tests
