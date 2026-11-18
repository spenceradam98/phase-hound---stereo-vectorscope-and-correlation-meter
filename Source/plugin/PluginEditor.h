#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "ScopeView.h"
#include "Themes.h"

// MONO: a click latches it on/off, holding it longer than ~0.35 s makes it momentary.
class MonoButton : public juce::TextButton
{
public:
    explicit MonoButton (juce::RangedAudioParameter& p) : juce::TextButton ("MONO"), param (p) {}
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void set (bool on);
    juce::RangedAudioParameter& param;
    bool wasOn = false;
    double downAt = 0;
};

class PhaseHoundEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PhaseHoundEditor (PhaseHoundProcessor&);
    ~PhaseHoundEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kDefaultW = 960, kDefaultH = 620;

    // For the snapshot tool: pull the queued audio and draw a few frames now.
    void pumpFrames (int frames);

private:
    void timerCallback() override;

    struct Knob
    {
        juce::Slider slider;
        juce::String label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };
    Knob& addKnob (const juce::String& id, const juce::String& label, bool bipolar = false);
    juce::TextButton& addToggle (juce::TextButton& b, const juce::String& id);
    void placeKnob (Knob&, juce::Rectangle<int>);
    void applyTheme (const juce::String& name);
    void drawPanel (juce::Graphics&, juce::Rectangle<int>, const juce::String& title);

    PhaseHoundProcessor& proc;
    ph::ThemedLookAndFeel lnf;

    ph::CloudView cloud;
    ph::BarMeter corrMeter { ph::BarMeter::Kind::Correlation, false };
    ph::BarMeter balanceMeter { ph::BarMeter::Kind::Balance, false };
    std::array<ph::BarMeter, 3> bandMeters { ph::BarMeter (ph::BarMeter::Kind::Correlation, true),
                                             ph::BarMeter (ph::BarMeter::Kind::Correlation, true),
                                             ph::BarMeter (ph::BarMeter::Kind::Correlation, true) };

    std::vector<std::unique_ptr<Knob>> knobs;
    Knob *persist = nullptr, *zoom = nullptr, *xlow = nullptr, *xhigh = nullptr, *width = nullptr, *bass = nullptr, *output = nullptr;

    juce::ComboBox presetBox, themeBox, sizeBox, modeBox, bandBox, drawBox;
    juce::TextButton prevPreset { "<" }, nextPreset { ">" }, freeze { "FREEZE" }, autoGain { "AUTO" },
                     swap { "SWAP L/R" }, invL { juce::String::fromUTF8 ("\xc3\x98 L") }, invR { juce::String::fromUTF8 ("\xc3\x98 R") };
    std::unique_ptr<MonoButton> mono;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttach, bandAttach, drawAttach;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;

    juce::Rectangle<int> header, displayPanel, bandPanel, fixPanel, widthReadout;
    std::vector<float> bufL, bufR;
    float widthShown = 0;
    int lastProgram = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhaseHoundEditor)
};
