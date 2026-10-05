#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>

#if defined(YOIBOI_TEST_LEGACY010)
 #include "../YoiVoice010.h"
 namespace engine = yoiboi::dsp::legacy010;
#elif defined(YOIBOI_TEST_LEGACY012)
 #include "../YoiVoice012.h"
 namespace engine = yoiboi::dsp::legacy012;
#else
 // The verification script compiles this branch against the unmodified
 // YoiVoice.h/.cpp extracted from the original release archive.
 #include "YoiVoice.h"
 namespace engine = yoiboi::dsp;
#endif

namespace
{
std::uint64_t hashSample (std::uint64_t hash, float sample)
{
    const auto bits = std::bit_cast<std::uint32_t> (sample);
    for (unsigned shift = 0; shift < 32; shift += 8)
    {
        hash ^= (bits >> shift) & 0xff;
        hash *= UINT64_C (1099511628211);
    }
    return hash;
}
}

int main()
{
    constexpr std::array<double, 3> rates { 44100.0, 48000.0, 96000.0 };
    constexpr std::array<int, 7> blocks { 7, 16, 63, 64, 127, 512, 1024 };
    constexpr std::array<float, 6> lengths { 0.08f, 0.25f, 1.5f, 2.0f, 4.0f, 12.0f };
    int completed = 0;
    for (const auto rate : rates)
        for (const auto block : blocks)
            for (int program = 0; program < 6; ++program)
            {
                engine::YoiVoice voice;
                engine::VoiceControls controls;
                controls.mouth = 0.2f + 0.15f * static_cast<float> (program);
                controls.size = 0.1f + 0.16f * static_cast<float> (program);
                controls.bite = 0.2f + 0.16f * static_cast<float> (program);
                controls.dirt = 0.17f * static_cast<float> (program);
                controls.sub = 0.19f * static_cast<float> (program);
                controls.output = program > 3 ? 1.995262f : 0.501187f;
                controls.lengthSeconds = lengths[static_cast<std::size_t> (program)];
                controls.bodyTime = 0.2f + 0.1f * static_cast<float> (program);
                controls.curve = 0.2f * static_cast<float> (program);
                controls.fmRatio = 0.5f + 0.5f * static_cast<float> (program);
                controls.fmIndex = 0.8f + 1.9f * static_cast<float> (program);
                controls.resonance = 0.2f + 0.15f * static_cast<float> (program);
                controls.startPose = program % 5;
                controls.bodyPose = (program + 3) % 5;
                controls.endPose = (program + 1) % 5;
                controls.loop = (program % 2) != 0;
                voice.setControls (controls);
                voice.prepare (rate);
                voice.noteOn (24 + 7 * program, 0.88f, 0.0f, true);

                std::uint64_t hash = UINT64_C (14695981039346656037);
                float peak = 0.0f;
                const auto samples = static_cast<int> (rate);
                for (int i = 0; i < samples; ++i)
                {
                    // Repeated control publication matches a host processing
                    // blocks. Different block lengths exercise the historical
                    // 0.1.0/0.1.1 pose-clock behavior as well as 0.1.2's fix.
                    if (i % block == 0)
                    {
                        if (program == 5)
                        {
                            const auto motion = static_cast<float> (i) / static_cast<float> (samples);
                            controls.fmRatio = 0.75f + 4.0f * motion;
                            controls.resonance = 1.0f - motion;
                            controls.dirt = 0.2f + 0.8f * motion;
                        }
                        voice.setControls (controls);
                    }
                    if (i == samples / 8)
                        voice.noteOn (31 + 7 * program, 0.72f, 41.0f, false);
                    if (i == samples / 4) voice.setPitchBend (1.65f);
                    if (i == samples * 3 / 8)
                    {
                        voice.reset();
                        voice.noteOn (24 + 7 * program, 0.91f, 0.0f, true);
                    }
                    if (i == samples * 5 / 8) voice.noteOff();
                    const auto sample = voice.renderSample();
                    if (! std::isfinite (sample))
                    {
                        std::cerr << "Non-finite audio at rate " << rate << ", block " << block
                                  << ", program " << program << ", sample " << i << '\n';
                        return 1;
                    }
                    peak = std::max (peak, std::abs (sample));
                    hash = hashSample (hash, sample);
                }
                if (peak == 0.0f)
                {
                    std::cerr << "Unexpected silence\n";
                    return 1;
                }
                hash = hashSample (hash, voice.gesturePhase());
                std::cout << static_cast<int> (rate) << ' ' << block << ' ' << program << ' '
                          << std::hex << std::setw (16) << std::setfill ('0') << hash << std::dec << '\n';
                ++completed;
            }
    std::cerr << completed << " render scenarios completed\n";
}
