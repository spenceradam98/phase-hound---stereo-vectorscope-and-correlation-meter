// Factory presets. phase-hound presets are mostly display setups: how the scope is drawn and which
// band it looks at. Only "Bass Check" and "Wide Mix" engage a fix; every other preset leaves the
// audio bit-exact.
#pragma once

#include <vector>
#include "Engine.h"

namespace ph
{
enum class DisplayMode { Goniometer = 0, Lissajous, Polar };

struct Display
{
    DisplayMode mode = DisplayMode::Goniometer;
    float persistence = 0.5f;   // 0 = no trail, 1 = long phosphor decay
    float zoomDb = 0.0f;        // manual gain, -12..+24 dB (ignored while auto-gain is on)
    bool autoGain = true;
    bool dots = true;           // dots or connected lines
};

struct Preset
{
    const char* name;
    const char* blurb;
    Display display;
    Params params;
};

inline const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> list = [] {
        std::vector<Preset> v;
        auto add = [&v] (const char* name, const char* blurb, Display d, Params p) { v.push_back ({ name, blurb, d, p }); };

        { Display d; Params p;
          add ("Default", "Goniometer, medium trail, auto-gain. Audio untouched.", d, p); }

        { Display d; d.persistence = 0.35f; Params p; p.xoverLowHz = 150; p.xoverHighHz = 4000;
          add ("Mastering", "Fine dots, short trail, bands split at 150 Hz and 4 kHz. Audio untouched.", d, p); }

        { Display d; d.persistence = 0.9f; d.dots = false; Params p;
          add ("Phosphor Classic", "Connected lines with a long phosphor decay, like an analogue X-Y scope.", d, p); }

        { Display d; d.persistence = 0.6f; Params p; p.bassMonoHz = 120; p.xoverLowHz = 120; p.displayBand = Band::Low;
          add ("Bass Check", "Scope shows only the low band; bass mono on at 120 Hz.", d, p); }

        { Display d; d.mode = DisplayMode::Polar; d.persistence = 0.5f; Params p;
          add ("Mono Compatibility", "Polar view: anything below the horizon cancels in mono. Tap MONO to listen.", d, p); }

        { Display d; d.persistence = 0.5f; Params p; p.width = 1.3f; p.bassMonoHz = 120;
          add ("Wide Mix", "Width 130 % with the low end kept mono below 120 Hz.", d, p); }

        { Display d; d.mode = DisplayMode::Lissajous; d.persistence = 0.7f; d.dots = false; Params p;
          add ("Lissajous X-Y", "Unrotated X-Y: left on the horizontal axis, right on the vertical.", d, p); }
        return v;
    }();
    return list;
}
} // namespace ph
