#include "Engine.h"

namespace ph
{
Engine::Engine() : fifo (1 << 15)
{
    for (auto& b : status.bandCorrelation) b.store (0);
    for (auto& b : status.bandSilent) b.store (true);
}

void Engine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    widthRamp.prepare (sr, 0.03);
    gainRamp.prepare (sr, 0.03);
    monoRamp.prepare (sr, 0.01);
    bassRamp.prepare (sr, 0.03);
    full.prepare (sr);
    for (auto& b : bands) b.prepare (sr);
    scopeL.assign ((size_t) std::max (64, maxBlock), 0.0f);
    scopeR.assign ((size_t) std::max (64, maxBlock), 0.0f);
    bassHzSet = xLowSet = xHighSet = -1;
    reset();
}

void Engine::reset()
{
    widthRamp.snap (params.width);
    gainRamp.snap (dbToGain (params.outputDb));
    monoRamp.snap (params.monoCheck ? 1.0f : 0.0f);
    bassRamp.snap (params.bassMonoHz > 0 ? 1.0f : 0.0f);
    bassSide.reset(); bassMid.reset();
    bandL1.reset(); bandR1.reset(); bandL2.reset(); bandR2.reset();
    full.reset();
    for (auto& b : bands) b.reset();
    msM = msS = 0;
    updateFilters();
}

void Engine::setParams (const Params& p)
{
    params = p;
    params.width = std::clamp (params.width, 0.0f, 2.0f);
    params.bassMonoHz = params.bassMonoHz > 0 ? std::clamp (params.bassMonoHz, kBassMonoMinHz, kBassMonoMaxHz) : 0.0f;
    params.outputDb = std::clamp (params.outputDb, -60.0f, 24.0f);
    params.xoverLowHz = std::clamp (params.xoverLowHz, 40.0f, 1000.0f);
    params.xoverHighHz = std::clamp (params.xoverHighHz, std::max (1000.0f, params.xoverLowHz * 2.0f), 12000.0f);

    widthRamp.setTarget (params.width);
    // exactly 1.0 at 0 dB so the bypass test can compare with ==
    gainRamp.setTarget (params.outputDb == 0.0f ? 1.0f : dbToGain (params.outputDb));
    monoRamp.setTarget (params.monoCheck ? 1.0f : 0.0f);
    const float bassTarget = params.bassMonoHz > 0 ? 1.0f : 0.0f;
    if (bassTarget > 0 && bassRamp.value == 0.0f && bassRamp.target == 0.0f)
    {
        bassSide.reset(); bassMid.reset();   // start the filters clean when switching on
    }
    bassRamp.setTarget (bassTarget);
    updateFilters();
}

void Engine::updateFilters()
{
    if (params.bassMonoHz > 0 && params.bassMonoHz != bassHzSet)
    {
        bassSide.set (params.bassMonoHz, sr);
        bassMid.set (params.bassMonoHz, sr);
        bassHzSet = params.bassMonoHz;
    }
    if (params.xoverLowHz != xLowSet || params.xoverHighHz != xHighSet)
    {
        bandL1.set (params.xoverLowHz, sr); bandR1.set (params.xoverLowHz, sr);
        bandL2.set (params.xoverHighHz, sr); bandR2.set (params.xoverHighHz, sr);
        xLowSet = params.xoverLowHz; xHighSet = params.xoverHighHz;
    }
}

