// Renders the editor in every theme (and each display mode) to PNG files for the docs.
// Build with -DPHASEHOUND_BUILD_SNAPSHOTS=ON; on Linux run under a display (e.g. xvfb-run).
// Usage: phasehound_snapshots <out-dir>
#include <random>
#include "../Source/plugin/PluginProcessor.h"
#include "../Source/plugin/PluginEditor.h"

namespace
{
// A small "mix": a centred bass and vocal-ish tone, a wide pad, and a slightly out-of-phase hi-hat band.
void fillTestBlock (juce::AudioBuffer<float>& buf, int64_t start, double sr, std::mt19937& rng)
{
    std::normal_distribution<float> n (0.0f, 1.0f);
    for (int i = 0; i < buf.getNumSamples(); ++i)
    {
        const double t = (double) (start + i) / sr;
        const float bass = 0.25f * (float) std::sin (2 * ph::kPi * 55 * t);
        const float lead = 0.12f * (float) std::sin (2 * ph::kPi * 330 * t + 0.4 * std::sin (2 * ph::kPi * 5 * t));
        const float padL = 0.10f * (float) std::sin (2 * ph::kPi * 220 * t);
        const float padR = 0.10f * (float) std::sin (2 * ph::kPi * 220 * t + 1.6);
        const float hat = 0.03f * n (rng);
        const float wide = 0.05f * n (rng);
        buf.setSample (0, i, bass + lead + padL + hat + wide);
        buf.setSample (1, i, bass + lead + padR - 0.6f * hat + 0.3f * wide + 0.04f * n (rng));
    }
}

void save (juce::Component& c, const juce::File& f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    f.deleteFile();
    juce::FileOutputStream out (f);
    juce::PNGImageFormat().writeImageToStream (img, out);
}

void runAudio (PhaseHoundProcessor& proc, PhaseHoundEditor& ed, int frames, int64_t& pos)
{
    std::mt19937 rng (1);
    juce::AudioBuffer<float> buf (2, 800);   // one 60 Hz frame at 48 kHz
    juce::MidiBuffer midi;
    for (int f = 0; f < frames; ++f)
    {
        fillTestBlock (buf, pos, 48000, rng);
        pos += buf.getNumSamples();
        proc.processBlock (buf, midi);
        ed.pumpFrames (1);
    }
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File dir (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File::getCurrentWorkingDirectory());
    dir.createDirectory();

    int64_t pos = 0;
    for (int ti = 0; ti < (int) ph::getThemes().size(); ++ti)
    {
        PhaseHoundProcessor proc;
        proc.setCurrentProgram (0);
        proc.prepareToPlay (48000, 800);
        proc.setThemeName (ph::getTheme (ti).name);
        std::unique_ptr<PhaseHoundEditor> ed (dynamic_cast<PhaseHoundEditor*> (proc.createEditor()));
        ed->setSize (PhaseHoundEditor::kDefaultW, PhaseHoundEditor::kDefaultH);
        runAudio (proc, *ed, 90, pos);
        save (*ed, dir.getChildFile ("theme-" + juce::String (ti).paddedLeft ('0', 2) + "-"
                                     + ph::getTheme (ti).name.replace (" ", "-").toLowerCase() + ".png"));
    }

    // display modes and a couple of presets in the default theme
    const std::pair<int, const char*> shots[] = { { 2, "preset-phosphor-classic" }, { 3, "preset-bass-check" },
                                                  { 4, "preset-mono-compatibility" }, { 6, "preset-lissajous" } };
    for (auto& [preset, name] : shots)
    {
        PhaseHoundProcessor proc;
        proc.setCurrentProgram (preset);
        proc.prepareToPlay (48000, 800);
        std::unique_ptr<PhaseHoundEditor> ed (dynamic_cast<PhaseHoundEditor*> (proc.createEditor()));
        ed->setSize (PhaseHoundEditor::kDefaultW, PhaseHoundEditor::kDefaultH);
        runAudio (proc, *ed, 90, pos);
        save (*ed, dir.getChildFile (juce::String (name) + ".png"));
    }
    return 0;
}
