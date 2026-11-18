#include "Themes.h"

namespace ph
{
using namespace juce;

void ThemedLookAndFeel::setTheme (const Theme& t)
{
    theme = &t;
    setColour (ResizableWindow::backgroundColourId, t.chassis);
    setColour (TextButton::buttonColourId, t.buttonFace);
    setColour (TextButton::buttonOnColourId, t.accent);
    setColour (TextButton::textColourOffId, t.buttonText);
    setColour (TextButton::textColourOnId, t.accent.contrasting (0.9f));
    setColour (ComboBox::backgroundColourId, t.buttonFace);
    setColour (ComboBox::textColourId, t.buttonText);
    setColour (ComboBox::outlineColourId, t.screenEdge);
    setColour (ComboBox::arrowColourId, t.buttonText);
    setColour (PopupMenu::backgroundColourId, t.buttonFace);
    setColour (PopupMenu::textColourId, t.buttonText);
    setColour (PopupMenu::headerTextColourId, t.accent);
    setColour (PopupMenu::highlightedBackgroundColourId, t.accent);
    setColour (PopupMenu::highlightedTextColourId, t.accent.contrasting (0.9f));
    setColour (Label::textColourId, t.chassisText);
    setColour (Slider::thumbColourId, t.accent);
    setColour (Slider::trackColourId, t.buttonFace);
    setColour (Slider::rotarySliderFillColourId, t.accent);
    setColour (Slider::rotarySliderOutlineColourId, t.buttonFace);
    setColour (Slider::textBoxTextColourId, t.chassisText);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (ToggleButton::textColourId, t.chassisText);
    setColour (ToggleButton::tickColourId, t.accent);
}

Font ThemedLookAndFeel::getTextButtonFont (TextButton&, int h) { return theme->font (jmin (14.0f, (float) h * 0.55f), true); }
Font ThemedLookAndFeel::getComboBoxFont (ComboBox&) { return theme->font (13.0f); }
Font ThemedLookAndFeel::getPopupMenuFont() { return theme->font (14.0f); }
Font ThemedLookAndFeel::getLabelFont (Label& l) { return theme->font (l.getFont().getHeight()); }

void ThemedLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour& bg, bool over, bool down)
{
    if (! theme->bevel)
    {
        LookAndFeel_V4::drawButtonBackground (g, b, bg, over, down);
        return;
    }

    auto r = b.getLocalBounds().toFloat();
    auto face = b.getToggleState() ? theme->accent : bg;
    if (over) face = face.brighter (0.08f);
    g.setColour (face);
    g.fillRect (r);

    const bool sunken = down || b.getToggleState();
    auto light = Colours::white.withAlpha (0.7f);
    auto dark = Colours::black.withAlpha (0.55f);
    g.setColour (sunken ? dark : light);
    g.fillRect (r.removeFromTop (1.5f));
    g.fillRect (b.getLocalBounds().toFloat().removeFromLeft (1.5f));
    g.setColour (sunken ? light : dark);
    g.fillRect (b.getLocalBounds().toFloat().removeFromBottom (1.5f));
    g.fillRect (b.getLocalBounds().toFloat().removeFromRight (1.5f));
}

void ThemedLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s)
{
    const auto& t = *theme;
    auto area = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float size = jmin (area.getWidth(), area.getHeight());
    auto box = area.withSizeKeepingCentre (size, size);
    const auto c = box.getCentre();
    const float radius = size * 0.5f;
    const float angle = start + pos * (end - start);
    const bool bipolar = s.getProperties().getWithDefault ("bipolar", false);
    const float from = bipolar ? (start + end) * 0.5f : start;

    // track
    Path track;
    track.addCentredArc (c.x, c.y, radius - 2.0f, radius - 2.0f, 0, start, end, true);
    g.setColour (t.meterOff);
    g.strokePath (track, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));

    Path value;
    value.addCentredArc (c.x, c.y, radius - 2.0f, radius - 2.0f, 0, jmin (from, angle), jmax (from, angle), true);
    if (t.glow > 0.0f)
    {
        g.setColour (t.accent.withAlpha (0.18f * t.glow));
        g.strokePath (value, PathStrokeType (7.0f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    g.setColour (s.isEnabled() ? t.accent : t.accent.withMultipliedSaturation (0.2f));
    g.strokePath (value, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));

    // cap
    auto cap = box.reduced (radius * 0.32f);
    if (t.bevel)
    {
        g.setGradientFill (ColourGradient (t.buttonFace.brighter (0.3f), cap.getX(), cap.getY(),
                                           t.buttonFace.darker (0.4f), cap.getRight(), cap.getBottom(), false));
        g.fillEllipse (cap);
    }
    else
    {
        g.setColour (t.buttonFace);
        g.fillEllipse (cap);
    }
    g.setColour (t.screenEdge);
    g.drawEllipse (cap, 1.0f);

    // pointer
    const float r1 = cap.getWidth() * 0.12f, r2 = cap.getWidth() * 0.48f;
    const Point<float> a1 (c.x + r1 * std::sin (angle), c.y - r1 * std::cos (angle));
    const Point<float> a2 (c.x + r2 * std::sin (angle), c.y - r2 * std::cos (angle));
    g.setColour (t.buttonText);
    g.drawLine ({ a1, a2 }, 2.0f);
}
} // namespace ph
