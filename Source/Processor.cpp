#include "Processor.h"
#include "Editor.h"
#include <cmath>

namespace essential {
using namespace juce;

static constexpr size_t cacheSize = 5;   // current preset, its neighbours and recent ones

AudioProcessorValueTreeState::ParameterLayout Processor::layout()
{
    AudioProcessorValueTreeState::ParameterLayout p;
    auto add = [&](const char* id, const char* name, float lo, float hi, float step, float skew, float def, const char* unit) {
        p.add(std::make_unique<AudioParameterFloat>(ParameterID { id, 1 }, name,
                                                    NormalisableRange<float>(lo, hi, step, skew), def,
                                                    AudioParameterFloatAttributes().withLabel(unit)));
    };
    add("tone", "Tone", 0.0f, 100.0f, 0.1f, 1.0f, 72.0f, "%");
    add("attack", "Attack", 0.5f, 1000.0f, 0.1f, 0.3f, 2.0f, "ms");
    add("release", "Release", 20.0f, 4000.0f, 1.0f, 0.4f, 450.0f, "ms");
    add("chorus", "Chorus", 0.0f, 100.0f, 0.1f, 1.0f, 0.0f, "%");
    add("delay", "Delay", 0.0f, 100.0f, 0.1f, 1.0f, 18.0f, "%");
    add("reverb", "Reverb", 0.0f, 100.0f, 0.1f, 1.0f, 25.0f, "%");
    add("output", "Volume", -36.0f, 6.0f, 0.1f, 1.0f, -3.0f, "dB");
    p.add(std::make_unique<AudioParameterBool>(ParameterID { "eq", 1 }, "EQ", false));
    // Slide: a note played while another is still held glides from that note (legato).
    p.add(std::make_unique<AudioParameterBool>(ParameterID { "slide", 1 }, "Slide", false));
    return p;
}

Processor::Processor()
    : AudioProcessor(BusesProperties().withOutput("Output", AudioChannelSet::stereo(), true)),
      Thread("Swell Essential Guitar loader"),
      params(*this, nullptr, "SwellEssentialGuitar", layout())
{
   #if JUCE_MAC
    auto base = File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("Application Support");
   #else
    auto base = File::getSpecialLocation(File::userApplicationDataDirectory);
   #endif
    settingsFile = base.getChildFile("Swell Audio").getChildFile("Essential Guitar").getChildFile("favourites.txt");
    loadFavourites();

    cat = Catalog::scan(root);
    // Start on Electric / Steel Clean when it exists, otherwise on the first preset found.
    int c = categoryNames.indexOf("Electric"), pr = 0;
    if (c >= 0) {
        auto& list = cat.categories[(size_t) c].presets;
        for (int i = 0; i < (int) list.size(); ++i)
            if (list[(size_t) i].name == "Steel Clean") pr = i;
        if (list.empty()) c = -1;
    }
    if (c < 0)
        for (int i = 0; i < (int) cat.categories.size() && c < 0; ++i)
            if (! cat.categories[(size_t) i].presets.empty()) c = i;
    selCategory = jmax(0, c);
    selPreset = pr;
    startThread();
    notify();
}

Processor::~Processor()
{
    signalThreadShouldExit();
    notify();
    stopThread(4000);
}

// ---------------------------------------------------------------- library

File Processor::libraryRoot() const
{
    const ScopedLock l(rootLock);
    return root;
}

void Processor::setLibraryRoot(const File& chosen)
{
    const File f = resolveBank(chosen);
    auto oldCategory = selCategory.load();
    String oldName = currentInfo() != nullptr ? currentInfo()->name : String();
    {
        const ScopedLock l(rootLock);
        root = f;
        cat = Catalog::scan(f);
    }
    // Keep the same preset if the new folder has it, else the first one of the category.
    int c = jlimit(0, (int) cat.categories.size() - 1, oldCategory), pr = 0;
    auto& list = cat.categories[(size_t) c].presets;
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].name == oldName) pr = i;
    if (list.empty())
        for (int i = 0; i < (int) cat.categories.size(); ++i)
            if (! cat.categories[(size_t) i].presets.empty()) { c = i; break; }
    selectPreset(c, pr);
}

