#include "Library.h"
#include "Dsp.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace essential {
using namespace juce;

// Presets removed from the plugin (folders stay on disk). Format: "Category/Folder".
static const StringArray hiddenPresets {
    "Electric/Like Bass",
    "Electric/Synth Guitar",
    "Muted/Mute Me",
    "Nylon/Happyness",
    "Electric/Conviction",
};

// Presets shown first in their category (the rest follow in alphabetical order). Format: "Category/Folder".
static const StringArray firstPresets {
    "Electric/Guitar Solo",
    "Acoustic/Classic Acoustic",
};

// Exported files whose audio does not match their note name; the note is transposed
// from its neighbours instead. Format: "Category/Folder/File.wav".
static const StringArray badSamples {
    "Nylon/STEREO NYLON/STEREO NYLON_F5_96.wav", // sounds two octaves higher (F7)
};

static const char* audioPattern = "*.wav;*.flac";

bool looksLikeBank(const File& dir)
{
    if (! dir.isDirectory()) return false;
    for (auto& c : categoryNames) if (dir.getChildFile(c).isDirectory()) return true;
    return false;
}

File resolveBank(const File& chosen)
{
    if (looksLikeBank(chosen)) return chosen;
    if (looksLikeBank(chosen.getChildFile(bankFolderName))) return chosen.getChildFile(bankFolderName);
    // The user may pick the folder the zip was unpacked into, or one level above: look two levels down.
    for (auto& a : chosen.findChildFiles(File::findDirectories, false)) {
        if (looksLikeBank(a)) return a;
        for (auto& b : a.findChildFiles(File::findDirectories, false))
            if (looksLikeBank(b)) return b;
    }
    return chosen;
}

File installedLibrary()
{
   #if JUCE_WINDOWS
    return File::getSpecialLocation(File::commonDocumentsDirectory).getChildFile("Swell Audio").getChildFile("Essential Guitar");
   #else
    return File("/Users/Shared/Swell Audio/Essential Guitar");
   #endif
}

File defaultLibrary()
{
    const auto home = File::getSpecialLocation(File::userHomeDirectory);
    const auto music = File::getSpecialLocation(File::userMusicDirectory);
    const File places[] {
        installedLibrary(),
        music.getChildFile("Swell Audio").getChildFile(bankFolderName),
        music.getChildFile(bankFolderName),
        home.getChildFile("Downloads").getChildFile(bankFolderName),
        home.getChildFile("Downloads").getChildFile("Swell-Essential-Guitar-Library").getChildFile(bankFolderName),
        home.getChildFile("Desktop").getChildFile(bankFolderName),
    };
    for (auto& p : places) if (looksLikeBank(p)) return p;
   #if JUCE_MAC
    if (looksLikeBank(File(defaultLibraryPath))) return File(defaultLibraryPath);
   #endif
    return installedLibrary();
}

String PresetInfo::key() const { return folder.getParentDirectory().getFileName() + "/" + name; }

int Catalog::total() const
{
    int n = 0;
    for (auto& c : categories) n += (int) c.presets.size();
    return n;
}

String displayName(String folderName)
{
    // One export folder is called "Soft Cleandwp": the DirectWave extension leaked into the name.
    if (folderName.endsWithIgnoreCase("dwp") && folderName.length() > 3 && ! folderName.endsWithIgnoreCase(" dwp"))
        folderName = folderName.dropLastCharacters(3);
    folderName = folderName.trim();

    // Each word capitalised, the rest in lower case. A few words keep a fixed spelling.
    static const StringPairArray fixed = [] {
        StringPairArray m(false);
        m.set("rnb", "RnB");
        m.set("ngatti", "NGatti");
        return m;
    }();
    String out, word;
    auto flush = [&] {
        if (word.isEmpty()) return;
        auto low = word.toLowerCase();
        out << (fixed.containsKey(low) ? fixed[low] : low.substring(0, 1).toUpperCase() + low.substring(1));
        word.clear();
    };
    for (auto ch : folderName) {
        if (ch == ' ' || ch == '-' || ch == '_') { flush(); out << ch; continue; }
        word << ch;
    }
    flush();
    return out;
}

