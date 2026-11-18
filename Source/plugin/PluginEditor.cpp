#include "PluginEditor.h"

using namespace juce;

//==============================================================================
void MonoButton::set (bool on)
{
    param.beginChangeGesture();
    param.setValueNotifyingHost (on ? 1.0f : 0.0f);
    param.endChangeGesture();
}

void MonoButton::mouseDown (const MouseEvent& e)
{
    TextButton::mouseDown (e);
    wasOn = param.getValue() > 0.5f;
    downAt = Time::getMillisecondCounterHiRes();
    if (! wasOn) set (true);
}

void MonoButton::mouseUp (const MouseEvent& e)
{
    TextButton::mouseUp (e);
    const bool held = Time::getMillisecondCounterHiRes() - downAt > 350.0;
    if (held) set (wasOn);          // momentary: back to how it was
    else if (wasOn) set (false);    // short click on a latched button: unlatch
}

//==============================================================================
PhaseHoundEditor::PhaseHoundEditor (PhaseHoundProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    bufL.resize (1 << 15);
    bufR.resize (1 << 15);

    addAndMakeVisible (cloud);
    corrMeter.setCaption ("CORRELATION");
    balanceMeter.setCaption ("BALANCE");
    addAndMakeVisible (corrMeter);
    addAndMakeVisible (balanceMeter);
    const char* bandNames[] = { "LOW", "MID", "HIGH" };
    for (int i = 0; i < 3; ++i)
    {
        bandMeters[(size_t) i].setCaption (bandNames[i]);
        addAndMakeVisible (bandMeters[(size_t) i]);
    }

    // header
    for (auto& pr : ph::factoryPresets()) presetBox.addItem (pr.name, presetBox.getNumItems() + 1);
    presetBox.onChange = [this] {
        const int idx = presetBox.getSelectedItemIndex();
        if (idx >= 0 && idx != proc.getCurrentProgram()) proc.setCurrentProgram (idx);
        lastProgram = proc.getCurrentProgram();
        cloud.clear();
    };
    prevPreset.onClick = [this] { const int n = proc.getNumPrograms(); proc.setCurrentProgram ((proc.getCurrentProgram() + n - 1) % n); cloud.clear(); };
    nextPreset.onClick = [this] { const int n = proc.getNumPrograms(); proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % n); cloud.clear(); };
    for (auto& t : ph::getThemes()) themeBox.addItem (t.name, themeBox.getNumItems() + 1);
    themeBox.onChange = [this] { applyTheme (themeBox.getText()); };
    sizeBox.addItemList ({ "Small", "Medium", "Large" }, 1);
    sizeBox.setTextWhenNothingSelected ("Size");
    sizeBox.onChange = [this] {
        const float k[] = { 0.82f, 1.0f, 1.25f };
        const int i = sizeBox.getSelectedItemIndex();
        if (i >= 0) setSize (roundToInt (kDefaultW * k[i]), roundToInt (kDefaultH * k[i]));
    };
    freeze.setClickingTogglesState (true);
    freeze.onClick = [this] { cloud.setFrozen (freeze.getToggleState()); };
    for (auto* c : std::initializer_list<Component*> { &presetBox, &prevPreset, &nextPreset, &themeBox, &sizeBox, &freeze })
        addAndMakeVisible (c);

    // display
    modeBox.addItemList ({ "Goniometer", "Lissajous", "Polar" }, 1);
    bandBox.addItemList ({ "Full band", "Low band", "Mid band", "High band" }, 1);
    drawBox.addItemList ({ "Dots", "Lines" }, 1);
    for (auto* c : { &modeBox, &bandBox, &drawBox }) addAndMakeVisible (c);
    modeAttach = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "mode", modeBox);
    bandAttach = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "band", bandBox);
    drawAttach = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "draw", drawBox);
    addToggle (autoGain, "autogain");
    persist = &addKnob ("persist", "PERSIST");
    zoom = &addKnob ("zoom", "ZOOM");

    // bands
    xlow = &addKnob ("xlow", "LOW | MID");
    xhigh = &addKnob ("xhigh", "MID | HIGH");
    for (auto* k : { xlow, xhigh }) k->slider.setTextBoxStyle (Slider::TextBoxRight, false, 66, 16);

    // fixes
    width = &addKnob ("width", "WIDTH");
    bass = &addKnob ("bassmono", "BASS MONO");
    output = &addKnob ("output", "OUTPUT", true);
    addToggle (swap, "swap");
    addToggle (invL, "invl");
    addToggle (invR, "invr");
    mono = std::make_unique<MonoButton> (*proc.apvts.getParameter ("mono"));
    mono->setTooltip ("Click to latch, hold to listen in mono while pressed");
    addAndMakeVisible (*mono);

    applyTheme (proc.getThemeName());
    presetBox.setSelectedItemIndex (proc.getCurrentProgram(), dontSendNotification);
    lastProgram = proc.getCurrentProgram();

    setResizable (true, true);
    setResizeLimits (720, 465, 1800, 1160);
    const auto s = proc.getUiSize();
    setSize (s.x > 0 ? s.x : kDefaultW, s.y > 0 ? s.y : kDefaultH);
    startTimerHz (60);
}