const PresetInfo* Processor::info(int c, int p) const
{
    if (c < 0 || c >= (int) cat.categories.size()) return nullptr;
    auto& list = cat.categories[(size_t) c].presets;
    return p >= 0 && p < (int) list.size() ? &list[(size_t) p] : nullptr;
}

const PresetInfo* Processor::currentInfo() const { return info(selCategory.load(), selPreset.load()); }

void Processor::selectPreset(int category, int presetIndex)
{
    selCategory = category;
    selPreset = presetIndex;
    request.fetch_add(1);
    notify();
}

std::pair<int, int> Processor::neighbour(int c, int p, int delta) const
{
    const int n = (int) cat.categories.size();
    if (cat.empty() || c < 0 || c >= n) return { c, p };
    p += delta;
    // Walk to the neighbouring non-empty category when leaving this one.
    while (p < 0 || p >= (int) cat.categories[(size_t) c].presets.size()) {
        c = (c + (delta > 0 ? 1 : -1) + n) % n;
        auto size = (int) cat.categories[(size_t) c].presets.size();
        if (size == 0) continue;
        p = delta > 0 ? 0 : size - 1;
    }
    return { c, p };
}

void Processor::stepPreset(int delta)
{
    if (cat.empty()) return;
    auto [c, p] = neighbour(selCategory.load(), selPreset.load(), delta);
    selectPreset(c, p);
}

// ---------------------------------------------------------------- favourites

void Processor::setSettingsFile(const File& f)
{
    settingsFile = f;
    loadFavourites();
}

void Processor::loadFavourites()
{
    const ScopedLock l(favouriteLock);
    favouriteKeys.clear();
    if (settingsFile.existsAsFile()) {
        favouriteKeys.addLines(settingsFile.loadFileAsString());
        favouriteKeys.removeEmptyStrings();
        favouriteKeys.removeDuplicates(false);
    }
}

void Processor::saveFavourites() const
{
    const ScopedLock l(favouriteLock);
    settingsFile.getParentDirectory().createDirectory();
    settingsFile.replaceWithText(favouriteKeys.joinIntoString("\n"));
}

bool Processor::isFavourite(int c, int p) const
{
    auto* i = info(c, p);
    const ScopedLock l(favouriteLock);
    return i != nullptr && favouriteKeys.contains(i->key());
}

void Processor::toggleFavourite(int c, int p)
{
    auto* i = info(c, p);
    if (i == nullptr) return;
    loadFavourites(); // another instance may have changed the list
    {
        const ScopedLock l(favouriteLock);
        if (favouriteKeys.contains(i->key())) favouriteKeys.removeString(i->key());
        else favouriteKeys.add(i->key());
    }
    saveFavourites();
}

std::vector<std::pair<int, int>> Processor::favourites() const
{
    std::vector<std::pair<int, int>> out;
    const ScopedLock l(favouriteLock);
    for (int c = 0; c < (int) cat.categories.size(); ++c)
        for (int p = 0; p < (int) cat.categories[(size_t) c].presets.size(); ++p)
            if (favouriteKeys.contains(cat.categories[(size_t) c].presets[(size_t) p].key())) out.push_back({ c, p });
    return out;
}

// ---------------------------------------------------------------- loader thread

LoadedPreset* Processor::fetch(const PresetInfo& inf, juce::uint32 wanted, bool foreground)
{
    const auto path = inf.folder.getFullPathName();
    for (size_t i = 0; i < cache.size(); ++i)
        if (cache[i]->key == path) { cacheUse[i] = ++useClock; return cache[i].get(); }

    auto loaded = loadPreset(inf, [&] { return threadShouldExit() || request.load() != wanted; },
                             foreground ? &loadProgress : nullptr);
    if (loaded == nullptr) return nullptr;
    loaded->key = path;
    cache.push_back(std::move(loaded));
    cacheUse.push_back(++useClock);
    return cache.back().get();
}