Catalog Catalog::scan(const File& root)
{
    Catalog cat;
    for (auto& name : categoryNames) {
        Category c;
        c.name = name;
        auto dir = root.getChildFile(name);
        if (dir.isDirectory()) {
            for (auto& sub : dir.findChildFiles(File::findDirectories, false)) {
                if (sub.getFileName().startsWith(".")) continue;
                if (hiddenPresets.contains(name + "/" + sub.getFileName(), true)) continue;
                if (sub.findChildFiles(File::findFiles, false, audioPattern).isEmpty()) continue;
                c.presets.push_back({ sub.getFileName(), displayName(sub.getFileName()), sub });
            }
            std::sort(c.presets.begin(), c.presets.end(), [&name](const PresetInfo& a, const PresetInfo& b) {
                const bool fa = firstPresets.contains(name + "/" + a.name, true);
                const bool fb = firstPresets.contains(name + "/" + b.name, true);
                if (fa != fb) return fa;
                return a.display.compareNatural(b.display) < 0;
            });
        }
        cat.categories.push_back(std::move(c));
    }
    return cat;
}

int midiNoteFromFileName(const String& fileName)
{
    auto base = fileName.upToLastOccurrenceOf(".", false, false);
    auto velocityPart = base.fromLastOccurrenceOf("_", false, false);
    auto rest = base.upToLastOccurrenceOf("_", false, false);
    auto notePart = rest.fromLastOccurrenceOf("_", false, false).trim().toUpperCase();
    if (velocityPart.isEmpty() || ! velocityPart.containsOnly("0123456789") || notePart.length() < 2)
        return -1;

    static const int pitchClass[] { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
    auto letter = notePart[0];
    if (letter < 'A' || letter > 'G') return -1;
    int pc = pitchClass[letter - 'A'];
    int i = 1;
    if (notePart[i] == '#') { ++pc; ++i; }
    else if (notePart[i] == 'B' && notePart.length() > 2) { --pc; ++i; }

    auto octaveText = notePart.substring(i);
    if (octaveText.isEmpty() || ! octaveText.containsOnly("-0123456789")) return -1;
    int note = octaveText.getIntValue() * 12 + pc; // FL Studio: C5 = 60
    return note >= 0 && note < 128 ? note : -1;
}

std::unique_ptr<LoadedPreset> loadPreset(const PresetInfo& info, const std::function<bool()>& cancel,
                                         std::atomic<float>* progress)
{
    auto result = std::make_unique<LoadedPreset>();
    result->sampleForNote.fill(-1);
    result->key = info.key();

    AudioFormatManager formats;
    formats.registerBasicFormats(); // WAV (studio library) and FLAC (installed bank)
    std::array<int, 128> byNote;
    byNote.fill(-1);
    std::vector<float> loudness;

    auto files = info.folder.findChildFiles(File::findFiles, false, audioPattern);
    files.sort();

    // A folder may hold two different recordings (Guitar Solo also contains "Guitar Solo Suite_*",
    // another DirectWave program with a brighter, quieter tone). Mixing them makes single notes stand out,
    // so only the main recording (the most files) is kept; its neighbours fill the other notes.
    std::map<String, int> prefixes;
    auto prefixOf = [](const File& f) {
        auto base = f.getFileNameWithoutExtension();
        return base.upToLastOccurrenceOf("_", false, false).upToLastOccurrenceOf("_", false, false);
    };
    for (auto& f : files) if (! f.getFileName().startsWith(".")) ++prefixes[prefixOf(f)];
    String mainPrefix;
    int most = 0;
    for (auto& [p, n] : prefixes) if (n > most) { most = n; mainPrefix = p; }
    AudioBuffer<float> temp;
    for (int fi = 0; fi < files.size(); ++fi) {
        auto& f = files.getReference(fi);
        if (progress != nullptr) progress->store((float) fi / (float) jmax(1, files.size()));
        if (cancel()) return {};
        if (f.getFileName().startsWith(".")) continue;
        if (prefixOf(f) != mainPrefix) continue;
        if (badSamples.contains(info.key() + "/" + f.getFileNameWithoutExtension() + ".wav", true)) continue;
        int note = midiNoteFromFileName(f.getFileName());
        if (note < 0 || byNote[(size_t) note] >= 0) continue;

        std::unique_ptr<AudioFormatReader> reader(formats.createReaderFor(f));
        if (reader == nullptr || reader->lengthInSamples < 16 || reader->lengthInSamples > 44100 * 60 * 2)
            continue;

        const int total = (int) reader->lengthInSamples;
        temp.setSize(2, total, false, false, true);
        if (! reader->read(&temp, 0, total, 0, true, true)) continue; // mono files fill both channels

        // Many recordings start with a few ms (up to ~35 ms) of string noise before the pick hits the
        // string: played live, the guitar feels late. The attack is found where the note first gets within
        // 20 dB of its peak, then followed back while the level (max over 8 ms, so a low string's zero
        // crossings don't count as silence) stays above -30 dB: string noise is dropped, a slow swell or a pick scratch is kept.
        // The sample starts 1 ms before that, faded in so the cut never clicks.
        const int skip = [&] {
            auto* l = temp.getReadPointer(0);
            auto* r = temp.getReadPointer(1);
            const float peak = jmax(temp.getMagnitude(0, 0, total), temp.getMagnitude(1, 0, total));
            if (peak <= 0.0f) return 0;
            const int ms = jmax(1, (int) (reader->sampleRate * 0.001));
            std::vector<float> block((size_t) (total / ms + 1), 0.0f); // peak of each 1 ms block
            for (int i = 0; i < total; ++i) block[(size_t) (i / ms)] = jmax(block[(size_t) (i / ms)], std::abs(l[i]), std::abs(r[i]));
            int attack = 0;
            while (attack < (int) block.size() && block[(size_t) attack] < peak * 0.1f) ++attack;
            int start = attack;
            for (; start > 0; --start) {
                float m = 0;
                for (int k = jmax(0, start - 8); k < start; ++k) m = jmax(m, block[(size_t) k]);
                if (m < peak * 0.0316f) break;
            }
            return jlimit(0, jmax(0, total - 16), (start - 1) * ms);
        }();
        const int frames = total - skip;
        if (skip > 0) {
            const int fade = jmin(frames, (int) (reader->sampleRate * 0.001) + 1);
            for (int c = 0; c < 2; ++c) {
                auto* d = temp.getWritePointer(c);
                std::memmove(d, d + skip, sizeof(float) * (size_t) frames);
                for (int i = 0; i < fade; ++i) d[i] *= (float) i / (float) fade;
            }
        }

        LoadedPreset::Sample s;
        s.rate = reader->sampleRate;
        s.root = note;
        s.frames = frames;
        s.left.resize((size_t) frames + 1);
        s.right.resize((size_t) frames + 1);
        double energy = 0;
        const int measured = jmin(frames, (int) (reader->sampleRate * 0.4)); // the attack is what the ear compares
        for (int c = 0; c < 2; ++c) {
            auto* src = temp.getReadPointer(c);
            auto& dst = c == 0 ? s.left : s.right;
            for (int i = 0; i < frames; ++i)
                dst[(size_t) i] = (int16) jlimit(-32767, 32767, (int) std::lround(src[i] * 32767.0f));
            dst[(size_t) frames] = 0;
            // Loudness as the ear hears it (close to the K-weighting used by LUFS meters):
            // lows count less, presence counts more.
            dsp::Biquad hp, shelf;
            hp.highpass(reader->sampleRate, 110.0);
            shelf.highShelf(reader->sampleRate, 1500.0, 4.0);
            for (int i = 0; i < measured; ++i) { float y = shelf.process(hp.process(src[i])); energy += y * y; }
        }
        loudness.push_back((float) std::sqrt(energy / (2.0 * jmax(1, measured))));

        byNote[(size_t) note] = (int) result->samples.size();
        result->lowest = jmin(result->lowest, note);
        result->highest = jmax(result->highest, note);
        result->samples.push_back(std::move(s));
    }
    if (result->samples.empty()) return {};

    // Every MIDI note plays: notes without their own sample are transposed from the nearest one.
    for (int n = 0; n < 128; ++n) {
        int best = -1;
        for (int d = 0; d < 128 && best < 0; ++d) {
            if (n - d >= 0 && byNote[(size_t) (n - d)] >= 0) best = byNote[(size_t) (n - d)];
            else if (n + d < 128 && byNote[(size_t) (n + d)] >= 0) best = byNote[(size_t) (n + d)];
        }
        result->sampleForNote[(size_t) n] = best;
    }

    // Loudness matching: the median RMS of the first 0.4 s of each note is brought to a common target.
    std::sort(loudness.begin(), loudness.end());
    const float median = loudness[loudness.size() / 2];
    constexpr float target = 0.16f;
    result->level = median > 1.0e-5f ? jlimit(0.25f, 16.0f, target / median) : 1.0f;
    if (progress != nullptr) progress->store(1.0f);
    return result;
}

} // namespace essential
