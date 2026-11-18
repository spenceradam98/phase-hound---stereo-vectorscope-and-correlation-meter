// Small DSP building blocks for phase-hound. Plain C++17, no JUCE, scalar only (no AVX2).
#pragma once

#include <algorithm>
#include <cmath>

namespace ph
{
constexpr double kPi = 3.14159265358979323846;

inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
inline float gainToDb (float g) { return g > 1.0e-6f ? 20.0f * std::log10 (g) : -120.0f; }

// Transposed direct form II biquad, double state so low crossovers stay clean at 96 kHz+.
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }

    // 2nd-order Butterworth (Q = 1/sqrt 2). Two in series make a 4th-order Linkwitz-Riley.
    void setButterworth (bool highPass, double hz, double sampleRate)
    {
        hz = std::clamp (hz, 10.0, sampleRate * 0.45);
        const double w = 2.0 * kPi * hz / sampleRate;
        const double c = std::cos (w), s = std::sin (w);
        const double alpha = s / (2.0 * 0.7071067811865476);
        const double a0 = 1.0 + alpha;
        if (highPass) { b0 = (1.0 + c) * 0.5; b1 = -(1.0 + c); b2 = b0; }
        else          { b0 = (1.0 - c) * 0.5; b1 = 1.0 - c;    b2 = b0; }
        b0 /= a0; b1 /= a0; b2 /= a0;
        a1 = -2.0 * c / a0;
        a2 = (1.0 - alpha) / a0;
    }

    inline double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// 4th-order Linkwitz-Riley split at one frequency: low + high sums to an allpass (flat magnitude),
// and both outputs have the same phase, so L/R correlation inside a band is preserved.
struct LR4Split
{
    Biquad lp1, lp2, hp1, hp2;

    void set (double hz, double sampleRate)
    {
        lp1.setButterworth (false, hz, sampleRate); lp2 = copyCoeffs (lp2, lp1);
        hp1.setButterworth (true, hz, sampleRate);  hp2 = copyCoeffs (hp2, hp1);
    }
    void reset() { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }

    inline void process (double x, double& low, double& high)
    {
        low = lp2.process (lp1.process (x));
        high = hp2.process (hp1.process (x));
    }

private:
    static Biquad copyCoeffs (Biquad dst, const Biquad& src)
    {
        dst.b0 = src.b0; dst.b1 = src.b1; dst.b2 = src.b2; dst.a1 = src.a1; dst.a2 = src.a2;
        return dst;
    }
};

// Linear ramp towards a target over a fixed time. Lands exactly on the target, so the
// engine can tell when it is settled and take the bit-exact bypass path.
struct Ramp
{
    float value = 0, target = 0, step = 0;
    int remaining = 0, length = 1;

    void prepare (double sampleRate, double seconds) { length = std::max (1, (int) (sampleRate * seconds)); }
    void snap (float v) { value = target = v; remaining = 0; }
    void setTarget (float t)
    {
        if (! (t < target || t > target)) return;   // exact match (written this way to keep -Wfloat-equal quiet)
        target = t;
        remaining = length;
        step = (target - value) / (float) length;
    }
    bool settled() const { return remaining == 0; }
    inline float next()
    {
        if (remaining > 0)
        {
            value += step;
            if (--remaining == 0) value = target;
        }
        return value;
    }
};
} // namespace ph
