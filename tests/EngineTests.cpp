// Engine tests: run with ctest or directly. No framework, exits non-zero on failure.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "Engine.h"
#include "Presets.h"

using namespace ph;

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("FAIL %s:%d: ", __FILE__, __LINE__); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

struct Stereo
{
    std::vector<float> l, r;
    explicit Stereo (int n = 0) : l ((size_t) n, 0.0f), r ((size_t) n, 0.0f) {}
    int size() const { return (int) l.size(); }
};

static Stereo noise (int n, unsigned seed, float amp = 0.3f, bool correlated = false)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> u (-amp, amp);
    Stereo s (n);
    for (int i = 0; i < n; ++i) { s.l[(size_t) i] = u (rng); s.r[(size_t) i] = correlated ? s.l[(size_t) i] : u (rng); }
    return s;
}

static std::vector<float> sine (double hz, double sr, int n, float amp = 0.5f, double phase = 0)
{
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i) v[(size_t) i] = amp * (float) std::sin (2 * kPi * hz * i / sr + phase);
    return v;
}

// Runs the whole buffer through the engine in blocks, in place.
static Stereo run (Engine& e, Stereo s, int block = 512)
{
    for (int start = 0; start < s.size(); start += block)
    {
        const int n = std::min (block, s.size() - start);
        float* ch[2] = { s.l.data() + start, s.r.data() + start };
        e.process (ch, ch, 2, n);
    }
    return s;
}

static Engine& prepared (Engine& e, const Params& p, double sr = 48000)
{
    e.setParams (p);
    e.prepare (sr, 512);
    return e;
}

static double rms (const std::vector<float>& v, int from = 0)
{
    double a = 0;
    for (size_t i = (size_t) from; i < v.size(); ++i) a += (double) v[i] * v[i];
    return std::sqrt (a / std::max<size_t> (1, v.size() - (size_t) from));
}

static void testBitTransparent()
{
    for (const auto& pr : factoryPresets())
    {
        const auto& p = pr.params;
        const bool neutral = p.width == 1.0f && p.bassMonoHz == 0 && ! p.monoCheck && ! p.swapLR && ! p.invertL && ! p.invertR && p.outputDb == 0;
        if (! neutral) continue;
        Engine e; prepared (e, p);
        auto in = noise (48000, 1);
        auto out = run (e, in, 333);
        CHECK (std::memcmp (in.l.data(), out.l.data(), in.l.size() * sizeof (float)) == 0 &&
               std::memcmp (in.r.data(), out.r.data(), in.r.size() * sizeof (float)) == 0,
               "preset '%s' is not bit-transparent", pr.name);
        CHECK (e.wasBypassed(), "preset '%s' did not report bypass", pr.name);
    }

    // engage a fix, then return to neutral: once the ramps settle the path is bit-exact again
    Engine e; Params p; prepared (e, p);
    p.width = 1.5f; p.outputDb = -3; e.setParams (p);
    run (e, noise (4800, 2));
    p = Params(); e.setParams (p);
    run (e, noise (4800, 3));
    auto in = noise (9600, 4);
    auto out = run (e, in);
    CHECK (std::memcmp (in.l.data(), out.l.data(), in.l.size() * sizeof (float)) == 0, "not bit-exact after returning to neutral");

    // out-of-place processing also copies exactly
    Engine e2; prepared (e2, Params());
    auto src = noise (1024, 5);
    Stereo dst (1024);
    const float* ins[2] = { src.l.data(), src.r.data() };
    float* outs[2] = { dst.l.data(), dst.r.data() };
    e2.process (ins, outs, 2, 1024);
    CHECK (std::memcmp (src.l.data(), dst.l.data(), 1024 * sizeof (float)) == 0 && std::memcmp (src.r.data(), dst.r.data(), 1024 * sizeof (float)) == 0,
           "out-of-place copy not exact");
}