void Processor::trimCache()
{
    while (cache.size() > cacheSize) {
        int victim = -1;
        std::unique_ptr<LoadedPreset> doomed;
        {
            const SpinLock::ScopedLockType l(presetLock);
            for (size_t i = 0; i < cache.size(); ++i) {
                auto* p = cache[i].get();
                if (p == current) continue;
                bool playing = false;
                for (auto& v : voices) if (v.active && v.source == p) { playing = true; break; }
                if (playing) continue;
                if (victim < 0 || cacheUse[i] < cacheUse[(size_t) victim]) victim = (int) i;
            }
            if (victim >= 0) {
                doomed = std::move(cache[(size_t) victim]);
                cache.erase(cache.begin() + victim);
                cacheUse.erase(cacheUse.begin() + victim);
            }
        }
        if (victim < 0) break; // everything still sounding: try again later
        doomed.reset();        // freed here, never on the audio thread
    }
}

void Processor::run()
{
    juce::uint32 served = 0, prefetched = 0;
    while (! threadShouldExit()) {
        const auto wanted = request.load();
        const int c = selCategory.load(), p = selPreset.load();

        if (wanted != served) {
            served = wanted;
            PresetInfo inf;
            bool valid = false;
            {
                const ScopedLock l(rootLock);
                if (auto* i = info(c, p)) { inf = *i; valid = true; }
            }
            if (! valid) { loadedCategory = -1; loadedPreset = -1; loading = false; continue; }

            loading = true;
            loadProgress = 0.0f;
            auto* next = fetch(inf, wanted, true);
            if (request.load() != wanted || threadShouldExit()) continue; // a newer choice is waiting
            {
                // The swap only changes which preset new notes use: sounding notes carry on.
                const SpinLock::ScopedLockType l(presetLock);
                current = next;
            }
            loadedCategory = next != nullptr ? c : -1;
            loadedPreset = next != nullptr ? p : -1;
            loadProgress = 1.0f;
            loading = false;
            trimCache();
            continue;
        }

        // Idle: preload the presets the arrows lead to, so they switch instantly.
        if (prefetched != served && ! cat.empty()) {
            prefetched = served;
            for (int delta : { 1, -1 }) {
                PresetInfo inf;
                bool valid = false;
                {
                    const ScopedLock l(rootLock);
                    auto [nc, np] = neighbour(c, p, delta);
                    if (auto* i = info(nc, np)) { inf = *i; valid = true; }
                }
                if (valid && request.load() == served) fetch(inf, served, false);
            }
            trimCache();
            continue;
        }
        trimCache();
        wait(40);
    }
}

bool Processor::waitUntilLoaded(int timeoutMs)
{
    auto end = Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (Time::getMillisecondCounter() < end) {
        if (! loading.load() && loadedCategory.load() == selCategory.load() && loadedPreset.load() == selPreset.load())
            return true;
        Thread::sleep(5);
    }
    return false;
}

// ---------------------------------------------------------------- audio

void Processor::prepareToPlay(double sampleRate, int blockSize)
{
    const SpinLock::ScopedLockType l(presetLock);
    rate = sampleRate;
    resetVoices();
    keyboard.reset();

    juce::dsp::ProcessSpec spec { sampleRate, (uint32) jmax(1, blockSize), 2 };
    tone.prepare(spec);
    tone.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    tone.setResonance(0.62f);

    for (auto& b : eqLowMid) { b.peak(sampleRate, 320.0, 0.8, -3.5); b.reset(); }
    for (auto& b : eqAir) { b.highShelf(sampleRate, 9000.0, 1.0); b.reset(); }
    chorus.prepare(sampleRate);

    delayLine.setSize(2, (int) std::ceil(sampleRate * 2.5) + 4);
    delayLine.clear();
    delayWrite = 0;

    reverb.prepare(sampleRate);
    reverb.reset();
    limiterEnv = 0;

    auto init = [&](SmoothedValue<float>& s, float v, double t) { s.reset(sampleRate, t); s.setCurrentAndTargetValue(v); };
    init(outGain, Decibels::decibelsToGain(param("output")), 0.02);
    init(delayMix, param("delay") / 100.0f, 0.03);
    init(toneHz, 600.0f * std::pow(20000.0f / 600.0f, param("tone") / 100.0f), 0.03);
    init(eqMix, param("eq") > 0.5f ? 1.0f : 0.0f, 0.05);
    init(chorusMix, param("chorus") / 100.0f, 0.05);
    init(reverbMix, param("reverb") / 100.0f, 0.05);
}

