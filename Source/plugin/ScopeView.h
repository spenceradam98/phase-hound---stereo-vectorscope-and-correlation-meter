// Displays: the themed "screen" (ported from scope-vst's DisplayView), the phosphor vectorscope and
// the correlation / balance meters.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Presets.h"
#include "Themes.h"

namespace ph
{
// Draws the themed screen (bezel, backdrop, scanlines, vignette, gloss, LCD pixelation) around content.
class ScreenView : public juce::Component
{
public:
    ScreenView() { setOpaque (false); }
    virtual void setTheme (const Theme& t) { theme = &t; resized(); repaint(); }
    void paint (juce::Graphics&) override;

    juce::Rectangle<float> screenArea() const;
    static void strokeGlow (juce::Graphics&, const juce::Path&, juce::Colour, float width, const Theme&);

protected:
    virtual void paintContent (juce::Graphics&, juce::Rectangle<float> area) = 0;
    const Theme* theme = &getThemes().front();

private:
    void drawBackdrop (juce::Graphics&, juce::Rectangle<float>) const;
    void drawOverlay (juce::Graphics&, juce::Rectangle<float>) const;
};

//==============================================================================
// The phase cloud. Samples are drawn into an accumulation image that fades every frame
// (phosphor persistence), so the UI cost does not grow with the trail length.
class CloudView : public ScreenView
{
public:
    void resized() override;

    // UI thread, once per frame. Fades the trail and draws the new samples.
    void addFrame (const float* l, const float* r, int n, const Display& d, double frameRateHz);
    void fadeOnly (const Display& d, double frameRateHz);
    void clear();

    void setBandLabel (const juce::String& s) { bandLabel = s; }
    void setFrozen (bool f) { frozen = f; repaint(); }
    float getGain() const { return gain; }

protected:
    void paintContent (juce::Graphics&, juce::Rectangle<float>) override;

private:
    struct Geometry { juce::Point<float> centre; float radius = 1; };
    Geometry geometry (juce::Rectangle<float> area, DisplayMode m) const;
    juce::Point<float> map (float l, float r, DisplayMode m, const Geometry& g, float k) const;
    void drawGrid (juce::Graphics&, juce::Rectangle<float>, DisplayMode) const;
    void fade (const Display& d, double frameRateHz);

    juce::Image trail;
    DisplayMode mode = DisplayMode::Goniometer;
    float gain = 1.0f, envelope = 0.0f;
    bool frozen = false;
    juce::String bandLabel { "FULL" };
};

//==============================================================================
// Horizontal (correlation, balance) or vertical (per-band) bar meter, -1..+1, filled from the centre.
class BarMeter : public ScreenView
{
public:
    enum class Kind { Correlation, Balance };
    BarMeter (Kind k, bool isVertical) : kind (k), vertical (isVertical) {}

    void setValue (float v, bool isSilent, const juce::String& readout);
    void setCaption (const juce::String& c) { caption = c; repaint(); }

protected:
    void paintContent (juce::Graphics&, juce::Rectangle<float>) override;

private:
    juce::Colour colourFor (float v) const;

    Kind kind;
    bool vertical;
    float value = 0, shown = 0, holdMin = 1;
    double holdTime = 0;
    bool silent = true;
    juce::String text, caption;
};
} // namespace ph
