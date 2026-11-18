#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine.h"
#include "Presets.h"

class PhaseHoundProcessor : public juce::AudioProcessor
{
public:
    PhaseHoundProcessor();
    ~PhaseHoundProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- UI hooks (message thread) ----
    const ph::Status& getStatus() const { return engine.getStatus(); }
    ph::StereoFifo& getScopeFifo() { return engine.getScopeFifo(); }
    ph::Display currentDisplay() const;
    double getSampleRateForUi() const { return getSampleRate() > 0 ? getSampleRate() : 48000.0; }

    juce::String getThemeName() const { return themeName; }
    void setThemeName (const juce::String& t) { themeName = t; }
    juce::Point<int> getUiSize() const { return uiSize; }
    void setUiSize (juce::Point<int> s) { uiSize = s; }

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    ph::Params readParams() const;

    ph::Engine engine;
    juce::String themeName { "Lab Scope" };
    juce::Point<int> uiSize { 0, 0 };   // 0 = default size
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhaseHoundProcessor)
};