void Processor::releaseResources()
{
    const SpinLock::ScopedLockType l(presetLock);
    resetVoices();
}

bool Processor::isBusesLayoutSupported(const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == AudioChannelSet::stereo();
}

void Processor::resetVoices()
{
    for (auto& v : voices) v.active = false;
    pedal = false;
}

void Processor::noteOn(int note, float velocity)
{
    if (current == nullptr) return;
    int s = current->sampleForNote[(size_t) note];
    if (s < 0) return;

    const float quickFade = (float) std::exp(std::log(0.001) / (0.035 * rate));
    const float slideFade = (float) std::exp(std::log(0.001) / (0.07 * rate));
    float slideFrom = 0;
    if (param("slide") > 0.5f) {
        // Slide (legato): if a key is still held, the new note starts at that note's pitch and glides
        // to its own, while the held note is let go, as when one finger slides along a string.
        Voice* held = nullptr;
        for (auto& v : voices)
            if (v.active && ! v.released && ! v.sustained && v.note != note && (held == nullptr || v.age > held->age)) held = &v;
        if (held != nullptr && std::abs(held->note - note) <= 12) {
            slideFrom = (float) (held->note - note);
            for (auto& v : voices)
                if (v.active && ! v.released && v.note == held->note) { v.released = true; v.sustained = false; v.releaseDec = slideFade; }
        }
    }
    // A repeated note fades the previous one quickly, like a string being struck again.
    for (auto& v : voices)
        if (v.active && v.note == note && ! v.released) { v.released = true; v.sustained = false; v.releaseDec = quickFade; }

    int slot = -1;
    for (int i = 0; i < maxVoices && slot < 0; ++i) if (! voices[(size_t) i].active) slot = i;
    if (slot < 0) { // steal the oldest, released voices first
        slot = 0;
        for (int i = 1; i < maxVoices; ++i) {
            auto& a = voices[(size_t) i]; auto& b = voices[(size_t) slot];
            if ((a.released && ! b.released) || (a.released == b.released && a.age < b.age)) slot = i;
        }
    }

    auto& sample = current->samples[(size_t) s];
    auto& v = voices[(size_t) slot];
    v = {};
    v.active = true;
    v.note = note;
    v.sample = s;
    v.source = current;
    v.step = sample.rate / rate * std::pow(2.0, (note - sample.root) / 12.0);
    v.gain = current->level * (0.18f + 0.82f * std::pow(velocity, 1.4f)) / 32767.0f;
    const double attackSamples = jmax(1.0, param("attack") * 0.001 * rate);
    v.attackInc = (float) (1.0 / attackSamples);
    v.env = attackSamples <= 1.0 ? 1.0f : 0.0f;
    if (slideFrom != 0) {
        v.glide = slideFrom;
        v.glideDec = slideFrom / (float) (0.14 * rate);
        v.glideFactor = std::exp2(slideFrom / 12.0f);
    }
    v.age = ++voiceClock;
}

void Processor::noteOff(int note)
{
    const float dec = (float) std::exp(std::log(0.001) / (jmax(0.02f, param("release") * 0.001f) * rate));
    for (auto& v : voices) {
        if (! v.active || v.released || v.note != note) continue;
        if (pedal) { v.sustained = true; continue; }
        v.released = true;
        v.releaseDec = dec;
    }
}