void Engine::process (const float* const* in, float* const* out, int numChannels, int numSamples)
{
    if (numSamples <= 0 || numChannels <= 0) return;
    const bool stereo = numChannels > 1;
    const float* inL = in[0];
    const float* inR = stereo ? in[1] : in[0];
    float* outL = out[0];
    float* outR = stereo ? out[1] : nullptr;

    const bool msNeutral = widthRamp.settled() && widthRamp.value == 1.0f
                        && gainRamp.settled() && gainRamp.value == 1.0f
                        && monoRamp.settled() && monoRamp.value == 0.0f
                        && bassRamp.settled() && bassRamp.value == 0.0f;

    if (! stereo)
    {
        // Mono track: only Output and polarity make sense.
        const float sign = params.invertL ? -1.0f : 1.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = gainRamp.next();
            outL[i] = msNeutral ? sign * inL[i] : sign * inL[i] * g;
        }
        widthRamp.snap (widthRamp.target); monoRamp.snap (monoRamp.target); bassRamp.snap (bassRamp.target);
        lastBlockBypassed = msNeutral && ! params.invertL;
        analyse (outL, outL, numSamples);
        return;
    }

    const float signL = params.invertL ? -1.0f : 1.0f, signR = params.invertR ? -1.0f : 1.0f;
    const bool swap = params.swapLR;

    if (msNeutral)
    {
        // Bit-exact path: negation and swapping are exact, nothing else touches the samples.
        for (int i = 0; i < numSamples; ++i)
        {
            const float l = signL * inL[i], r = signR * inR[i];
            outL[i] = swap ? r : l;
            outR[i] = swap ? l : r;
        }
        lastBlockBypassed = ! swap && ! params.invertL && ! params.invertR;
    }
    else
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float l = signL * inL[i], r = signR * inR[i];
            if (swap) std::swap (l, r);
            double m = 0.5 * ((double) l + r), s = 0.5 * ((double) l - r);

            const float bass = bassRamp.next();
            if (bass > 0.0f || ! bassRamp.settled())
            {
                double sLow, sHigh, mLow, mHigh;
                bassSide.process (s, sLow, sHigh);
                bassMid.process (m, mLow, mHigh);
                // side keeps only what is above the crossover; mid goes through the matching
                // allpass (low + high) so mid and side stay phase-aligned above the crossover
                s += bass * (sHigh - s);
                m += bass * ((mLow + mHigh) - m);
            }

            s *= widthRamp.next();
            double L = m + s, R = m - s;
            const float mono = monoRamp.next();
            if (mono > 0.0f)
            {
                L += mono * (m - L);
                R += mono * (m - R);
            }
            const float g = gainRamp.next();
            outL[i] = (float) (L * g);
            outR[i] = (float) (R * g);
        }
        lastBlockBypassed = false;
    }

    analyse (outL, outR, numSamples);
}

void Engine::analyse (const float* l, const float* r, int n)
{
    float pkL = 0, pkR = 0;
    const double k = 1.0 - full.coeff;
    const int chunk = (int) scopeL.size();
    for (int start = 0; start < n; start += chunk)
    {
        const int len = std::min (chunk, n - start);
        for (int j = 0; j < len; ++j)
        {
            const int i = start + j;
            const double L = l[i], R = r[i];
            pkL = std::max (pkL, std::abs (l[i]));
            pkR = std::max (pkR, std::abs (r[i]));
            full.push (L, R);
            const double m = 0.5 * (L + R), s = 0.5 * (L - R);
            msM += k * (m * m - msM);
            msS += k * (s * s - msS);

            double lowL, restL, midL, highL, lowR, restR, midR, highR;
            bandL1.process (L, lowL, restL); bandL2.process (restL, midL, highL);
            bandR1.process (R, lowR, restR); bandR2.process (restR, midR, highR);
            bands[0].push (lowL, lowR);
            bands[1].push (midL, midR);
            bands[2].push (highL, highR);

            double sl = L, sr2 = R;
            switch (params.displayBand)
            {
                case Band::Low:  sl = lowL;  sr2 = lowR;  break;
                case Band::Mid:  sl = midL;  sr2 = midR;  break;
                case Band::High: sl = highL; sr2 = highR; break;
                case Band::Full: break;
            }
            scopeL[(size_t) j] = (float) sl;
            scopeR[(size_t) j] = (float) sr2;
        }
        fifo.push (scopeL.data(), scopeR.data(), len);
    }

    // flush denormal-range state
    if (msM < 1.0e-30) msM = 0;
    if (msS < 1.0e-30) msS = 0;

    status.correlation.store (full.value());
    status.silent.store (full.isSilent());
    for (size_t b = 0; b < 3; ++b)
    {
        status.bandCorrelation[b].store (bands[b].value());
        status.bandSilent[b].store (bands[b].isSilent());
    }
    const double bal = full.isSilent() ? 0.0 : 10.0 * std::log10 ((full.rr + 1.0e-12) / (full.ll + 1.0e-12));
    status.balanceDb.store ((float) std::clamp (bal, -24.0, 24.0));
    const double w = (msM + msS) < 1.0e-8 ? 0.0 : 100.0 * std::sqrt (msS / std::max (msM, 1.0e-12));
    status.widthPercent.store ((float) std::min (w, 999.0));
    status.peakL.store (pkL);
    status.peakR.store (pkR);
}
} // namespace ph
