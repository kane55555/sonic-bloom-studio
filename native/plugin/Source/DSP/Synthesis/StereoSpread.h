#pragma once
//==============================================================================
//  StereoSpread.h — Haas-style ultra-short cross-channel delay + M/S width
//  control. Used per-voice to widen synth layers without phasey chorus.
//  amount: 0..1 (0 = mono, 1 = wide). Delay <= 14 ms keeps mono compat OK.
//==============================================================================
#include <vector>
#include <cmath>

namespace dida {

class StereoSpread
{
public:
    void prepare(double sr) noexcept
    {
        sampleRate = sr;
        const int maxN = (int) std::ceil(sr * 0.020); // up to 20 ms
        bufL.assign(maxN, 0.0f);
        bufR.assign(maxN, 0.0f);
        writeIdx = 0;
    }

    void setAmount(float a) noexcept
    {
        amount = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
        delaySamps = (int) (sampleRate * 0.014 * amount); // 0..14 ms
        widthGain  = amount; // M/S side scaling
    }

    inline void process(float& l, float& r) noexcept
    {
        if (amount <= 0.0001f) return;
        // Haas: delay right channel by N samples (and a touch of L into R).
        const int n = (int) bufL.size();
        bufL[writeIdx] = l;
        bufR[writeIdx] = r;
        const int rIdx = (writeIdx - delaySamps + n) % n;
        const float dl = bufL[rIdx];
        // M/S widening on the resulting pair.
        const float lH = l;
        const float rH = r * (1.0f - 0.3f * amount) + dl * 0.3f * amount;
        const float mid  = 0.5f * (lH + rH);
        const float side = 0.5f * (lH - rH) * (1.0f + widthGain);
        l = mid + side;
        r = mid - side;
        writeIdx = (writeIdx + 1) % n;
    }

private:
    double sampleRate = 44100.0;
    std::vector<float> bufL, bufR;
    int writeIdx = 0, delaySamps = 0;
    float amount = 0.0f, widthGain = 0.0f;
};

} // namespace dida