static void testSwapAndPolarity()
{
    auto in = noise (2048, 6);
    { Engine e; Params p; p.swapLR = true; prepared (e, p);
      auto o = run (e, in);
      CHECK (o.l == in.r && o.r == in.l, "swap is not exact"); }
    { Engine e; Params p; p.invertL = true; prepared (e, p);
      auto o = run (e, in);
      bool ok = o.r == in.r;
      for (int i = 0; i < in.size(); ++i) ok = ok && o.l[(size_t) i] == -in.l[(size_t) i];
      CHECK (ok, "polarity L is not exact"); }
    { Engine e; Params p; p.invertR = true; p.swapLR = true; prepared (e, p);
      auto o = run (e, in);
      bool ok = true;   // R is inverted first, then swapped onto the left
      for (int i = 0; i < in.size(); ++i) ok = ok && o.l[(size_t) i] == -in.r[(size_t) i] && o.r[(size_t) i] == in.l[(size_t) i];
      CHECK (ok, "invert R then swap is wrong"); }
}

static void testMonoAndWidth()
{
    auto in = noise (9600, 7);
    { Engine e; Params p; p.monoCheck = true; prepared (e, p);
      auto o = run (e, in);
      double err = 0;
      for (int i = 0; i < in.size(); ++i)
      {
          const double m = 0.5 * ((double) in.l[(size_t) i] + in.r[(size_t) i]);
          err = std::max ({ err, std::abs (o.l[(size_t) i] - m), std::abs (o.r[(size_t) i] - m) });
      }
      CHECK (err < 1e-6, "mono check is not (L+R)/2, max err %g", err); }

    { Engine e; Params p; p.width = 0; prepared (e, p);
      auto o = run (e, in);
      double err = 0;
      for (int i = 0; i < in.size(); ++i) err = std::max (err, (double) std::abs (o.l[(size_t) i] - o.r[(size_t) i]));
      CHECK (err < 1e-6, "width 0 is not mono, max L-R %g", err); }

    { Engine e; Params p; p.width = 2; prepared (e, p);
      auto o = run (e, in);
      double err = 0;
      for (int i = 0; i < in.size(); ++i)
      {
          const double m = 0.5 * ((double) in.l[(size_t) i] + in.r[(size_t) i]);
          const double s = 0.5 * ((double) in.l[(size_t) i] - in.r[(size_t) i]);
          err = std::max ({ err, std::abs (o.l[(size_t) i] - (m + 2 * s)), std::abs (o.r[(size_t) i] - (m - 2 * s)) });
      }
      CHECK (err < 1e-5, "width 200%% does not double the side, max err %g", err); }

    { Engine e; Params p; p.outputDb = -6; prepared (e, p);
      auto o = run (e, in);
      const double ratio = rms (o.l) / rms (in.l);
      CHECK (std::abs (20 * std::log10 (ratio) + 6) < 0.01, "output -6 dB gave %.3f dB", 20 * std::log10 (ratio)); }

    // width changes are smoothed: no sample jumps by more than the signal could
    { Engine e; Params p; prepared (e, p);
      Stereo dc (4800);
      std::fill (dc.l.begin(), dc.l.end(), 0.5f);
      std::fill (dc.r.begin(), dc.r.end(), -0.5f);   // pure side
      run (e, dc);
      p.width = 0; e.setParams (p);
      auto o = run (e, dc, 64);
      float jump = 0;
      for (size_t i = 1; i < o.l.size(); ++i) jump = std::max (jump, std::abs (o.l[i] - o.l[i - 1]));
      CHECK (jump < 0.01f, "width change clicks: jump %.4f", jump);
      CHECK (std::abs (o.l.back()) < 1e-6f, "side not removed at width 0"); }
}

