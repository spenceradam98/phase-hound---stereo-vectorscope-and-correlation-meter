# Phase Hound: stereo vectorscope & correlation meter (design, v0.1)

Stack: JUCE 8.0.4, C++17, CMake, VST3 + AU (macOS) + Standalone. Same layout as Comma and the
other plugins: a JUCE-free engine library with its own tests and a thin plugin wrapper.
Scalar DSP only, so it runs on macOS 11 Intel machines without AVX2. Zero latency.

## Signal flow

```
in L/R -> polarity L/R -> swap -> M/S -> bass mono -> width -> M/S decode -> mono check -> output
                                                                                         |
                       meters + scope feed (what you hear) <-----------------------------+
```

- **Bit-transparent bypass.** When Width is 100 %, Bass Mono is off, Mono is off and Output
  is 0 dB, and every gain ramp has landed, the block is copied (polarity and swap are exact
  negations / swaps). The tests compare the output byte for byte for every neutral preset and
  after a fix is engaged and released.
- **Width:** side gain 0-200 % with a 30 ms ramp.
- **Bass mono:** a 4th-order Linkwitz-Riley split on the side channel keeps only the side
  above the cutoff. The mid goes through the matching LR4 allpass (low + high) so mid and side
  stay phase-aligned above the crossover; the mid's magnitude is unchanged (tested within
  0.1 dB). Switching on or off crossfades over 30 ms. One knob: fully left = Off, then 60-300 Hz.
- **Mono check:** (L+R)/2 on both sides, 10 ms crossfade. The MONO button latches on a click and
  is momentary when held longer than ~0.35 s.
- Mono tracks: only Output and Polarity L apply.

## Analysis (audio thread, on the output)

- **Correlation:** normalised cross-correlation E[LR] / sqrt(E[LL] E[RR]) with exponential
  averaging (tau 100 ms, about a 300 ms window). Silence (< about -80 dBFS) reads 0 and dims
  the meter; one silent side also reads 0. The UI adds a 3 s low-water mark.
- **Per-band correlation:** 3-way LR4 split (Low/Mid at 40-1000 Hz, default 200 Hz; Mid/High
  at 1-12 kHz, default 3 kHz). LR filters have the same phase on L and R, so correlation within
  a band is preserved.
- **Balance:** 10 log10(E[RR] / E[LL]) dB, positive = right louder.
- **Width:** 100 * rms(S) / rms(M). Mono = 0 %, uncorrelated equal-level channels = 100 %,
  fully out of phase shows OUT.
- **Scope feed:** the selected band (Full / Low / Mid / High) is pushed into a lock-free SPSC
  FIFO of stereo pairs (32k). If the editor is closed or frozen the audio thread drops samples
  instead of waiting.

## UI

- The cloud is drawn into an accumulation image that fades every frame (`multiplyAllAlphas`), so
  persistence costs the same whatever the trail length. Persistence 0-100 % maps to a fade
  to 1 % in 2 to 120 frames. Glow themes draw a faint wider dot / stroke and a bloom pass.
- Auto-gain follows the cloud's peak (instant attack, release of a few seconds) and scales it to 85 % of the
  radius; with Auto off, Zoom (-12 to +24 dB) applies.
- Modes: Goniometer (x = (R-L)/sqrt2, y = (L+R)/sqrt2), Lissajous (x = L, y = R), Polar (the
  goniometer folded into the upper half; the wedge between the L and R lines is in-phase,
  points towards the horizon cancel in mono).
- Screen rendering and the 13 themes are ported from scope-vst (bezel, backdrop, scanlines,
  vignette, gloss, LCD pixelation, segmented meters). Theme and window size (Small / Medium /
  Large presets or free resize) are saved with the session. Freeze is UI-only.

## Presets

Display presets: Default, Mastering (crossovers 150 Hz / 4 kHz, short trail), Phosphor
Classic (lines, long decay), Bass Check (cloud shows the Low band, bass mono on at 120 Hz),
Mono Compatibility (Polar), Wide Mix (width 130 % + bass mono 120 Hz), Lissajous X-Y. Only
Bass Check and Wide Mix change the audio.

## Decisions made while building

- Meters read the output, so you see what the fixes do.
- Added a display **Band** selector (not in the plan) so "Bass Check" can show only the lows.
- AU added on macOS (Comma, the template, ships VST3 + Standalone only).
- Standalone keeps JUCE's default input mute (feedback guard); the README says how to unmute.
- Swap and polarity toggles switch instantly (they are exact operations), so toggling them
  during playback can click. Width, bass mono, mono and output are ramped.

## Not in v0.1

- Spectrum-of-correlation view (correlation per 1/3 octave), loudness readouts.
- Mid/side solo listening, per-band width.
- User preset save/load.
