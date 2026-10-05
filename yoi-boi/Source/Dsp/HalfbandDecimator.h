#pragma once

#include <array>
#include <cmath>

namespace yoiboi::dsp
{
// A 65-tap linear-phase half-band FIR. Two cascaded stages turn a 4x source
// into host-rate audio with exactly 24 host samples of delay. Its fixed storage
// and symmetric sparse taps make processing allocation-free and inexpensive.
class HalfbandDecimator final
{
public:
    void prepare() noexcept
    {
        constexpr double pi = 3.14159265358979323846;
        double sum = 0.5;
        for (int tap = 0; tap < 16; ++tap)
        {
            const auto n = 2 * tap + 1;
            const auto offset = static_cast<double> (n - 32);
            const auto window = 0.42 - 0.5 * std::cos (2.0 * pi * n / 64.0)
                                      + 0.08 * std::cos (4.0 * pi * n / 64.0);
            taps[static_cast<std::size_t> (tap)] = static_cast<float> (
                std::sin (0.5 * pi * offset) / (pi * offset) * window);
            sum += 2.0 * taps[static_cast<std::size_t> (tap)];
        }
        for (auto& tap : taps) tap /= static_cast<float> (sum);
        centre = static_cast<float> (0.5 / sum);
        reset();
    }

    void reset() noexcept { delay.fill (0.0f); writePosition = 0; }

    float process (float first, float second) noexcept
    {
        delay[writePosition] = first;
        writePosition = (writePosition + 1u) & 127u;
        delay[writePosition] = second;
        const auto newest = writePosition;
        writePosition = (writePosition + 1u) & 127u;
        auto result = centre * delay[(newest - 32u) & 127u];
        for (unsigned tap = 0; tap < 16; ++tap)
        {
            const auto n = 2u * tap + 1u;
            result += taps[tap] * (delay[(newest - n) & 127u]
                                  + delay[(newest - (64u - n)) & 127u]);
        }
        return result;
    }

private:
    std::array<float, 16> taps {};
    std::array<float, 128> delay {};
    float centre = 0.5f;
    unsigned writePosition = 0;
};
} // namespace yoiboi::dsp
