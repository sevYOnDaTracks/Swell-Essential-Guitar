// Offline check: library scan, note mapping, audio rendering, seamless preset switching,
// loudness matching, favourites, state restore, and PNG captures of the interface.
// Usage: EGCheck [library folder] [capture folder]
//        EGCheck --render <library> <category> <preset index> <out.wav>   (one note per key, for pitch analysis)
#include "Processor.h"
#include "Editor.h"
#include <iostream>

using namespace juce;
using namespace essential;

static int failures = 0;
static void expect(bool ok, const String& what)
{
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << std::endl;
    if (! ok) ++failures;
}

static void silenceEffects(Processor& p)
{
    for (auto* id : { "delay", "reverb", "chorus" })
        p.params.getParameter(id)->setValueNotifyingHost(0.0f);
    p.params.getParameter("tone")->setValueNotifyingHost(1.0f);
}

// Renders `seconds` with a note-on at the start (and note-off half way). Returns RMS.
static float renderNote(Processor& p, int note, float seconds, bool release = true)
{
    const int block = 512;
    AudioBuffer<float> buf(2, block);
    double sum = 0; int64 count = 0;
    const int blocks = jmax(1, (int) (seconds * 44100 / block));
    for (int b = 0; b < blocks; ++b) {
        MidiBuffer midi;
        if (b == 0) midi.addEvent(MidiMessage::noteOn(1, note, (uint8) 110), 0);
        if (release && b == blocks / 2) midi.addEvent(MidiMessage::noteOff(1, note), 0);
        p.processBlock(buf, midi);
        for (int c = 0; c < 2; ++c) {
            auto* d = buf.getReadPointer(c);
            for (int i = 0; i < block; ++i) { sum += d[i] * d[i]; ++count; }
        }
    }
    return (float) std::sqrt(sum / (double) jmax<int64>(1, count));
}

static void flush(Processor& p, int blocks = 700)
{
    AudioBuffer<float> buf(2, 512); MidiBuffer none;
    MidiBuffer off; off.addEvent(MidiMessage::allNotesOff(1), 0);
    p.processBlock(buf, off);
    for (int i = 0; i < blocks; ++i) p.processBlock(buf, none);
}

static Component* findByTitle(Component& root, const String& title)
{
    if (root.getTitle() == title) return &root;
    for (auto* c : root.getChildren())
        if (auto* f = findByTitle(*c, title)) return f;
    return nullptr;
}

static void capture(AudioProcessorEditor& ed, const File& file)
{
    auto img = ed.createComponentSnapshot(ed.getLocalBounds(), true, 1.0f);
    file.deleteFile();
    FileOutputStream out(file);
    PNGImageFormat().writeImageToStream(img, out);
}

static int renderKeys(const File& library, int category, int preset, const File& out)
{
    Processor p;
    p.setSettingsFile(File::getSpecialLocation(File::tempDirectory).getChildFile("eg-fav-test.txt"));
    p.setLibraryRoot(library);
    p.selectPreset(category, preset);
    if (! p.waitUntilLoaded(30000)) return 1;
    p.prepareToPlay(44100, 512);
    silenceEffects(p);
    flush(p, 10);
    const int block = 512, perNote = (int) (0.6 * 44100 / block);
    AudioBuffer<float> all(2, 0);
    std::vector<float> L, R;
    for (int note = 24; note <= 108; ++note) {
        
        for (int b = 0; b < perNote; ++b) {
            AudioBuffer<float> buf(2, block);
            MidiBuffer midi;
            if (b == 0) midi.addEvent(MidiMessage::noteOn(1, note, (uint8) 100), 0);
            if (b == perNote - 1) midi.addEvent(MidiMessage::allNotesOff(1), block - 1);
            p.processBlock(buf, midi);
            for (int i = 0; i < block; ++i) { L.push_back(buf.getSample(0, i)); R.push_back(buf.getSample(1, i)); }
        }
    }
    AudioBuffer<float> data(2, (int) L.size());
    for (size_t i = 0; i < L.size(); ++i) { data.setSample(0, (int) i, L[i]); data.setSample(1, (int) i, R[i]); }
    out.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<AudioFormatWriter> w(wav.createWriterFor(new FileOutputStream(out), 44100, 2, 24, {}, 0));
    w->writeFromAudioSampleBuffer(data, 0, data.getNumSamples());
    std::cout << "rendu : notes 24..108, " << perNote * block << " echantillons par note" << std::endl;
    return 0;
}

