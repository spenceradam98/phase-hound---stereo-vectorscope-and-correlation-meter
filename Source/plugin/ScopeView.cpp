#include "ScopeView.h"

namespace ph
{
using namespace juce;

//==============================================================================
// ScreenView (ported from scope-vst's DisplayView)
//==============================================================================
Rectangle<float> ScreenView::screenArea() const
{
    return getLocalBounds().toFloat().reduced (theme->bevel ? 4.0f : 2.0f);
}

void ScreenView::paint (Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const auto& t = *theme;
    const float r = t.cornerRadius;

    // Bezel
    g.setColour (t.screenEdge);
    g.fillRoundedRectangle (bounds, r + 2.0f);
    auto screen = screenArea();

    Path clip;
    clip.addRoundedRectangle (screen, r);

    {
        Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (clip);
        g.setColour (t.screen);
        g.fillRect (screen);
        drawBackdrop (g, screen);

        if (t.pixelated && screen.getWidth() > 4 && screen.getHeight() > 4)
        {
            // Render at half resolution and scale up for chunky LCD pixels.
            const int w = (int) screen.getWidth() / 2, h = (int) screen.getHeight() / 2;
            Image img (Image::ARGB, jmax (1, w), jmax (1, h), true);
            {
                Graphics ig (img);
                ig.addTransform (AffineTransform::scale (0.5f));
                paintContent (ig, screen.withPosition (0, 0));
            }
            g.setImageResamplingQuality (Graphics::lowResamplingQuality);
            g.drawImage (img, screen);
        }
        else
        {
            Graphics::ScopedSaveState s2 (g);
            g.setOrigin (screen.getTopLeft().toInt());
            paintContent (g, screen.withPosition (0, 0));
        }

        drawOverlay (g, screen);
    }

    if (t.bevel)
    {
        // Sunken screen: dark top-left, light bottom-right
        g.setColour (Colours::black.withAlpha (0.5f));
        g.drawLine (screen.getX(), screen.getY(), screen.getRight(), screen.getY(), 2.0f);
        g.drawLine (screen.getX(), screen.getY(), screen.getX(), screen.getBottom(), 2.0f);
        g.setColour (Colours::white.withAlpha (0.35f));
        g.drawLine (screen.getX(), screen.getBottom(), screen.getRight(), screen.getBottom(), 1.5f);
        g.drawLine (screen.getRight(), screen.getY(), screen.getRight(), screen.getBottom(), 1.5f);
    }
}

void ScreenView::drawBackdrop (Graphics& g, Rectangle<float> a) const
{
    const auto& t = *theme;
    switch (t.backdrop)
    {
        case Backdrop::PerspectiveGrid:
        {
            // Sky glow above the horizon, then a receding floor grid
            const float horizon = a.getY() + a.getHeight() * 0.62f;
            g.setGradientFill (ColourGradient (t.screen, a.getX(), a.getY(),
                                               t.accent.withAlpha (0.22f), a.getX(), horizon, false));
            g.fillRect (a.withBottom (horizon));
            g.setColour (t.accent.withAlpha (0.35f));
            g.drawHorizontalLine ((int) horizon, a.getX(), a.getRight());

            g.setColour (t.grid.withMultipliedAlpha (0.8f));
            const float cx = a.getCentreX();
            for (int i = -14; i <= 14; ++i)
            {
                const float xb = cx + (float) i * a.getWidth() * 0.12f;
                g.drawLine (cx + (float) i * a.getWidth() * 0.012f, horizon, xb, a.getBottom(), 1.0f);
            }
            for (int i = 1; i < 12; ++i)
            {
                const float k = (float) i / 12.0f;
                const float y = horizon + (a.getBottom() - horizon) * k * k;
                g.drawHorizontalLine ((int) y, a.getX(), a.getRight());
            }
            break;
        }
        case Backdrop::HexGrid:
        {
            g.setColour (t.grid.withMultipliedAlpha (0.6f));
            const float s = 16.0f, hgt = s * std::sqrt (3.0f);
            for (float y = a.getY() - hgt; y < a.getBottom() + hgt; y += hgt)
                for (int col = 0; col * s * 1.5f < a.getWidth() + 2 * s; ++col)
                {
                    const float x = a.getX() + (float) col * s * 1.5f;
                    const float yy = y + ((col & 1) ? hgt * 0.5f : 0.0f);
                    Path hexagon;
                    hexagon.addPolygon ({ x, yy }, 6, s * 0.95f, MathConstants<float>::halfPi / 3.0f);
                    g.strokePath (hexagon, PathStrokeType (0.6f));
                }
            break;
        }
        case Backdrop::BrushedMetal:
        case Backdrop::None:
            break;
    }
}

void ScreenView::drawOverlay (Graphics& g, Rectangle<float> a) const
{
    const auto& t = *theme;
    if (t.scanlines)
    {
        g.setColour (Colours::black.withAlpha (0.22f));
        for (float y = a.getY(); y < a.getBottom(); y += 3.0f)
            g.fillRect (a.getX(), y, a.getWidth(), 1.0f);
    }
    if (t.vignette)
    {
        ColourGradient vg (Colours::transparentBlack, a.getCentreX(), a.getCentreY(),
                           Colours::black.withAlpha (0.55f), a.getX(), a.getY(), true);
        vg.addColour (0.6, Colours::transparentBlack);
        g.setGradientFill (vg);
        g.fillRect (a);
    }
    if (t.gloss)
    {
        Path p;
        p.startNewSubPath (a.getTopLeft());
        p.lineTo (a.getTopRight());
        p.lineTo (a.getRight(), a.getY() + a.getHeight() * 0.32f);
        p.quadraticTo (a.getCentreX(), a.getY() + a.getHeight() * 0.48f, a.getX(), a.getY() + a.getHeight() * 0.36f);
        p.closeSubPath();
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.16f), a.getX(), a.getY(),
                                           Colours::white.withAlpha (0.02f), a.getX(), a.getY() + a.getHeight() * 0.45f, false));
        g.fillPath (p);
    }
}