PhaseHoundEditor::~PhaseHoundEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

PhaseHoundEditor::Knob& PhaseHoundEditor::addKnob (const String& id, const String& label, bool bipolar)
{
    auto k = std::make_unique<Knob>();
    k->label = label;
    k->slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (Slider::TextBoxBelow, false, 70, 16);
    k->slider.getProperties().set ("bipolar", bipolar);
    addAndMakeVisible (k->slider);
    k->attach = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k->slider);
    knobs.push_back (std::move (k));
    return *knobs.back();
}

TextButton& PhaseHoundEditor::addToggle (TextButton& b, const String& id)
{
    b.setClickingTogglesState (true);
    addAndMakeVisible (b);
    buttonAttachments.push_back (std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id, b));
    return b;
}

void PhaseHoundEditor::applyTheme (const String& name)
{
    const int idx = ph::themeIndexByName (name);
    const auto& t = ph::getTheme (idx);
    proc.setThemeName (t.name);
    lnf.setTheme (t);
    themeBox.setSelectedItemIndex (idx, dontSendNotification);
    cloud.setTheme (t);
    corrMeter.setTheme (t);
    balanceMeter.setTheme (t);
    for (auto& m : bandMeters) m.setTheme (t);
    cloud.clear();
    sendLookAndFeelChange();
    repaint();
}

void PhaseHoundEditor::placeKnob (Knob& k, Rectangle<int> area)
{
    k.slider.setBounds (area.withTrimmedTop (16));
}