void Processor::handleMidi(const MidiMessage& m)
{
    if (m.isNoteOn()) noteOn(m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff()) noteOff(m.getNoteNumber());
    else if (m.isSustainPedalOn()) pedal = true;
    else if (m.isSustainPedalOff()) {
        pedal = false;
        const float dec = (float) std::exp(std::log(0.001) / (jmax(0.02f, param("release") * 0.001f) * rate));
        for (auto& v : voices)
            if (v.active && v.sustained) { v.sustained = false; v.released = true; v.releaseDec = dec; }
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff()) resetVoices();
}

void Processor::renderVoices(AudioBuffer<float>& out, int start, int count)
{
    auto* L = out.getWritePointer(0, start);
    auto* R = out.getWritePointer(1, start);
    for (auto& v : voices) {
        if (! v.active) continue;
        auto& s = v.source->samples[(size_t) v.sample];
        const int16* a = s.left.data();
        const int16* b = s.right.data();
        const int last = s.frames - 1;
        for (int i = 0; i < count; ++i) {
            int idx = (int) v.pos;
            if (idx >= last) { v.active = false; break; }
            float frac = (float) (v.pos - idx);
            if (v.released) {
                v.env *= v.releaseDec;
                if (v.env < 0.0005f) { v.active = false; break; }
            } else if (v.env < 1.0f) {
                v.env = jmin(1.0f, v.env + v.attackInc);
            }
            const float g = v.gain * v.env;
            L[i] += g * ((float) a[idx] + (float) (a[idx + 1] - a[idx]) * frac);
            R[i] += g * ((float) b[idx] + (float) (b[idx + 1] - b[idx]) * frac);
            if (v.glide != 0.0f) {
                v.glide -= v.glideDec;
                if ((v.glideDec > 0 && v.glide <= 0) || (v.glideDec < 0 && v.glide >= 0)) { v.glide = 0; v.glideFactor = 1; }
                else if ((i & 7) == 0) v.glideFactor = std::exp2(v.glide / 12.0f);
            }
            v.pos += v.step * v.glideFactor;
        }
    }
}

