// phase-hound engine: stereo fixes (width, bass mono, mono check, swap, polarity, output) plus the
// analysis behind the meters (correlation, per-band correlation, balance, width) and the sample
// feed for the vectorscope. Plain C++17, no JUCE, so it is unit-tested on its own.
#pragma once

#include <array>
#include <atomic>
#include <vector>
#include "Dsp.h"
#include "Fifo.h"

namespace ph
{
enum class Band { Full = 0, Low, Mid, High };

struct Params
{
    // Fixes. With all of these at their defaults the audio path is bit-exact.
    float width = 1.0f;          // side gain, 0 (mono) .. 2 (200 %)
    float bassMonoHz = 0.0f;     // 0 = off, otherwise 60..300 Hz: side below this is removed
    bool monoCheck = false;      // fold to mono, (L+R)/2 on both sides
    bool swapLR = false;
    bool invertL = false, invertR = false;
    float outputDb = 0.0f;

    // Analysis
    float xoverLowHz = 200.0f, xoverHighHz = 3000.0f;   // 3-way split for the band meters
    Band displayBand = Band::Full;                      // which band the scope is fed
};

constexpr float kBassMonoMinHz = 60.0f, kBassMonoMaxHz = 300.0f;

// Meter readings. Written by the audio thread once per block, read by the UI.
struct Status
{
    std::atomic<float> correlation { 0 };              // -1..+1, 0 when silent
    std::array<std::atomic<float>, 3> bandCorrelation {}; // low, mid, high
    std::array<std::atomic<bool>, 3> bandSilent {};
    std::atomic<float> balanceDb { 0 };                // > 0: right is louder
    std::atomic<float> widthPercent { 0 };             // 100 * rms(S) / rms(M); 0 = mono, 100 = uncorrelated
    std::atomic<float> peakL { 0 }, peakR { 0 };       // linear, per block
    std::atomic<bool> silent { true };
};

// Running normalised cross-correlation of two signals with exponential (~300 ms effective window, tau 100 ms) averaging.
struct Correlator
{
    double lr = 0, ll = 0, rr = 0, coeff = 0.999;

    void prepare (double sampleRate, double tauSeconds = 0.1) { coeff = std::exp (-1.0 / (tauSeconds * sampleRate)); reset(); }
    void reset() { lr = ll = rr = 0; }
    inline void push (double l, double r)
    {
        const double k = 1.0 - coeff;
        lr += k * (l * r - lr);
        ll += k * (l * l - ll);
        rr += k * (r * r - rr);
    }
    // Mean square below about -80 dBFS counts as silence.
    bool isSilent() const { return ll + rr < 2.0e-8; }
    float value() const
    {
        if (isSilent()) return 0.0f;
        const double d = std::sqrt (ll * rr);
        if (d < 1.0e-12) return 0.0f;   // one side silent: no defined phase relation
        return (float) std::clamp (lr / d, -1.0, 1.0);
    }
};

class Engine
{
public:
    Engine();

    void prepare (double sampleRate, int maxBlock);
    void reset();
    void setParams (const Params& p);
    const Params& getParams() const { return params; }

    // in/out may alias. numChannels is 1 or 2 (mono: fixes that need two channels are skipped).
    void process (const float* const* in, float* const* out, int numChannels, int numSamples);

    const Status& getStatus() const { return status; }
    StereoFifo& getScopeFifo() { return fifo; }

    // True when the last block took the bit-exact path (all fixes neutral and settled).
    bool wasBypassed() const { return lastBlockBypassed; }

    static float bassMonoFromKnob (float hz) { return hz < kBassMonoMinHz ? 0.0f : std::min (hz, kBassMonoMaxHz); }

private:
    void updateFilters();
    void analyse (const float* l, const float* r, int n);

    Params params;
    double sr = 48000.0;

    Ramp widthRamp, gainRamp, monoRamp, bassRamp;
    LR4Split bassSide, bassMid;
    float bassHzSet = -1.0f;

    LR4Split bandL1, bandR1, bandL2, bandR2;
    float xLowSet = -1, xHighSet = -1;
    Correlator full;
    std::array<Correlator, 3> bands;
    double msM = 0, msS = 0;

    std::vector<float> scopeL, scopeR;
    StereoFifo fifo;
    Status status;
    bool lastBlockBypassed = true;
};
} // namespace ph