void PhaseHoundEditor::resized()
{
    proc.setUiSize ({ getWidth(), getHeight() });
    auto b = getLocalBounds();
    header = b.removeFromTop (46);
    {
        auto h = header.reduced (10, 9);
        h.removeFromLeft (190);   // title
        freeze.setBounds (h.removeFromRight (78));
        h.removeFromRight (8);
        sizeBox.setBounds (h.removeFromRight (86));
        h.removeFromRight (8);
        themeBox.setBounds (h.removeFromRight (140));
        h.removeFromRight (16);
        prevPreset.setBounds (h.removeFromLeft (26));
        nextPreset.setBounds (h.removeFromRight (26));
        presetBox.setBounds (h.reduced (4, 0));
    }

    b.reduce (10, 0);
    b.removeFromBottom (10);
    auto right = b.removeFromRight (jlimit (300, 420, (int) (getWidth() * 0.36f)));
    b.removeFromRight (10);

    // left: cloud, then correlation, then balance + width
    auto balanceRow = b.removeFromBottom (jlimit (34, 52, getHeight() / 15));
    b.removeFromBottom (6);
    corrMeter.setBounds (b.removeFromBottom (jlimit (40, 64, getHeight() / 12)));
    b.removeFromBottom (6);
    cloud.setBounds (b);
    widthReadout = balanceRow.removeFromRight (balanceRow.getWidth() / 3);
    balanceRow.removeFromRight (6);
    balanceMeter.setBounds (balanceRow);

    // right: three panels
    const int H = right.getHeight();
    displayPanel = right.removeFromTop ((int) (H * 0.33f));
    right.removeFromTop (8);
    bandPanel = right.removeFromTop ((int) (H * 0.30f));
    right.removeFromTop (8);
    fixPanel = right;

    {
        auto a = displayPanel.reduced (10).withTrimmedTop (16);
        auto row = a.removeFromTop (24);
        modeBox.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3));
        row.removeFromLeft (6);
        bandBox.setBounds (row);
        a.removeFromTop (6);
        row = a.removeFromTop (24);
        drawBox.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3));
        row.removeFromLeft (6);
        autoGain.setBounds (row);
        a.removeFromTop (2);
        placeKnob (*persist, a.removeFromLeft (a.getWidth() / 2));
        placeKnob (*zoom, a);
    }
    {
        auto a = bandPanel.reduced (10).withTrimmedTop (16);
        auto meters = a.removeFromLeft ((int) (a.getWidth() * 0.6f));
        const int mw = meters.getWidth() / 3;
        for (int i = 0; i < 3; ++i)
            bandMeters[(size_t) i].setBounds (meters.removeFromLeft (mw).reduced (2, 0));
        a.removeFromLeft (6);
        placeKnob (*xlow, a.removeFromTop (a.getHeight() / 2));
        placeKnob (*xhigh, a);
    }
    {
        auto a = fixPanel.reduced (10).withTrimmedTop (16);
        auto monoArea = a.removeFromRight ((int) (a.getWidth() * 0.3f));
        mono->setBounds (monoArea.reduced (4, jmax (4, (monoArea.getHeight() - 90) / 2)));
        a.removeFromRight (6);
        auto toggles = a.removeFromBottom (26);
        const int tw = toggles.getWidth() / 3;
        swap.setBounds (toggles.removeFromLeft (tw).reduced (2, 0));
        invL.setBounds (toggles.removeFromLeft (tw).reduced (2, 0));
        invR.setBounds (toggles.reduced (2, 0));
        a.removeFromBottom (6);
        const int kw = a.getWidth() / 3;
        placeKnob (*width, a.removeFromLeft (kw));
        placeKnob (*bass, a.removeFromLeft (kw));
        placeKnob (*output, a);
    }
}

void PhaseHoundEditor::drawPanel (Graphics& g, Rectangle<int> r, const String& title)
{
    const auto& t = lnf.getTheme();
    auto rf = r.toFloat();
    g.setColour (t.chassis.contrasting (0.06f).withAlpha (0.6f));
    g.fillRoundedRectangle (rf, jmin (6.0f, t.cornerRadius));
    if (t.bevel)
    {
        g.setColour (Colours::white.withAlpha (0.25f));
        g.drawLine (rf.getX(), rf.getY(), rf.getRight(), rf.getY(), 1.5f);
        g.drawLine (rf.getX(), rf.getY(), rf.getX(), rf.getBottom(), 1.5f);
        g.setColour (Colours::black.withAlpha (0.35f));
        g.drawLine (rf.getX(), rf.getBottom(), rf.getRight(), rf.getBottom(), 1.5f);
        g.drawLine (rf.getRight(), rf.getY(), rf.getRight(), rf.getBottom(), 1.5f);
    }
    else
    {
        g.setColour (t.screenEdge.withAlpha (0.8f));
        g.drawRoundedRectangle (rf.reduced (0.5f), jmin (6.0f, t.cornerRadius), 1.0f);
    }
    g.setColour (t.accent);
    g.setFont (t.font (12.0f, true));
    g.drawText (title, r.reduced (10, 6).removeFromTop (14), Justification::topLeft);
}