static void testBassMono()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        const int n = (int) sr;   // 1 s
        // low sine on the side only, high sine on the side only, plus a mid tone
        auto low = sine (40, sr, n, 0.3f), high = sine (2000, sr, n, 0.3f), mid = sine (440, sr, n, 0.3f);
        Stereo in (n);
        for (int i = 0; i < n; ++i)
        {
            in.l[(size_t) i] = mid[(size_t) i] + low[(size_t) i] + high[(size_t) i];
            in.r[(size_t) i] = mid[(size_t) i] - low[(size_t) i] - high[(size_t) i];
        }
        Engine e; Params p; p.bassMonoHz = 150; prepared (e, p, sr);
        auto o = run (e, in);

        // measure side content at 40 Hz and 2 kHz by correlating with the probe sines
        auto bandLevel = [&] (const std::vector<float>& x, double hz) {
            double re = 0, im = 0; const int from = n / 2;
            for (int i = from; i < n; ++i) { re += x[(size_t) i] * std::cos (2 * kPi * hz * i / sr); im += x[(size_t) i] * std::sin (2 * kPi * hz * i / sr); }
            return 2 * std::sqrt (re * re + im * im) / (n - from);
        };
        std::vector<float> side ((size_t) n), m2 ((size_t) n);
        for (int i = 0; i < n; ++i) { side[(size_t) i] = 0.5f * (o.l[(size_t) i] - o.r[(size_t) i]); m2[(size_t) i] = 0.5f * (o.l[(size_t) i] + o.r[(size_t) i]); }
        const double lowDb = 20 * std::log10 (bandLevel (side, 40) / 0.3 + 1e-12);
        const double highDb = 20 * std::log10 (bandLevel (side, 2000) / 0.3);
        const double midDb = 20 * std::log10 (bandLevel (m2, 440) / 0.3);
        CHECK (lowDb < -30, "bass mono at %g Hz: 40 Hz side only down %.1f dB", sr, lowDb);
        CHECK (std::abs (highDb) < 0.2, "bass mono at %g Hz: 2 kHz side changed %.2f dB", sr, highDb);
        CHECK (std::abs (midDb) < 0.1, "bass mono at %g Hz: mid changed %.2f dB", sr, midDb);
        CHECK (! e.wasBypassed(), "bass mono reported bypass");
    }

    // knob mapping: below 60 Hz means off
    CHECK (Engine::bassMonoFromKnob (50) == 0 && Engine::bassMonoFromKnob (60) == 60 && Engine::bassMonoFromKnob (400) == 300, "bass mono knob mapping");
}

