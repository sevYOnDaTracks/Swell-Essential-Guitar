#pragma once
#include "Library.h"
#include "Dsp.h"
#include "Licence/SwellLicence.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

namespace essential {

class Processor final : public juce::AudioProcessor, private juce::Thread {
public:
    Processor();
    ~Processor() override;

    juce::AudioProcessorValueTreeState params;
    juce::MidiKeyboardState keyboard;
    juce::SharedResourcePointer<swell::Licence> licence;
    std::atomic<float> uiScale { 1.0f }; // window size, saved with the project

    // ---- Library & presets (message thread) ----
    const Catalog& catalog() const { return cat; }
    juce::File libraryRoot() const;
    void setLibraryRoot(const juce::File&);
    void selectPreset(int category, int preset);
    void stepPreset(int delta);   // crosses into the next/previous category at the ends
    int currentCategory() const { return selCategory.load(); }
    int currentPreset() const { return selPreset.load(); }
    const PresetInfo* currentInfo() const;
    const PresetInfo* info(int category, int preset) const;

    // ---- Favourites (shared by every instance, saved in the user's settings) ----
    bool isFavourite(int category, int preset) const;
    void toggleFavourite(int category, int preset);
    std::vector<std::pair<int, int>> favourites() const; // in catalog order
    void setSettingsFile(const juce::File&);              // tests use a temporary file

    // ---- Status for the editor ----
    std::atomic<bool> loading { false };
    std::atomic<float> loadProgress { 1.0f };
    std::atomic<int> loadedCategory { -1 }, loadedPreset { -1 };
    std::atomic<float> outputPeak { 0.0f };

    // ---- AudioProcessor ----
    void prepareToPlay(double, int) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // Waits for the requested preset (tests and offline use).
    bool waitUntilLoaded(int timeoutMs);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void run() override;
    LoadedPreset* fetch(const PresetInfo&, juce::uint32 wanted, bool foreground);
    void trimCache();
    std::pair<int, int> neighbour(int category, int preset, int delta) const;
    void handleMidi(const juce::MidiMessage&);
    void noteOn(int note, float velocity);
    void noteOff(int note);
    void renderVoices(juce::AudioBuffer<float>&, int start, int count);
    void resetVoices();
    void loadFavourites();
    void saveFavourites() const;
    float param(const char* id) const { return params.getRawParameterValue(id)->load(); }

    struct Voice {
        bool active = false, released = false, sustained = false;
        int note = 0, sample = -1;
        const LoadedPreset* source = nullptr;
        double pos = 0, step = 1;
        float gain = 0, env = 0, attackInc = 1, releaseDec = 0;
        float glide = 0, glideDec = 0, glideFactor = 1; // slide, in semitones from the target
        juce::uint32 age = 0;
    };
    static constexpr int maxVoices = 48;
    std::array<Voice, maxVoices> voices;
    juce::uint32 voiceClock = 0;
    bool pedal = false;

    // Presets kept in memory by the loader thread. The audio thread only sees raw pointers:
    // `current` for new notes, and each voice's own `source` until it ends, so switching
    // preset never cuts what is already sounding.
    std::vector<std::unique_ptr<LoadedPreset>> cache;   // loader thread only
    std::vector<juce::uint32> cacheUse;
    juce::uint32 useClock = 0;
    LoadedPreset* current = nullptr;
    juce::SpinLock presetLock;

    Catalog cat;
    mutable juce::CriticalSection rootLock;
    juce::File root { defaultLibrary() };
    std::atomic<int> selCategory { 2 }, selPreset { 0 };
    std::atomic<juce::uint32> request { 1 };

    mutable juce::CriticalSection favouriteLock;
    juce::StringArray favouriteKeys;
    juce::File settingsFile;

    double rate = 44100.0;
    juce::dsp::StateVariableTPTFilter<float> tone;
    std::array<dsp::Biquad, 2> eqLowMid, eqAir;
    dsp::Chorus chorus;
    juce::AudioBuffer<float> delayLine;
    int delayWrite = 0;
    dsp::Reverb reverb;
    float limiterEnv = 0;
    juce::SmoothedValue<float> outGain, delayMix, toneHz, eqMix, chorusMix, reverbMix;
};

} // namespace essential