void PhaseHoundEditor::paint (Graphics& g)
{
    const auto& t = lnf.getTheme();
    g.fillAll (t.chassis);
    if (t.backdrop == ph::Backdrop::BrushedMetal)
    {
        Random rng (7);
        for (int y = 0; y < getHeight(); y += 2)
        {
            g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (0.025f + 0.02f * rng.nextFloat()));
            g.drawHorizontalLine (y, 0.0f, (float) getWidth());
        }
    }

    // title
    auto title = header.reduced (14, 0).removeFromLeft (190);
    g.setColour (t.accent);
    g.setFont (t.font (21.0f, true));
    g.drawText ("PHASE HOUND", title.removeFromTop (30).withTrimmedTop (6), Justification::left);
    g.setColour (t.chassisText.withAlpha (0.7f));
    g.setFont (t.font (10.5f));
    g.drawText ("stereo vectorscope & correlation", title, Justification::topLeft);

    drawPanel (g, displayPanel, "DISPLAY");
    drawPanel (g, bandPanel, "BAND CORRELATION");
    drawPanel (g, fixPanel, "FIXES");

    g.setFont (t.font (11.0f, true));
    g.setColour (t.chassisText);
    for (auto& k : knobs)
        if (k->slider.isVisible())
            g.drawText (k->label, k->slider.getBounds().withY (k->slider.getY() - 16).withHeight (16), Justification::centred);

    // width readout box
    auto w = widthReadout.toFloat();
    g.setColour (t.screen);
    g.fillRoundedRectangle (w, jmin (4.0f, t.cornerRadius));
    g.setColour (t.screenEdge);
    g.drawRoundedRectangle (w.reduced (0.5f), jmin (4.0f, t.cornerRadius), 1.0f);
    g.setColour (t.gridText);
    g.setFont (t.font (10.5f));
    g.drawText ("WIDTH", w.reduced (8, 0), Justification::centredLeft);
    g.setColour (t.text);
    g.setFont (t.font (15.0f, true));
    const String ws = widthShown >= 990.0f ? String ("OUT") : String (roundToInt (widthShown)) + " %";
    g.drawText (ws, w.reduced (8, 0), Justification::centredRight);
}

void PhaseHoundEditor::pumpFrames (int frames)
{
    for (int i = 0; i < frames; ++i) timerCallback();
}

void PhaseHoundEditor::timerCallback()
{
    auto& fifo = proc.getScopeFifo();
    const auto display = proc.currentDisplay();
    if (freeze.getToggleState())
    {
        fifo.discard();
    }
    else
    {
        const int n = fifo.pull (bufL.data(), bufR.data(), (int) bufL.size());
        if (n > 0) cloud.addFrame (bufL.data(), bufR.data(), n, display, 60.0);
        else       cloud.fadeOnly (display, 60.0);
    }
    const char* bandLabels[] = { "FULL", "LOW", "MID", "HIGH" };
    cloud.setBandLabel (bandLabels[jlimit (0, 3, (int) proc.apvts.getRawParameterValue ("band")->load())]);

    const auto& st = proc.getStatus();
    const bool silent = st.silent.load();
    const float c = st.correlation.load();
    corrMeter.setValue (c, silent, silent ? String ("--") : (c >= 0 ? "+" : "") + String (c, 2));
    const float bal = st.balanceDb.load();
    balanceMeter.setValue (bal / 12.0f, silent,
                           silent ? String ("--") : std::abs (bal) < 0.05f ? String ("C") : (bal < 0 ? "L " : "R ") + String (std::abs (bal), 1));
    for (size_t i = 0; i < 3; ++i)
    {
        const float v = st.bandCorrelation[i].load();
        const bool s = st.bandSilent[i].load();
        bandMeters[i].setValue (v, s, s ? String ("--") : (v >= 0 ? "+" : "") + String (v, 2));
    }
    const float w = silent ? 0.0f : st.widthPercent.load();
    widthShown += (w - widthShown) * 0.25f;
    if (w >= 990.0f) widthShown = w;
    repaint (widthReadout);

    mono->setToggleState (proc.apvts.getRawParameterValue ("mono")->load() > 0.5f, dontSendNotification);
    if (proc.getCurrentProgram() != lastProgram)
    {
        lastProgram = proc.getCurrentProgram();
        presetBox.setSelectedItemIndex (lastProgram, dontSendNotification);
    }
}