static void testCorrelation()
{
    const int n = 48000;
    auto settle = [] (Engine& e, const Stereo& s) { run (e, s); return e.getStatus().correlation.load(); };

    { Engine e; prepared (e, Params()); auto s = noise (n, 8, 0.3f, true);
      const float c = settle (e, s);
      CHECK (c > 0.999f, "identical channels read %.3f", c);
      CHECK (e.getStatus().widthPercent.load() < 1.0f, "mono reads width %.1f%%", e.getStatus().widthPercent.load()); }

    { Engine e; prepared (e, Params()); auto s = noise (n, 9, 0.3f, true);
      for (auto& x : s.r) x = -x;
      const float c = settle (e, s);
      CHECK (c < -0.999f, "inverted channels read %.3f", c);
      CHECK (e.getStatus().widthPercent.load() > 900.0f, "out of phase should read huge width, got %.1f", e.getStatus().widthPercent.load()); }

    { Engine e; prepared (e, Params()); auto s = noise (n, 10);
      const float c = settle (e, s);
      CHECK (std::abs (c) < 0.15f, "uncorrelated noise read %.3f", c);
      const float w = e.getStatus().widthPercent.load();
      CHECK (w > 70 && w < 130, "uncorrelated equal-level width %.1f%%, expected about 100", w); }

    { Engine e; prepared (e, Params()); Stereo s (n);   // quadrature sines
      s.l = sine (500, 48000, n); s.r = sine (500, 48000, n, 0.5f, kPi / 2);
      const float c = settle (e, s);
      CHECK (std::abs (c) < 0.05f, "90 degree sines read %.3f", c); }

    { Engine e; prepared (e, Params()); Stereo s (n);
      const float c = settle (e, s);
      CHECK (c == 0.0f && e.getStatus().silent.load(), "silence should read 0 and silent"); }

    { Engine e; prepared (e, Params()); Stereo s (n);   // right channel silent
      s.l = sine (300, 48000, n);
      const float c = settle (e, s);
      CHECK (c == 0.0f && ! e.getStatus().silent.load(), "one-sided signal should read 0, got %.3f", c);
      CHECK (e.getStatus().balanceDb.load() < -20.0f, "left-only balance %.1f dB", e.getStatus().balanceDb.load()); }

    { Engine e; prepared (e, Params()); Stereo s (n);   // right 6 dB quieter
      s.l = sine (300, 48000, n); s.r = sine (300, 48000, n, 0.25f);
      settle (e, s);
      const float b = e.getStatus().balanceDb.load();
      CHECK (std::abs (b + 6.02f) < 0.1f, "balance with R -6 dB read %.2f dB", b); }

    // the meter follows a change within about half a second
    { Engine e; prepared (e, Params());
      run (e, noise (n, 11, 0.3f, true));
      auto inv = noise (n / 2, 12, 0.3f, true);
      for (auto& x : inv.r) x = -x;
      const float c = settle (e, inv);
      CHECK (c < -0.95f, "correlation did not follow within 0.5 s: %.3f", c); }
}

static void testBandCorrelation()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        const int n = (int) sr;
        auto low = sine (60, sr, n, 0.3f), mid = sine (900, sr, n, 0.3f), high = sine (9000, sr, n, 0.2f);
        Stereo s (n);   // low in phase, mid in quadrature, high out of phase
        auto midQ = sine (900, sr, n, 0.3f, kPi / 2);
        for (int i = 0; i < n; ++i)
        {
            s.l[(size_t) i] = low[(size_t) i] + mid[(size_t) i] + high[(size_t) i];
            s.r[(size_t) i] = low[(size_t) i] + midQ[(size_t) i] - high[(size_t) i];
        }
        Engine e; Params p; p.xoverLowHz = 200; p.xoverHighHz = 3000; prepared (e, p, sr);
        run (e, s);
        const auto& st = e.getStatus();
        CHECK (st.bandCorrelation[0].load() > 0.98f, "%g Hz: low band %.3f", sr, st.bandCorrelation[0].load());
        CHECK (std::abs (st.bandCorrelation[1].load()) < 0.1f, "%g Hz: mid band %.3f", sr, st.bandCorrelation[1].load());
        CHECK (st.bandCorrelation[2].load() < -0.98f, "%g Hz: high band %.3f", sr, st.bandCorrelation[2].load());
        CHECK (! st.bandSilent[0].load() && ! st.bandSilent[2].load(), "bands flagged silent");
    }
}

static void testCrossoverFlat()
{
    // LR4 low + high is an allpass: magnitude flat at every frequency
    for (double hz : { 50.0, 200.0, 1000.0, 5000.0, 15000.0 })
    {
        LR4Split x; x.set (300, 48000);
        const int n = 48000;
        auto s = sine (hz, 48000, n, 0.5f);
        double acc = 0;
        for (int i = 0; i < n; ++i) { double lo, hi; x.process (s[(size_t) i], lo, hi); if (i >= n / 2) acc += (lo + hi) * (lo + hi); }
        const double db = 10 * std::log10 (acc / (n / 2) / 0.125);
        CHECK (std::abs (db) < 0.05, "LR4 sum at %g Hz is %.3f dB", hz, db);
    }
}

