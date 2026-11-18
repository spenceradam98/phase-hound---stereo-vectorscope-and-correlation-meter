#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout PhaseHoundProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto pct = [] (float v, int) { return String (v, 0) + " %"; };
    auto db = [] (float v, int) { return (v > 0.04f ? "+" : "") + String (v, 1) + " dB"; };
    auto hz = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (v, 0) + " Hz"; };
    auto onOff = [] (const char* id, const char* name)
    {
        return std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, false);
    };

    // Display
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Display Mode",
                                                         StringArray { "Goniometer", "Lissajous", "Polar" }, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "band", 1 }, "Display Band",
                                                         StringArray { "Full", "Low", "Mid", "High" }, 0));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "persist", 1 }, "Persistence",
                                                        NormalisableRange<float> (0, 100, 0.1f), 50.0f,
                                                        AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "zoom", 1 }, "Zoom",
                                                        NormalisableRange<float> (-12, 24, 0.1f), 0.0f,
                                                        AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "autogain", 1 }, "Auto Gain", true));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "draw", 1 }, "Draw", StringArray { "Dots", "Lines" }, 0));

    // Meters
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "xlow", 1 }, "Low/Mid Crossover",
                                                        NormalisableRange<float> (40, 1000, 1, 0.4f), 200.0f,
                                                        AudioParameterFloatAttributes().withStringFromValueFunction (hz)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "xhigh", 1 }, "Mid/High Crossover",
                                                        NormalisableRange<float> (1000, 12000, 1, 0.4f), 3000.0f,
                                                        AudioParameterFloatAttributes().withStringFromValueFunction (hz)));

    // Fixes
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "width", 1 }, "Width",
                                                        NormalisableRange<float> (0, 200, 0.1f), 100.0f,
                                                        AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "bassmono", 1 }, "Bass Mono",
                                                        NormalisableRange<float> (55, 300, 1), 55.0f,
                                                        AudioParameterFloatAttributes().withStringFromValueFunction (
                                                            [] (float v, int) { return v < ph::kBassMonoMinHz ? String ("Off") : String (v, 0) + " Hz"; })));
    p.push_back (onOff ("mono", "Mono Check"));
    p.push_back (onOff ("swap", "Swap L/R"));
    p.push_back (onOff ("invl", "Polarity L"));
    p.push_back (onOff ("invr", "Polarity R"));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output",
                                                        NormalisableRange<float> (-24, 12, 0.1f), 0.0f,
                                                        AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    return { p.begin(), p.end() };
}

PhaseHoundProcessor::PhaseHoundProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PhaseHound", createLayout())
{
}

bool PhaseHoundProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return in == out;
}

void PhaseHoundProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.setParams (readParams());
    engine.prepare (sampleRate, samplesPerBlock);
}

ph::Params PhaseHoundProcessor::readParams() const
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    ph::Params p;
    p.width = v ("width") / 100.0f;
    p.bassMonoHz = ph::Engine::bassMonoFromKnob (v ("bassmono"));
    p.monoCheck = v ("mono") > 0.5f;
    p.swapLR = v ("swap") > 0.5f;
    p.invertL = v ("invl") > 0.5f;
    p.invertR = v ("invr") > 0.5f;
    p.outputDb = v ("output");
    p.xoverLowHz = v ("xlow");
    p.xoverHighHz = v ("xhigh");
    p.displayBand = (ph::Band) (int) v ("band");
    return p;
}

ph::Display PhaseHoundProcessor::currentDisplay() const
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    ph::Display d;
    d.mode = (ph::DisplayMode) (int) v ("mode");
    d.persistence = v ("persist") / 100.0f;
    d.zoomDb = v ("zoom");
    d.autoGain = v ("autogain") > 0.5f;
    d.dots = (int) v ("draw") == 0;
    return d;
}

void PhaseHoundProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int inCh = getTotalNumInputChannels(), outCh = getTotalNumOutputChannels();
    for (int c = inCh; c < outCh; ++c) buffer.clear (c, 0, buffer.getNumSamples());
    const int ch = juce::jmin (2, inCh, outCh);
    if (ch == 0) return;
    engine.setParams (readParams());
    engine.process (buffer.getArrayOfReadPointers(), buffer.getArrayOfWritePointers(), ch, buffer.getNumSamples());
}

int PhaseHoundProcessor::getNumPrograms() { return (int) ph::factoryPresets().size(); }

const juce::String PhaseHoundProcessor::getProgramName (int index)
{
    const auto& ps = ph::factoryPresets();
    return juce::isPositiveAndBelow (index, (int) ps.size()) ? juce::String (ps[(size_t) index].name) : juce::String();
}

void PhaseHoundProcessor::setCurrentProgram (int index)
{
    const auto& ps = ph::factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) ps.size())) return;
    currentProgram = index;
    const auto& pr = ps[(size_t) index];
    auto set = [this] (const char* id, float value)
    {
        if (auto* prm = apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (value));
    };
    set ("mode", (float) (int) pr.display.mode);
    set ("persist", pr.display.persistence * 100.0f);
    set ("zoom", pr.display.zoomDb);
    set ("autogain", pr.display.autoGain ? 1.0f : 0.0f);
    set ("draw", pr.display.dots ? 0.0f : 1.0f);
    set ("band", (float) (int) pr.params.displayBand);
    set ("xlow", pr.params.xoverLowHz);
    set ("xhigh", pr.params.xoverHighHz);
    set ("width", pr.params.width * 100.0f);
    set ("bassmono", pr.params.bassMonoHz > 0 ? pr.params.bassMonoHz : 55.0f);
    set ("mono", pr.params.monoCheck ? 1.0f : 0.0f);
    set ("swap", pr.params.swapLR ? 1.0f : 0.0f);
    set ("invl", pr.params.invertL ? 1.0f : 0.0f);
    set ("invr", pr.params.invertR ? 1.0f : 0.0f);
    set ("output", pr.params.outputDb);
}

void PhaseHoundProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    state.setProperty ("theme", themeName, nullptr);
    state.setProperty ("uiW", uiSize.x, nullptr);
    state.setProperty ("uiH", uiSize.y, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void PhaseHoundProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            currentProgram = tree.getProperty ("program", 0);
            themeName = tree.getProperty ("theme", "Lab Scope").toString();
            uiSize = { (int) tree.getProperty ("uiW", 0), (int) tree.getProperty ("uiH", 0) };
            apvts.replaceState (tree);
        }
}

juce::AudioProcessorEditor* PhaseHoundProcessor::createEditor() { return new PhaseHoundEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PhaseHoundProcessor(); }
