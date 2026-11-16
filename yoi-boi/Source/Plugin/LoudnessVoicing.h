#pragma once
#include "ParameterMap.h"
#include <vector>
namespace yoiboi
{
// Shared standard 140 BPM demo phrase; pure DSP, no processor/editor/window.
struct PhraseRender { std::vector<float> left, right; double lufs = -120, seconds = 0; float peak = 0; };
PhraseRender renderVoicingPhrase (const Patch& patch);
// Called on the message/bounce thread only, AFTER locks/exclusions resolve.
// A single un-clipped probe estimates the trim; factory bake uses measured iteration.
void voicePatch (Patch& patch, bool precise);
void voiceGeneratedPatch (Patch& patch);
}
