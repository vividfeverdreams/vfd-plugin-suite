#pragma once
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
namespace yoiboi::dsp
{
// BS.1770-4 stereo K weighting + BS.1770/EBU R128 integrated gating:
// 400 ms blocks, 100 ms hop, absolute -70 LUFS, relative -10 LU.
// Offline 48 kHz meter. Stores one energy per 100 ms, never used on audio thread.
class IntegratedLoudness
{
public:
    void push (float left, float right)
    {
        double sum = 0;
        const double input[] { left, right };
        for (unsigned c = 0; c < 2; ++c)
        {
            double x = input[c];
            constexpr double coeff[2][5] {
                { 1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, .73248077421585 },
                { 1, -2, 1, -1.99004745483398, .99007225036621 } };
            for (unsigned s = 0; s < 2; ++s)
            {
                auto& z = state[c][s]; const auto& b = coeff[s];
                const double y = b[0] * x + z[0];
                z[0] = b[1] * x - b[3] * y + z[1]; z[1] = b[2] * x - b[4] * y; x = y;
            }
            sum += x * x;
        }
        hopEnergy += sum;
        if (++count == 4800)
        {
            hops.push_back (hopEnergy / 4800); hopEnergy = 0; count = 0;
            if (hops.size() >= 4)
            {
                double block = 0;
                for (unsigned i = 0; i < 4; ++i) block += hops[hops.size() - 1 - i] / 4;
                blocks.push_back (block);
            }
        }
    }
    double integrated() const
    {
        constexpr double absolute = 1.172465304582298e-7; // 10^((-70+.691)/10)
        double energy = 0; int n = 0;
        for (auto x : blocks) if (x >= absolute) { energy += x; ++n; }
        if (n == 0) return -120;
        const double relative = .1 * energy / n;
        energy = 0; n = 0;
        for (auto x : blocks) if (x >= std::max (absolute, relative)) { energy += x; ++n; }
        return n > 0 ? -.691 + 10 * std::log10 (energy / n) : -120;
    }
private:
    std::array<std::array<std::array<double, 2>, 2>, 2> state {};
    std::vector<double> hops, blocks;
    double hopEnergy = 0;
    int count = 0;
};
}