static void testScopeFeed()
{
    Engine e; Params p; prepared (e, p);
    auto in = noise (4096, 13);
    run (e, in);
    std::vector<float> l (8192), r (8192);
    const int got = e.getScopeFifo().pull (l.data(), r.data(), 8192);
    CHECK (got == 4096, "scope feed got %d samples, expected 4096", got);
    CHECK (std::equal (in.l.begin(), in.l.end(), l.begin()) && std::equal (in.r.begin(), in.r.end(), r.begin()), "scope feed differs from output");

    // UI not draining: FIFO fills and drops instead of blocking
    run (e, noise (100000, 14));
    CHECK (e.getScopeFifo().ready() == e.getScopeFifo().capacity() - 1, "fifo should be full, has %d", e.getScopeFifo().ready());
    e.getScopeFifo().discard();
    CHECK (e.getScopeFifo().ready() == 0, "discard left %d", e.getScopeFifo().ready());

    // band feed: High band of a 50 Hz tone is near silent
    Engine e2; Params p2; p2.displayBand = Band::High; prepared (e2, p2);
    Stereo s (48000); s.l = sine (50, 48000, 48000); s.r = s.l;
    run (e2, s);
    std::vector<float> a (32768), b (32768);
    const int k = e2.getScopeFifo().pull (a.data(), b.data(), 32768);
    float pk = 0;
    for (int i = k / 2; i < k; ++i) pk = std::max (pk, std::abs (a[(size_t) i]));
    CHECK (pk < 0.001f, "high band scope feed of 50 Hz peaks at %.4f", pk);
}

static void testMonoTrackAndSafety()
{
    Engine e; Params p; p.outputDb = -6; p.width = 2; prepared (e, p);
    std::vector<float> m = sine (100, 48000, 4800);
    float* ch[1] = { m.data() };
    e.process (ch, ch, 1, 4800);
    CHECK (std::abs (m[1000] - 0.5f * sine (100, 48000, 4800)[1000]) < 0.01f, "mono track gain wrong");

    // extreme settings at many sample rates stay finite
    for (double sr : { 22050.0, 44100.0, 192000.0 })
    {
        Engine x; Params q; q.width = 2; q.bassMonoHz = 300; q.outputDb = 12; q.xoverLowHz = 1000; q.xoverHighHz = 1000;
        prepared (x, q, sr);
        auto o = run (x, noise ((int) sr / 2, 15, 1.0f), 4096);   // block larger than prepared size
        bool finite = true;
        for (float v : o.l) finite = finite && std::isfinite (v);
        CHECK (finite, "non-finite output at %g Hz", sr);
        CHECK (x.getParams().xoverHighHz >= 2 * x.getParams().xoverLowHz, "crossovers not kept apart");
    }
}

static void testPresets()
{
    const auto& ps = factoryPresets();
    CHECK (ps.size() >= 5, "only %d presets", (int) ps.size());
    const char* needed[] = { "Mastering", "Phosphor Classic", "Bass Check", "Mono Compatibility", "Wide Mix" };
    for (auto* name : needed)
    {
        bool found = false;
        for (auto& p : ps) found = found || std::strcmp (p.name, name) == 0;
        CHECK (found, "missing preset %s", name);
    }
    CHECK (std::strcmp (ps.front().name, "Default") == 0, "first preset should be Default");
    for (auto& p : ps)
    {
        CHECK (p.display.persistence >= 0 && p.display.persistence <= 1, "%s persistence", p.name);
        CHECK (p.params.width >= 0 && p.params.width <= 2, "%s width", p.name);
        CHECK (p.params.bassMonoHz == 0 || (p.params.bassMonoHz >= kBassMonoMinHz && p.params.bassMonoHz <= kBassMonoMaxHz), "%s bass mono", p.name);
    }
}

int main()
{
    testBitTransparent();
    testSwapAndPolarity();
    testMonoAndWidth();
    testBassMono();
    testCorrelation();
    testBandCorrelation();
    testCrossoverFlat();
    testScopeFeed();
    testMonoTrackAndSafety();
    testPresets();
    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