void ScreenView::strokeGlow (Graphics& g, const Path& p, Colour c, float width, const Theme& t)
{
    if (t.glow > 0.0f)
    {
        g.setColour (c.withMultipliedAlpha (0.07f * t.glow));
        g.strokePath (p, PathStrokeType (width * 7.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (c.withMultipliedAlpha (0.18f * t.glow));
        g.strokePath (p, PathStrokeType (width * 3.0f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    g.setColour (c);
    g.strokePath (p, PathStrokeType (width, PathStrokeType::curved, PathStrokeType::rounded));
}

//==============================================================================
// CloudView
//==============================================================================
void CloudView::resized()
{
    const auto a = screenArea();
    const int w = jmax (1, (int) a.getWidth()), h = jmax (1, (int) a.getHeight());
    if (trail.isNull() || trail.getWidth() != w || trail.getHeight() != h)
        trail = Image (Image::ARGB, w, h, true);
}

void CloudView::clear()
{
    if (trail.isValid()) trail.clear (trail.getBounds());
    repaint();
}

CloudView::Geometry CloudView::geometry (Rectangle<float> a, DisplayMode m) const
{
    Geometry g;
    if (m == DisplayMode::Polar)
    {
        // half disc standing on the bottom edge
        g.radius = jmin (a.getWidth() * 0.5f, a.getHeight() - 24.0f) * 0.92f;
        g.centre = { a.getCentreX(), a.getBottom() - 14.0f };
    }
    else
    {
        g.radius = jmin (a.getWidth(), a.getHeight()) * 0.5f * 0.9f;
        g.centre = a.getCentre();
    }
    g.radius = jmax (4.0f, g.radius);
    return g;
}

Point<float> CloudView::map (float l, float r, DisplayMode m, const Geometry& g, float k) const
{
    constexpr float h = 0.70710678f;
    float x = 0, y = 0;
    switch (m)
    {
        case DisplayMode::Goniometer: x = (r - l) * h; y = (l + r) * h; break;   // mono = vertical, L top-left
        case DisplayMode::Lissajous:  x = l; y = r; break;                       // classic X-Y
        case DisplayMode::Polar:
        {
            float mid = (l + r) * h, side = (r - l) * h;
            if (mid < 0) { mid = -mid; side = -side; }   // fold into the upper half
            x = side; y = mid;
            break;
        }
    }
    x = jlimit (-1.1f, 1.1f, x * k);
    y = jlimit (-1.1f, 1.1f, y * k);
    return { g.centre.x + x * g.radius, g.centre.y - y * g.radius };
}

void CloudView::fade (const Display& d, double frameRateHz)
{
    if (trail.isNull()) return;
    const double frames = (2.0 + std::pow ((double) d.persistence, 1.5) * 118.0) * jmax (0.25, frameRateHz / 60.0);
    const float decay = (float) std::pow (0.01, 1.0 / frames);
    if (d.persistence <= 0.001f || decay < 0.05f)
        trail.clear (trail.getBounds());
    else
        trail.multiplyAllAlphas (decay);
}

void CloudView::fadeOnly (const Display& d, double frameRateHz)
{
    if (frozen) return;
    fade (d, frameRateHz);
    repaint();
}

void CloudView::addFrame (const float* l, const float* r, int n, const Display& d, double frameRateHz)
{
    if (frozen || trail.isNull()) return;
    if (d.mode != mode) { mode = d.mode; trail.clear (trail.getBounds()); }
    fade (d, frameRateHz);

    // gain: auto follows the cloud's peak with a slow release, manual uses Zoom
    float peak = 0;
    for (int i = 0; i < n; ++i)
    {
        const float a = mode == DisplayMode::Lissajous ? jmax (std::abs (l[i]), std::abs (r[i]))
                                                       : std::sqrt (0.5f * (l[i] * l[i] + r[i] * r[i])) * 1.4142f;
        peak = jmax (peak, a);
    }
    envelope = jmax (peak, envelope * 0.985f);
    const float target = d.autoGain ? jlimit (0.25f, 64.0f, 0.85f / jmax (envelope, 1.0e-4f))
                                    : std::pow (10.0f, d.zoomDb / 20.0f);
    gain += (target - gain) * (d.autoGain ? 0.12f : 1.0f);

    const auto& t = *theme;
    const auto area = trail.getBounds().toFloat();
    const auto geo = geometry (area, mode);
    Graphics g (trail);
    const auto colour = t.traceL;
    const int step = jmax (1, n / 6000);   // cap the work per frame

    if (d.dots)
    {
        if (t.glow > 0.0f)
        {
            g.setColour (colour.withAlpha (0.05f * t.glow));
            for (int i = 0; i < n; i += step)
            {
                const auto p = map (l[i], r[i], mode, geo, gain);
                g.fillRect (p.x - 2.0f, p.y - 2.0f, 4.0f, 4.0f);
            }
        }
        g.setColour (colour.withAlpha (0.6f));
        for (int i = 0; i < n; i += step)
        {
            const auto p = map (l[i], r[i], mode, geo, gain);
            g.fillRect (p.x - 0.8f, p.y - 0.8f, 1.6f, 1.6f);
        }
    }
    else if (n > 1)
    {
        Path path;
        path.preallocateSpace (3 * (n / step + 2));
        for (int i = 0; i < n; i += step)
        {
            const auto p = map (l[i], r[i], mode, geo, gain);
            if (i == 0) path.startNewSubPath (p); else path.lineTo (p);
        }
        if (t.glow > 0.0f)
        {
            g.setColour (colour.withAlpha (0.08f * t.glow));
            g.strokePath (path, PathStrokeType (4.0f));
        }
        g.setColour (colour.withAlpha (0.55f));
        g.strokePath (path, PathStrokeType (1.0f));
    }
    repaint();
}

void CloudView::drawGrid (Graphics& g, Rectangle<float> a, DisplayMode m) const
{
    const auto& t = *theme;
    const auto geo = geometry (a, m);
    const auto c = geo.centre;
    const float r = geo.radius;
    const float d = r * 0.70710678f;
    g.setFont (t.font (11.0f));

    auto label = [&] (const String& s, float x, float y)
    {
        g.setColour (t.gridText);
        g.drawText (s, Rectangle<float> (x - 14, y - 7, 28, 14), Justification::centred);
    };

    if (m == DisplayMode::Polar)
    {
        // safe wedge between the L and R lines: in-phase content lives here
        Path wedge;
        wedge.startNewSubPath (c);
        wedge.addCentredArc (c.x, c.y, r, r, 0, -MathConstants<float>::pi / 4, MathConstants<float>::pi / 4);
        wedge.closeSubPath();
        g.setColour (t.meterLow.withAlpha (0.06f));
        g.fillPath (wedge);

        g.setColour (t.grid);
        for (float k : { 1.0f, 0.5f })
        {
            Path arc;
            arc.addCentredArc (c.x, c.y, r * k, r * k, 0, -MathConstants<float>::halfPi, MathConstants<float>::halfPi, true);
            g.strokePath (arc, PathStrokeType (1.0f));
        }
        g.drawLine (c.x - r, c.y, c.x + r, c.y);
        g.drawLine (c.x, c.y, c.x, c.y - r);
        g.drawLine (c.x, c.y, c.x - d, c.y - d);
        g.drawLine (c.x, c.y, c.x + d, c.y - d);
        label ("M", c.x, c.y - r - 9);
        label ("L", c.x - d - 9, c.y - d - 9);
        label ("R", c.x + d + 9, c.y - d - 9);
        label ("-S", c.x - r + 10, c.y - 10);
        label ("+S", c.x + r - 10, c.y - 10);
        return;
    }

    g.setColour (t.grid);
    const auto sq = Rectangle<float> (r * 2, r * 2).withCentre (c);
    if (m == DisplayMode::Goniometer)
    {
        g.drawEllipse (sq, 1.0f);
        g.drawEllipse (sq.reduced (r * 0.5f), 1.0f);
        g.drawLine (c.x, sq.getY(), c.x, sq.getBottom());
        g.drawLine (sq.getX(), c.y, sq.getRight(), c.y);
        g.drawLine (c.x - d, c.y - d, c.x + d, c.y + d);
        g.drawLine (c.x + d, c.y - d, c.x - d, c.y + d);
        label ("M", c.x, sq.getY() - 8);
        label ("L", c.x - d - 9, c.y - d - 9);
        label ("R", c.x + d + 9, c.y - d - 9);
        label ("-S", sq.getX() - 4, c.y - 9);
        label ("+S", sq.getRight() + 4, c.y - 9);
    }
    else
    {
        g.drawRect (sq, 1.0f);
        g.drawRect (sq.reduced (r * 0.5f), 1.0f);
        g.drawLine (c.x, sq.getY(), c.x, sq.getBottom());
        g.drawLine (sq.getX(), c.y, sq.getRight(), c.y);
        g.drawLine (sq.getX(), sq.getBottom(), sq.getRight(), sq.getY());   // mono line
        if (t.minorTicks)
            for (int i = -10; i <= 10; ++i)
            {
                const float o = r * (float) i / 10.0f;
                g.drawLine (c.x + o, c.y - 3, c.x + o, c.y + 3);
                g.drawLine (c.x - 3, c.y + o, c.x + 3, c.y + o);
            }
        label ("L", sq.getRight() + 8, c.y);
        label ("R", c.x, sq.getY() - 8);
    }
}

void CloudView::paintContent (Graphics& g, Rectangle<float> a)
{
    const auto& t = *theme;
    drawGrid (g, a, mode);
    if (trail.isValid())
    {
        if (t.glow > 0.0f)
        {
            // cheap bloom: the trail again, slightly larger and faint
            Graphics::ScopedSaveState s (g);
            g.setOpacity (0.25f * t.glow);
            g.drawImage (trail, a.expanded (1.5f));
        }
        g.setOpacity (1.0f);
        g.drawImageAt (trail, 0, 0);
    }

    g.setFont (t.font (11.5f, true));
    g.setColour (t.text);
    const char* modeName = mode == DisplayMode::Goniometer ? "GONIO" : mode == DisplayMode::Lissajous ? "X-Y" : "POLAR";
    g.drawText (String (modeName) + "  " + bandLabel, a.reduced (10, 8), Justification::topLeft);
    const float gainDb = 20.0f * std::log10 (jmax (gain, 1.0e-4f));
    g.setFont (t.font (11.0f));
    g.setColour (t.gridText);
    g.drawText ((gainDb >= 0 ? "+" : "") + String (gainDb, 1) + " dB", a.reduced (10, 8), Justification::topRight);
    if (frozen)
    {
        g.setColour (t.marker);
        g.setFont (t.font (12.0f, true));
        g.drawText ("FROZEN", a.reduced (10, 8), Justification::bottomRight);
    }
}

//==============================================================================
// BarMeter
//==============================================================================
void BarMeter::setValue (float v, bool isSilent, const String& readout)
{
    value = jlimit (-1.0f, 1.0f, v);
    silent = isSilent;
    shown += (value - shown) * 0.35f;
    text = readout;
    const double now = Time::getMillisecondCounterHiRes();
    if (kind == Kind::Correlation)
    {
        if (silent) { holdMin = 1; }
        else if (value <= holdMin || now - holdTime > 3000.0) { holdMin = value; holdTime = now; }
    }
    repaint();
}

Colour BarMeter::colourFor (float v) const
{
    const auto& t = *theme;
    if (kind == Kind::Balance) return t.meterLow;
    if (v < 0.0f) return t.meterHigh;
    if (v < 0.4f) return t.meterMid;
    return t.meterLow;
}

void BarMeter::paintContent (Graphics& g, Rectangle<float> a)
{
    const auto& t = *theme;
    auto inner = a.reduced (vertical ? 5.0f : 8.0f, vertical ? 6.0f : 5.0f);
    Rectangle<float> captionArea, textArea;
    if (vertical)
    {
        captionArea = inner.removeFromTop (14.0f);
        textArea = inner.removeFromBottom (14.0f);
        inner = inner.reduced (inner.getWidth() * 0.22f, 2.0f);
    }
    else
    {
        textArea = inner.removeFromRight (jmin (64.0f, inner.getWidth() * 0.22f));
        captionArea = inner.removeFromBottom (12.0f);
    }

    g.setColour (t.meterOff);
    g.fillRect (inner);

    const float v = silent ? 0.0f : shown;
    const auto col = colourFor (v).withMultipliedAlpha (silent ? 0.3f : 1.0f);
    Rectangle<float> fill;
    if (vertical)
    {
        const float mid = inner.getCentreY(), y = mid - v * inner.getHeight() * 0.5f;
        fill = { inner.getX(), jmin (mid, y), inner.getWidth(), std::abs (y - mid) };
    }
    else
    {
        const float mid = inner.getCentreX(), x = mid + v * inner.getWidth() * 0.5f;
        fill = { jmin (mid, x), inner.getY(), std::abs (x - mid), inner.getHeight() };
    }

    if (t.meterStyle == MeterStyle::Segments)
    {
        const int segs = vertical ? 16 : 40;
        for (int i = 0; i < segs; ++i)
        {
            Rectangle<float> seg = vertical
                ? Rectangle<float> (inner.getX(), inner.getY() + inner.getHeight() * i / segs, inner.getWidth(), inner.getHeight() / segs).reduced (0, 1)
                : Rectangle<float> (inner.getX() + inner.getWidth() * i / segs, inner.getY(), inner.getWidth() / segs, inner.getHeight()).reduced (1, 0);
            const float centreVal = vertical ? 1.0f - 2.0f * (i + 0.5f) / segs : -1.0f + 2.0f * (i + 0.5f) / segs;
            const bool lit = fill.intersects (seg) && fill.getIntersection (seg).getWidth() * fill.getIntersection (seg).getHeight() > 0.4f * seg.getWidth() * seg.getHeight();
            g.setColour (lit ? colourFor (centreVal).withMultipliedAlpha (silent ? 0.3f : 1.0f) : t.meterOff.brighter (0.08f));
            g.fillRect (seg);
        }
    }
    else
    {
        if (t.glow > 0.0f)
        {
            g.setColour (col.withAlpha (0.25f * t.glow));
            g.fillRect (fill.expanded (2.0f));
        }
        g.setColour (col);
        g.fillRect (fill);
    }

    // centre line and hold marker
    g.setColour (t.gridText);
    if (vertical) g.drawHorizontalLine ((int) inner.getCentreY(), inner.getX() - 3, inner.getRight() + 3);
    else          g.drawVerticalLine ((int) inner.getCentreX(), inner.getY() - 3, inner.getBottom() + 3);
    if (kind == Kind::Correlation && ! silent && holdMin < 0.99f)
    {
        g.setColour (t.marker);
        if (vertical)
        {
            const float y = inner.getCentreY() - holdMin * inner.getHeight() * 0.5f;
            g.fillRect (inner.getX() - 2, y - 1, inner.getWidth() + 4, 2.0f);
        }
        else
        {
            const float x = inner.getCentreX() + holdMin * inner.getWidth() * 0.5f;
            g.fillRect (x - 1, inner.getY() - 2, 2.0f, inner.getHeight() + 4);
        }
    }

    g.setFont (t.font (10.5f));
    g.setColour (t.gridText);
    if (vertical)
    {
        g.drawText (caption, captionArea, Justification::centred);
        g.setColour (t.text);
        g.drawText (text, textArea, Justification::centred);
    }
    else
    {
        const bool bal = kind == Kind::Balance;
        g.drawText (bal ? "L" : "-1", captionArea, Justification::left);
        g.drawText (caption, captionArea, Justification::centred);
        g.drawText (bal ? "R" : "+1", captionArea, Justification::right);
        g.setFont (t.font (15.0f, true));
        g.setColour (t.text);
        g.drawText (text, textArea, Justification::centredRight);
    }
}
} // namespace ph