void Processor::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    keyboard.processNextMidiBuffer(midi, 0, n, true);
    buffer.clear();
    if (! licence->isUnlocked()) { outputPeak = 0.0f; return; } // not activated (or trial over): silence

    {
        // Held only for a pointer swap by the loader, so waiting here is a matter of microseconds.
        const SpinLock::ScopedLockType l(presetLock);
        int pos = 0;
        for (const auto meta : midi) {
            const int at = jlimit(0, n, meta.samplePosition);
            if (at > pos) renderVoices(buffer, pos, at - pos);
            pos = jmax(pos, at);
            handleMidi(meta.getMessage());
        }
        if (pos < n) renderVoices(buffer, pos, n - pos);
    }

    auto* L = buffer.getWritePointer(0);
    auto* R = buffer.getWritePointer(1);

    // Tone: low-pass from 600 Hz (dark) to 20 kHz (open).
    toneHz.setTargetValue(600.0f * std::pow(20000.0f / 600.0f, param("tone") / 100.0f));
    toneHz.skip(n);
    tone.setCutoffFrequency(jmin(toneHz.getCurrentValue(), (float) (rate * 0.45)));
    {
        juce::dsp::AudioBlock<float> block(buffer);
        tone.process(juce::dsp::ProcessContextReplacing<float>(block));
    }

    // EQ: a gentle dip in the low mids (and a touch of air), cross-faded when switched.
    eqMix.setTargetValue(param("eq") > 0.5f ? 1.0f : 0.0f);
    for (int i = 0; i < n; ++i) {
        const float m = eqMix.getNextValue();
        const float el = eqAir[0].process(eqLowMid[0].process(L[i]));
        const float er = eqAir[1].process(eqLowMid[1].process(R[i]));
        L[i] += m * (el - L[i]);
        R[i] += m * (er - R[i]);
    }

    // Chorus for width.
    chorusMix.setTargetValue(param("chorus") / 100.0f);
    chorusMix.skip(n);
    chorus.process(L, R, n, chorusMix.getCurrentValue());

    // Delay: ping-pong, dotted eighth of the host tempo (120 BPM when the host gives none).
    double bpm = 120.0;
    if (auto* head = getPlayHead())
        if (auto position = head->getPosition())
            if (auto b = position->getBpm()) bpm = jlimit(40.0, 300.0, *b);
    const int delaySamples = jlimit(1, delayLine.getNumSamples() - 1, (int) std::lround(60.0 / bpm * 0.75 * rate));
    delayMix.setTargetValue(param("delay") / 100.0f);
    {
        auto* dl = delayLine.getWritePointer(0);
        auto* dr = delayLine.getWritePointer(1);
        const int size = delayLine.getNumSamples();
        constexpr float feedback = 0.42f;
        for (int i = 0; i < n; ++i) {
            const float mix = delayMix.getNextValue();
            int read = delayWrite - delaySamples;
            if (read < 0) read += size;
            const float tapL = dl[read], tapR = dr[read];
            dl[delayWrite] = 0.5f * (L[i] + R[i]) + feedback * tapR * 0.92f;
            dr[delayWrite] = feedback * tapL * 0.92f;
            L[i] += mix * tapL;
            R[i] += mix * tapR;
            if (++delayWrite >= size) delayWrite = 0;
        }
    }

    // Reverb: wide stereo hall without low-mid build-up.
    reverbMix.setTargetValue(param("reverb") / 100.0f);
    reverbMix.skip(n);
    reverb.process(L, R, n, reverbMix.getCurrentValue());

    // Output level, then a transparent safety limiter at -0.3 dBFS.
    outGain.setTargetValue(Decibels::decibelsToGain(param("output")));
    const float release = (float) std::exp(-1.0 / (0.08 * rate));
    constexpr float ceiling = 0.966f;
    float peak = 0;
    for (int i = 0; i < n; ++i) {
        const float g = outGain.getNextValue();
        float l = L[i] * g, r = R[i] * g;
        const float level = jmax(std::abs(l), std::abs(r));
        limiterEnv = level > limiterEnv ? level : limiterEnv * release + level * (1.0f - release);
        if (limiterEnv > ceiling) { const float k = ceiling / limiterEnv; l *= k; r *= k; }
        L[i] = l; R[i] = r;
        peak = jmax(peak, std::abs(l), std::abs(r));
    }
    outputPeak = jmax(peak, outputPeak.load() * 0.85f);
}

// ---------------------------------------------------------------- state

void Processor::getStateInformation(MemoryBlock& dest)
{
    auto state = params.copyState();
    state.setProperty("library", libraryRoot().getFullPathName(), nullptr);
    state.setProperty("uiScale", uiScale.load(), nullptr);
    if (auto* i = currentInfo()) {
        state.setProperty("category", cat.categories[(size_t) selCategory.load()].name, nullptr);
        state.setProperty("preset", i->name, nullptr);
    }
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void Processor::setStateInformation(const void* data, int size)
{
    auto xml = getXmlFromBinary(data, size);
    if (xml == nullptr || ! xml->hasTagName(params.state.getType())) return;
    auto state = ValueTree::fromXml(*xml);
    params.replaceState(state);

    auto lib = state.getProperty("library").toString();
    uiScale = jlimit(0.5f, 2.0f, (float) state.getProperty("uiScale", 1.0f));
    {
        const ScopedLock l(rootLock);
        if (lib.isNotEmpty() && File::isAbsolutePath(lib) && File(lib).isDirectory()) root = File(lib); // else keep the default bank
        cat = Catalog::scan(root);
    }
    int c = categoryNames.indexOf(state.getProperty("category").toString());
    int p = 0;
    if (c >= 0) {
        auto& list = cat.categories[(size_t) c].presets;
        for (int i = 0; i < (int) list.size(); ++i)
            if (list[(size_t) i].name == state.getProperty("preset").toString()) p = i;
    } else {
        c = selCategory.load();
    }
    selectPreset(c, p);
}

AudioProcessorEditor* Processor::createEditor() { return swell::LicenceGuard::attach(new Editor(*this)); }

} // namespace essential

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new essential::Processor(); }