int main(int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI gui;
    swell::Licence::unlockForTesting(); // offline tool: no licence file needed
    if (argc >= 9 && String(argv[1]) == "--midi") {
        // --midi <library> <category> <preset folder> <file.mid> <out.wav> <loops> <lead-in bars> [reverb% delay% chorus%]
        Processor p;
        p.setSettingsFile(File::getSpecialLocation(File::tempDirectory).getChildFile("eg-fav-test.txt"));
        p.setLibraryRoot(File(argv[2]));
        const int c = categoryNames.indexOf(argv[3]);
        int idx = -1;
        for (int i = 0; c >= 0 && i < (int) p.catalog().categories[(size_t) c].presets.size(); ++i)
            if (p.catalog().categories[(size_t) c].presets[(size_t) i].name == String(argv[4])) idx = i;
        if (idx < 0) { std::cout << "preset introuvable" << std::endl; return 1; }
        p.selectPreset(c, idx);
        if (! p.waitUntilLoaded(30000)) return 1;
        const double sr = 44100;
        p.prepareToPlay(sr, 512);
        auto setPct = [&](const char* id, const char* v) { auto* prm = p.params.getParameter(id); prm->setValueNotifyingHost(prm->convertTo0to1(String(v).getFloatValue())); };
        if (argc > 9) setPct("reverb", argv[9]);
        if (argc > 10) setPct("delay", argv[10]);
        if (argc > 11) setPct("chorus", argv[11]);
        MidiFile mf; FileInputStream in{ File(argv[5]) };
        if (! mf.readFrom(in)) return 2;
        mf.convertTimestampTicksToSeconds();
        MidiMessageSequence seq;
        for (int t = 0; t < mf.getNumTracks(); ++t) seq.addSequence(*mf.getTrack(t), 0);
        const double bar = 4 * 60.0 / 126.0, loopLen = 2 * bar;
        const int loops = String(argv[7]).getIntValue();
        const double lead = String(argv[8]).getDoubleValue() * bar;
        AudioBuffer<float> out(2, (int) ((lead + loops * loopLen + 4.0) * sr));
        out.clear();
        AudioBuffer<float> block(2, 512);
        for (int pos = 0; pos < out.getNumSamples(); pos += 512) {
            const int n = jmin(512, out.getNumSamples() - pos);
            MidiBuffer m;
            const double t0 = pos / sr, t1 = (pos + n) / sr;
            for (int l = 0; l < loops; ++l)
                for (auto* e : seq) {
                    const double t = lead + l * loopLen + e->message.getTimeStamp();
                    if (t >= t0 && t < t1 && e->message.isNoteOnOrOff()) m.addEvent(e->message, (int) ((t - t0) * sr));
                }
            block.setSize(2, n, false, false, true);
            p.processBlock(block, m);
            for (int ch = 0; ch < 2; ++ch) out.copyFrom(ch, pos, block, ch, 0, n);
        }
        File f(argv[6]); f.deleteFile();
        WavAudioFormat wav;
        std::unique_ptr<AudioFormatWriter> w(wav.createWriterFor(new FileOutputStream(f), sr, 2, 24, {}, 0));
        w->writeFromAudioSampleBuffer(out, 0, out.getNumSamples());
        std::cout << "ok " << f.getFileName() << std::endl;
        return 0;
    }
    if (argc >= 3 && String(argv[1]) == "--probe") {
        AudioFormatManager fm; fm.registerBasicFormats();
        File f(argv[2]);
        std::unique_ptr<AudioFormatReader> r(fm.createReaderFor(f));
        std::cout << f.getFileName() << " reader=" << (r ? r->getFormatName() : String("null")) << " len=" << (r ? (int) r->lengthInSamples : -1)
                  << " note=" << midiNoteFromFileName(f.getFileName()) << " formats=" << fm.getWildcardForAllFormats() << std::endl;
        return 0;
    }
    if (argc >= 3 && String(argv[1]) == "--load") {
        for (int k = 0; k < 2; ++k) {
            PresetInfo info { "x", "x", File(argv[2]) };
            auto t = Time::getMillisecondCounterHiRes();
            auto lp = loadPreset(info, [] { return false; });
            std::cout << "chargement " << (k == 0 ? "a froid" : "a chaud") << " : " << roundToInt(Time::getMillisecondCounterHiRes() - t)
                      << " ms, " << (lp ? (int) lp->samples.size() : 0) << " samples" << std::endl;
        }
        return 0;
    }
    if (argc >= 6 && String(argv[1]) == "--render")
        return renderKeys(File(argv[2]), String(argv[3]).getIntValue(), String(argv[4]).getIntValue(), File(argv[5]));

    File library = argc > 1 ? File(argv[1]) : File(defaultLibraryPath);
    File shots = argc > 2 ? File(argv[2]) : File::getCurrentWorkingDirectory();
    shots.createDirectory();
    auto favFile = File::getSpecialLocation(File::tempDirectory).getChildFile("eg-fav-test.txt");
    favFile.deleteFile();

    std::cout << "Noms" << std::endl;
    expect(midiNoteFromFileName("Steel Clean_C5_127.wav") == 60, "C5 = 60 (FL Studio)");
    expect(midiNoteFromFileName("Steel Clean_A#3_127.wav") == 46, "A#3 = 46");
    expect(midiNoteFromFileName("Steel Clean.dwp") == -1, "fichier .dwp ignore");
    expect(displayName("DARK MOJAVELI") == "Dark Mojaveli", "typographie unifiee");

    Processor p;
    p.setSettingsFile(favFile);
    p.setLibraryRoot(library);
    std::cout << "Bibliotheque : " << library.getFullPathName() << std::endl;
    for (auto& c : p.catalog().categories) {
        std::cout << "  " << c.name << " : " << c.presets.size() << "  |";
        for (auto& pr : c.presets) std::cout << " " << pr.display << " |";
        std::cout << std::endl;
    }
    expect(p.catalog().total() > 0, String(p.catalog().total()) + " presets trouves");

    p.prepareToPlay(44100, 512);
    silenceEffects(p);

    std::cout << "Son" << std::endl;
    p.selectPreset(2, 0);
    auto t0 = Time::getMillisecondCounterHiRes();
    expect(p.waitUntilLoaded(30000), "chargement en " + String(roundToInt(Time::getMillisecondCounterHiRes() - t0)) + " ms");
    expect(renderNote(p, 60, 1.0f) > 0.005f, "C5 joue du son");
    flush(p);
    expect(renderNote(p, 10, 0.6f) > 0.001f, "note tres grave (10) transposee");
    flush(p);
    expect(renderNote(p, 115, 0.6f) > 0.001f, "note tres aigue (115) transposee");
    flush(p);
    expect(renderNote(p, 21, 0.6f) > 0.001f, "A1 joue une note normale");
    flush(p);
    {
        // Slide on: a note played while another is held glides into place (its pitch starts at the held note).
        p.params.getParameter("slide")->setValueNotifyingHost(1.0f);
        AudioBuffer<float> buf(2, 512); MidiBuffer m, none;
        m.addEvent(MidiMessage::noteOn(1, 60, (uint8) 100), 0);
        p.processBlock(buf, m);
        for (int i = 0; i < 20; ++i) p.processBlock(buf, none);
        MidiBuffer s; s.addEvent(MidiMessage::noteOn(1, 67, (uint8) 100), 0);
        p.processBlock(buf, s);
        float rms = 0;
        for (int i = 0; i < 20; ++i) { p.processBlock(buf, none); rms = jmax(rms, buf.getRMSLevel(0, 0, 512)); }
        expect(rms > 0.005f, "note liee jouee en slide");
        p.params.getParameter("slide")->setValueNotifyingHost(0.0f);
        flush(p);
    }

    std::cout << "Changement de preset sans coupure" << std::endl;
    {
        AudioBuffer<float> buf(2, 512); MidiBuffer on, none;
        on.addEvent(MidiMessage::noteOn(1, 60, (uint8) 110), 0);
        p.processBlock(buf, on);
        for (int i = 0; i < 10; ++i) p.processBlock(buf, none);
        p.selectPreset(4, 1); // a preset not in the cache
        int silentBlocks = 0, blocks = 0;
        while (! p.waitUntilLoaded(0) && blocks < 4000) {
            p.processBlock(buf, none);
            if (buf.getMagnitude(0, 512) < 1.0e-4f && blocks < 150) ++silentBlocks;
            ++blocks;
            Thread::sleep(1);
        }
        expect(silentBlocks == 0, "le son continue pendant le chargement (" + String(blocks) + " blocs)");
        flush(p);
    }
    {
        p.selectPreset(4, 2); p.waitUntilLoaded(30000); // neighbours get preloaded after this
        Thread::sleep(2500);
        auto t = Time::getMillisecondCounterHiRes();
        p.stepPreset(1);
        p.waitUntilLoaded(30000);
        expect(Time::getMillisecondCounterHiRes() - t < 60, "preset suivant pre-charge (" + String(roundToInt(Time::getMillisecondCounterHiRes() - t)) + " ms)");
    }

    std::cout << "Volumes" << std::endl;
    {
        std::vector<float> levels;
        String report;
        for (int c = 0; c < (int) p.catalog().categories.size(); ++c) {
            for (int i = 0; i < jmin(2, (int) p.catalog().categories[(size_t) c].presets.size()); ++i) {
                p.selectPreset(c, i); p.waitUntilLoaded(30000);
                flush(p, 5);
                // Same ear-weighted measure as the plugin, averaged over three octaves.
                float sum = 0;
                for (int note : { 48, 60, 72 }) {
                    AudioBuffer<float> buf(2, 512); double e = 0; int64 cnt = 0;
                    essential::dsp::Biquad hp[2], sh[2];
                    for (int c = 0; c < 2; ++c) { hp[c].highpass(44100, 110); sh[c].highShelf(44100, 1500, 4); }
                    for (int b = 0; b < 34; ++b) {
                        MidiBuffer m; if (b == 0) m.addEvent(MidiMessage::noteOn(1, note, (uint8) 110), 0);
                        p.processBlock(buf, m);
                        for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) { float y = sh[c].process(hp[c].process(buf.getSample(c, i))); e += y * y; ++cnt; }
                    }
                    sum += Decibels::gainToDecibels((float) std::sqrt(e / (double) cnt));
                    flush(p);
                }
                float db = sum / 3.0f;
                levels.push_back(db);
                report << p.catalog().categories[(size_t) c].presets[(size_t) i].display << " " << String(db, 1) << " dB | ";
                flush(p);
            }
        }
        std::sort(levels.begin(), levels.end());
        float spread = levels.back() - levels.front();
        std::cout << "  " << report << std::endl;
        expect(spread < 6.0f, "ecart de volume entre presets : " + String(spread, 1) + " dB");
    }

    std::cout << "Navigation" << std::endl;
    int lastElectric = (int) p.catalog().categories[2].presets.size() - 1;
    p.selectPreset(2, lastElectric);
    p.stepPreset(1);
    expect(p.currentCategory() == 3 && p.currentPreset() == 0, "fin d'Electric -> premier Muted");
    p.stepPreset(-1);
    expect(p.currentCategory() == 2 && p.currentPreset() == lastElectric, "retour au dernier Electric");

    std::cout << "Favoris" << std::endl;
    p.toggleFavourite(2, 3);
    p.toggleFavourite(4, 0);
    expect(p.isFavourite(2, 3) && p.favourites().size() == 2, "2 favoris ajoutes");
    {
        Processor other;
        other.setSettingsFile(favFile);
        other.setLibraryRoot(library);
        expect(other.isFavourite(4, 0), "favoris partages entre instances");
    }
    p.toggleFavourite(4, 0);
    expect(! p.isFavourite(4, 0), "favori retire");

    std::cout << "Etat" << std::endl;
    p.selectPreset(4, 3);
    p.params.getParameter("reverb")->setValueNotifyingHost(0.9f);
    p.params.getParameter("eq")->setValueNotifyingHost(1.0f);
    MemoryBlock state;
    p.getStateInformation(state);
    Processor q;
    q.setSettingsFile(favFile);
    q.setStateInformation(state.getData(), (int) state.getSize());
    expect(q.currentCategory() == 4 && q.currentPreset() == 3, "preset restaure");
    expect(std::abs(q.params.getParameter("reverb")->getValue() - 0.9f) < 0.01f, "reverb restauree");
    expect(q.params.getParameter("eq")->getValue() > 0.5f, "EQ restauree");

    std::cout << "Effets" << std::endl;
    {
        p.params.getParameter("reverb")->setValueNotifyingHost(1.0f);
        p.params.getParameter("chorus")->setValueNotifyingHost(1.0f);
        p.params.getParameter("delay")->setValueNotifyingHost(1.0f);
        p.params.getParameter("output")->setValueNotifyingHost(1.0f); // +6 dB
        p.selectPreset(2, 0); p.waitUntilLoaded(30000);
        AudioBuffer<float> buf(2, 512); float peak = 0; bool finite = true;
        for (int b = 0; b < 400; ++b) {
            MidiBuffer m;
            if (b % 20 == 0) for (int n : { 48, 55, 60, 64, 67 }) m.addEvent(MidiMessage::noteOn(1, n, (uint8) 127), 0);
            p.processBlock(buf, m);
            peak = jmax(peak, buf.getMagnitude(0, 512));
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) finite &= std::isfinite(buf.getSample(c, i));
        }
        expect(finite, "effets stables (pas de NaN)");
        expect(peak <= 0.99f, "limiteur : crete " + String(peak, 3) + " sous 0 dBFS");
        p.params.getParameter("output")->setValueNotifyingHost(p.params.getParameter("output")->getDefaultValue());
        p.params.getParameter("chorus")->setValueNotifyingHost(0.0f);
        p.params.getParameter("delay")->setValueNotifyingHost(0.18f);
        p.params.getParameter("reverb")->setValueNotifyingHost(0.25f);
        flush(p);
    }

    std::cout << "Interface" << std::endl;
    p.toggleFavourite(2, 17);
    std::unique_ptr<AudioProcessorEditor> ed(p.createEditor());
    for (int c = 0; c < 6; ++c) {
        int idx = c == 2 ? 17 : 0;
        if (idx >= (int) p.catalog().categories[(size_t) c].presets.size()) idx = 0;
        p.selectPreset(c, idx);
        p.waitUntilLoaded(30000);
        capture(*ed, shots.getChildFile("ui-" + categoryNames[c].toLowerCase() + ".png"));
    }
    if (auto* b = dynamic_cast<Button*>(findByTitle(*ed, "Liste des presets"))) {
        p.selectPreset(2, 17);
        b->onClick();
        capture(*ed, shots.getChildFile("ui-menu.png"));
        p.toggleFavourite(5, 1);
        auto* view = ed->getChildComponent(0);
        for (auto* c : view->getChildren())
            if (auto* br = dynamic_cast<Browser*>(c)) {
                // Hover the "Favoris" row to show the favourites list.
                auto pos = Point<float>((view->getWidth() - 780) * 0.5f + 60, 62 + 44 + 20);
                br->mouseMove(MouseEvent(Desktop::getInstance().getMainMouseSource(), pos, ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, br, br, Time::getCurrentTime(), pos, Time::getCurrentTime(), 1, false));
            }
        capture(*ed, shots.getChildFile("ui-favoris.png"));
        for (auto* c : view->getChildren()) if (auto* br = dynamic_cast<Browser*>(c)) br->setVisible(false);
        if (auto* g = dynamic_cast<Button*>(findByTitle(*ed, String::fromUTF8("Réglages")))) {
            g->onClick();
            capture(*ed, shots.getChildFile("ui-reglages.png"));
            for (auto* c : view->getChildren()) if (auto* st = dynamic_cast<Settings*>(c)) st->setVisible(false);
        }
        b->onClick();
    }
    {
        // No bank yet: the first-run tutorial must show.
        Processor empty;
        auto none = File::getSpecialLocation(File::tempDirectory).getChildFile("eg-empty-bank");
        none.createDirectory();
        empty.setLibraryRoot(none);
        expect(empty.catalog().empty(), "sans banque : catalogue vide");
        std::unique_ptr<AudioProcessorEditor> e2(empty.createEditor());
        capture(*e2, shots.getChildFile("ui-bienvenue.png"));
        // Picking the folder the zip was unpacked into still finds the bank one level down.
        auto parent = File::getSpecialLocation(File::tempDirectory).getChildFile("eg-unzip");
        parent.deleteRecursively();
        auto bank = parent.getChildFile(bankFolderName);
        bank.getChildFile("Electric").createDirectory();
        expect(resolveBank(parent) == bank, "dossier parent reconnu");
        parent.deleteRecursively();
    }
    expect(true, "captures dans " + shots.getFullPathName());
    ed.reset();
    favFile.deleteFile();

    std::cout << (failures == 0 ? "TOUT PASSE" : String(failures) + " ECHEC(S)") << std::endl;
    return failures == 0 ? 0 : 1;
}
