#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace essential {

// The six folders of the sample library, in the order the plugin shows them.
inline const juce::StringArray categoryNames { "Acoustic", "Bass", "Electric", "Muted", "Nylon", "Rock" };

// The sound bank is downloaded separately (zip on swell-audio's R2), unzipped anywhere, then picked once.
inline const char* bankFolderName = "Swell Essential Guitar - Banque";
inline const char* bankDownloadUrl = "https://pub-ddab5865ec344cc0b88b141aca466d60.r2.dev/Swell%20Essential%20Guitar/Swell-Essential-Guitar-Library.zip";
inline const char* bankSize = "265 Mo";
juce::File installedLibrary();                       // shared folder of the machine
inline const char* defaultLibraryPath = "/Volumes/HDD/Swell eseential Guitar Sample"; // studio WAV library (Mac)
juce::File defaultLibrary();                          // first usual place that holds a bank
juce::File resolveBank(const juce::File& chosen);     // accepts the bank, its parent, or the unzip folder
bool looksLikeBank(const juce::File& dir);

struct PresetInfo {
    juce::String name;     // folder name on disk
    juce::String display;  // name shown in the UI
    juce::File folder;
    juce::String key() const; // "Category/Folder", used for favourites
};

struct Category {
    juce::String name;
    std::vector<PresetInfo> presets;
};

// Directory listing of the library: <root>/<Category>/<Preset>/<Preset>_<Note><Octave>_<Velocity>.wav
struct Catalog {
    std::vector<Category> categories;  // always the six categories, some possibly empty
    int total() const;
    bool empty() const { return total() == 0; }
    static Catalog scan(const juce::File& root);
};

// Unified typography for preset names ("DARK MOJAVELI" -> "Dark Mojaveli").
juce::String displayName(juce::String folderName);

// Parses "Steel Clean_A#3_127.wav". Octaves follow FL Studio / DirectWave (C5 = MIDI 60).
// Returns -1 when the name does not end in _<Note><Octave>_<Velocity>.
int midiNoteFromFileName(const juce::String& fileName);

// One preset fully loaded in memory (16-bit, to keep several presets cached).
struct LoadedPreset {
    struct Sample {
        std::vector<juce::int16> left, right;
        int frames = 0;
        double rate = 44100.0;
        int root = 60;
    };
    std::vector<Sample> samples;
    std::array<int, 128> sampleForNote {}; // nearest recorded sample for every MIDI note
    int lowest = 127, highest = 0;         // recorded range
    float level = 1.0f;                    // gain that brings the preset to the common loudness
    juce::String key;
};

// Reads every WAV of a preset. `cancel` is polled between files, `progress` goes 0 -> 1.
std::unique_ptr<LoadedPreset> loadPreset(const PresetInfo& info, const std::function<bool()>& cancel,
                                         std::atomic<float>* progress = nullptr);

} // namespace essential
