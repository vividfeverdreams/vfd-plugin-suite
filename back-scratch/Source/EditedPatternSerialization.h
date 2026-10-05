#pragma once
#include "PatternEditing.h"
#include <juce_core/juce_core.h>
#include <cstdlib>

namespace backScratch {
inline std::unique_ptr<juce::XmlElement> editedPatternToXml(const ScratchPattern& pattern)
{
    auto xml = std::make_unique<juce::XmlElement>("EDITED_PATTERN");
    xml->setAttribute("format", 1);
    xml->setAttribute("beats", pattern.beats);
    xml->setAttribute("range", pattern.editingRange);
    for (int i = 0; i < pattern.count; ++i) {
        const auto& s = pattern.segments[static_cast<size_t>(i)];
        auto* node = xml->createNewChildElement("SEGMENT");
        node->setAttribute("start", s.start); node->setAttribute("end", s.end);
        node->setAttribute("delayStart", s.delayStart); node->setAttribute("delayEnd", s.delayEnd);
        node->setAttribute("gateStart", static_cast<double>(s.gateStart)); node->setAttribute("gateEnd", static_cast<double>(s.gateEnd));
        node->setAttribute("wet", static_cast<double>(s.wet)); node->setAttribute("curved", s.curved);
        node->setAttribute("curveStart", s.curveStart); node->setAttribute("curveEnd", s.curveEnd);
    }
    return xml;
}

inline bool editedPatternFromXml(const juce::XmlElement& xml, ScratchPattern& result)
{
    if (!xml.hasTagName("EDITED_PATTERN") || xml.getStringAttribute("format") != "1") return false;
    const auto number = [](const juce::XmlElement& element, const char* attribute, double& output) {
        if (!element.hasAttribute(attribute)) return false;
        const auto text = element.getStringAttribute(attribute);
        const auto* start = text.toRawUTF8();
        char* end = nullptr;
        output = std::strtod(start, &end);
        return end != start && *end == '\0' && std::isfinite(output);
    };
    ScratchPattern parsed;
    if (!number(xml, "beats", parsed.beats) || parsed.beats != 1.0
        || !number(xml, "range", parsed.editingRange)) return false;
    for (auto* node = xml.getFirstChildElement(); node != nullptr; node = node->getNextElement()) {
        if (!node->hasTagName("SEGMENT") || parsed.count >= ScratchPattern::maximumSegments) return false;
        auto& s = parsed.segments[static_cast<size_t>(parsed.count++)];
        double gateStart = 0.0, gateEnd = 0.0, wet = 0.0, curved = 0.0;
        if (!number(*node, "start", s.start) || !number(*node, "end", s.end)
            || !number(*node, "delayStart", s.delayStart) || !number(*node, "delayEnd", s.delayEnd)
            || !number(*node, "gateStart", gateStart) || !number(*node, "gateEnd", gateEnd)
            || !number(*node, "wet", wet) || !number(*node, "curved", curved)
            || !number(*node, "curveStart", s.curveStart) || !number(*node, "curveEnd", s.curveEnd)
            || (curved != 0.0 && curved != 1.0) || gateStart < 0.0 || gateStart > 1.0
            || gateEnd < 0.0 || gateEnd > 1.0 || wet < 0.0 || wet > 1.0) return false;
        s.gateStart = static_cast<float>(gateStart); s.gateEnd = static_cast<float>(gateEnd);
        s.wet = static_cast<float>(wet); s.curved = curved != 0.0;
    }
    if (!validateEditedPattern(parsed)) return false;
    result = parsed;
    return true;
}
} // namespace backScratch
