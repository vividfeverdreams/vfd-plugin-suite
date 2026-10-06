#include "ParameterMap.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>

namespace yoiboi
{
float lengthFeelMultiplier (float feel) noexcept
{
    const auto index = std::clamp (static_cast<int> (feel), 0, 2);
    return index == 1 ? 2.0f / 3.0f : index == 2 ? 1.5f : 1.0f;
}

ParameterValues parameterValuesFor (const Patch& patch)
{
    ParameterValues v {};
    const auto flag = [] (bool value) { return value ? 1.0f : 0.0f; };
    v[pMouth] = patch.mouth; v[pSize] = patch.size; v[pBite] = patch.bite; v[pDirt] = patch.dirt; v[pSub] = patch.sub;
    v[pLengthMs] = patch.freeLengthMilliseconds;
    v[pLengthDivision] = static_cast<float> (patch.lengthDivision);
    v[pSync] = flag (patch.tempoSync);
    v[pLoop] = flag (patch.loop);
    v[pGlideMs] = patch.glideMilliseconds;
    v[pOutputDb] = patch.outputDecibels;
    v[pStartPose] = static_cast<float> (patch.startPose);
    v[pBodyPose] = static_cast<float> (patch.bodyPose);
    v[pEndPose] = static_cast<float> (patch.endPose);
    v[pBodyTime] = patch.bodyTime;
    v[pCurve] = patch.curve;
    v[pSubMode] = static_cast<float> (patch.subMode);
    v[pLowCut] = flag (patch.lowCut);
    v[pLengthFeel] = static_cast<float> (patch.lengthFeel);
    v[pTalkMode] = static_cast<float> (patch.talkMode);
    v[pTalkRate] = static_cast<float> (patch.talkDivision);
    v[pTalkFeel] = static_cast<float> (patch.talkFeel);
    v[pBendRange] = static_cast<float> (patch.bendRange);
    v[pGlideMode] = static_cast<float> (patch.glideMode);
    v[pPitchEnv] = patch.pitchEnvSemitones;
    v[pPitchEnvMs] = patch.pitchEnvMilliseconds;
    v[pWidth] = patch.width;
    v[pSubReset] = flag (patch.subPhaseReset);
    v[pShaper] = static_cast<float> (patch.shaper);
    v[pSquash] = patch.squash;
    v[pVowelStart] = static_cast<float> (patch.vowelStart);
    v[pVowelBody] = static_cast<float> (patch.vowelBody);
    v[pVowelEnd] = static_cast<float> (patch.vowelEnd);
    v[pOnsetGlide] = static_cast<float> (patch.onsetGlide);
    v[pConsonant] = static_cast<float> (patch.consonant);
    v[pConsonantLevel] = patch.consonantLevel;
    v[pVelocityMouth] = patch.velocityToMouth;
    v[pVelocityBite] = patch.velocityToBite;
    v[pVelocityDirt] = patch.velocityToDirt;
    v[pAttackMs] = patch.attackMilliseconds;
    v[pReleaseMs] = patch.releaseMilliseconds;
    v[pMeterBars] = flag (patch.meterBars);
    return v;
}

dsp::LatestControls controlsFor (const ParameterValues& v, const HostTiming& timing, int engine, double sampleRate,
                                 float fmRatio, float fmIndex, float resonance)
{
    dsp::LatestControls c;
    // Exactly the 0.1.6 mapping for the original controls.
    c.mouth = v[pMouth]; c.size = v[pSize]; c.bite = v[pBite];
    c.dirt = v[pDirt]; c.sub = v[pSub];
    c.subMode = static_cast<int> (v[pSubMode]); c.lowCut = v[pLowCut] > 0.5f;
    c.fmRatio = fmRatio;
    c.fmIndex = fmIndex;
    c.resonance = resonance;
    c.startPose = static_cast<int> (v[pStartPose]);
    c.bodyPose = static_cast<int> (v[pBodyPose]);
    c.endPose = static_cast<int> (v[pEndPose]);
    c.bodyTime = v[pBodyTime]; c.curve = v[pCurve];
    c.loop = v[pLoop] > 0.5f;
    c.output = juce::Decibels::decibelsToGain (v[pOutputDb]);
    const double bpm = timing.hasBpm ? timing.bpm : 120.0;
    const bool latest = engine >= Patch::firstExtendedEngine;
    // Opt-in (engine 4): "1 Bar" spans the host's real bar, e.g. 3 beats in 3/4.
    const bool meterBars = latest && v[pMeterBars] > 0.5f;
    const float barBeats = meterBars ? static_cast<float> (std::clamp (timing.numerator, 1, 32) * 4.0
                                                            / std::clamp (timing.denominator, 1, 32))
                                     : syncedBeats[4];
    if (v[pSync] > 0.5f)
    {
        const int division = std::clamp (static_cast<int> (v[pLengthDivision]), 0, 4);
        const float beats = division == 4 ? barBeats : syncedBeats[static_cast<size_t> (division)];
        c.lengthSeconds = std::clamp (static_cast<float> (60.0 / std::max (20.0, bpm))
            * beats * lengthFeelMultiplier (v[pLengthFeel]), .01f, 18.0f);
    }
    else c.lengthSeconds = v[pLengthMs] * 0.001f;
    if (! latest) return c;

    c.talkMode = std::clamp (static_cast<int> (v[pTalkMode]), 0, 3);
    const int talkDivision = std::clamp (static_cast<int> (v[pTalkRate]), 0, 4);
    const double cycleBeats = static_cast<double> (talkDivision == 4 ? barBeats : syncedBeats[static_cast<size_t> (talkDivision)])
                              * lengthFeelMultiplier (v[pTalkFeel]);
    c.talkIncrement = std::max (20.0, bpm) / 60.0 / std::max (8000.0, sampleRate) / cycleBeats;
    // Phase-locked to the host's position while it plays; free-running per note otherwise.
    c.talkSyncPhase = timing.playing && timing.hasPpq && std::isfinite (timing.ppq)
                    ? std::fmod (std::fmod (timing.ppq, cycleBeats) + cycleBeats, cycleBeats) / cycleBeats
                    : -1.0;
    c.glideMode = std::clamp (static_cast<int> (v[pGlideMode]), 0, 1);
    c.pitchEnvSemitones = v[pPitchEnv];
    c.pitchEnvSeconds = v[pPitchEnvMs] * 0.001f;
    c.width = v[pWidth];
    c.subPhaseReset = v[pSubReset] > 0.5f;
    c.shaper = std::clamp (static_cast<int> (v[pShaper]), 0, 3);
    c.squash = v[pSquash];
    c.vowelStart = std::clamp (static_cast<int> (v[pVowelStart]), 0, vowelCount - 1);
    c.vowelBody = std::clamp (static_cast<int> (v[pVowelBody]), 0, vowelCount - 1);
    c.vowelEnd = std::clamp (static_cast<int> (v[pVowelEnd]), 0, vowelCount - 1);
    c.onsetGlide = std::clamp (static_cast<int> (v[pOnsetGlide]), 0, 2);
    c.consonant = std::clamp (static_cast<int> (v[pConsonant]), 0, 3);
    c.consonantLevel = v[pConsonantLevel];
    c.velocityToMouth = v[pVelocityMouth];
    c.velocityToBite = v[pVelocityBite];
    c.velocityToDirt = v[pVelocityDirt];
    c.attackSeconds = v[pAttackMs] * 0.001f;
    c.releaseSeconds = v[pReleaseMs] * 0.001f;
    return c;
}
} // namespace yoiboi
