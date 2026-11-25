#include "Editor.h"
#include "BinaryData.h"

namespace essential {
using namespace juce;

// ---------------------------------------------------------------- fonts

Font Fonts::ui(float height, bool bold) const
{
    auto tf = bold ? semibold : regular;
    return tf != nullptr ? Font(FontOptions(tf).withHeight(height)) : Font(FontOptions(height));
}

Font Fonts::script(float height) const
{
   #if JUCE_MAC
    return Font(FontOptions("Snell Roundhand", height, Font::bold));
   #else
    return Font(FontOptions("Segoe Script", height * 0.8f, Font::plain));
   #endif
}

static float textWidth(const Font& f, const String& s) { return GlyphArrangement::getStringWidth(f, s); }

void drawStar(Graphics& g, Rectangle<float> area, bool filled, Colour colour)
{
    auto c = area.getCentre();
    const float outer = jmin(area.getWidth(), area.getHeight()) * 0.5f, inner = outer * 0.45f;
    Path p;
    for (int i = 0; i < 10; ++i) {
        const float r = (i & 1) ? inner : outer;
        const float a = MathConstants<float>::pi * (float) i / 5.0f - MathConstants<float>::halfPi;
        if (i == 0) p.startNewSubPath(c.x + r * std::cos(a), c.y + r * std::sin(a));
        else p.lineTo(c.x + r * std::cos(a), c.y + r * std::sin(a));
    }
    p.closeSubPath();
    g.setColour(colour);
    if (filled) g.fillPath(p);
    else g.strokePath(p, PathStrokeType(1.3f, PathStrokeType::curved, PathStrokeType::rounded));
}

// ---------------------------------------------------------------- look and feel

void LookAndFeel::drawRotarySlider(Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider&)
{
    auto area = Rectangle<float>((float) x, (float) y, (float) w, (float) h);
    const float size = jmin(area.getWidth(), area.getHeight());
    auto c = area.getCentre();
    const float ring = size * 0.5f - 3.0f;
    const float angle = start + pos * (end - start);

    Path track, value;
    track.addCentredArc(c.x, c.y, ring, ring, 0.0f, start, end, true);
    value.addCentredArc(c.x, c.y, ring, ring, 0.0f, start, angle, true);
    PathStrokeType stroke(3.0f, PathStrokeType::curved, PathStrokeType::rounded);
    g.setColour(Colours::white.withAlpha(0.08f));
    g.strokePath(track, stroke);
    if (pos > 0.001f) { g.setColour(colours::accent); g.strokePath(value, stroke); }

    // Soft white cap with a dark notch.
    const float r = size * 0.38f;
    auto cap = Rectangle<float>(r * 2, r * 2).withCentre(c);
    for (int i = 6; i > 0; --i) { // drop shadow
        g.setColour(Colours::black.withAlpha(0.06f));
        g.fillEllipse(cap.translated(0, 5.0f).expanded((float) i * 1.6f));
    }
    ColourGradient grad(Colours::white, cap.getX() + r * 0.64f, cap.getY() + r * 0.52f,
                        Colour(0xffbfc4cc), cap.getRight(), cap.getBottom(), true);
    grad.addColour(0.42, Colour(0xffeef0f3));
    g.setGradientFill(grad);
    g.fillEllipse(cap);

    Path notch;
    notch.addRoundedRectangle(-1.5f, -r + 5.0f, 3.0f, r * 0.5f, 1.5f);
    g.setColour(Colour(0xff1c1d22));
    g.fillPath(notch, AffineTransform::rotation(angle).translated(c.x, c.y));
}

// ---------------------------------------------------------------- keyboard

Keyboard::Keyboard(MidiKeyboardState& s, const Fonts& f)
    : MidiKeyboardComponent(s, horizontalKeyboard), fonts(f)
{
    setAvailableRange(36, 95);   // C3 – B7 (FL Studio octaves); every other note plays too, transposed
    setOctaveForMiddleC(5);
    setScrollButtonsVisible(false);
    setBlackNoteLengthProportion(0.6f);
    setColour(shadowColourId, Colours::transparentBlack);
    setColour(keySeparatorLineColourId, Colours::transparentBlack);
    setMouseCursor(MouseCursor::PointingHandCursor);
}

void Keyboard::drawWhiteNote(int note, Graphics& g, Rectangle<float> area, bool down, bool over, Colour, Colour)
{
    auto key = area.reduced(1.0f, 0.0f);
    Path p;
    p.addRoundedRectangle(key.getX(), key.getY(), key.getWidth(), key.getHeight(), 6.0f, 6.0f, false, false, true, true);
    if (down) g.setColour(colours::accent);
    else g.setGradientFill(ColourGradient(Colour(0xffdcdde0), 0, key.getY(), Colour(0xfff1f0ec), 0, key.getY() + key.getHeight() * 0.3f, false));
    g.fillPath(p);
    if (over && ! down) { g.setColour(colours::accent.withAlpha(0.18f)); g.fillPath(p); }

    if (note % 12 == 0) {
        g.setColour(Colour(0xff8a8780));
        g.setFont(fonts.ui(10.0f));
        g.drawText(getWhiteNoteText(note), key.removeFromBottom(18.0f).toNearestInt(), Justification::centred);
    }
}

void Keyboard::drawBlackNote(int, Graphics& g, Rectangle<float> area, bool down, bool over, Colour)
{
    Path p;
    p.addRoundedRectangle(area.getX(), area.getY(), area.getWidth(), area.getHeight(), 4.0f, 4.0f, false, false, true, true);
    if (down) g.setColour(colours::accent);
    else g.setGradientFill(ColourGradient(Colour(0xff0e0e10), 0, area.getY(), Colour(0xff3a3b40), 0, area.getBottom(), false));
    g.fillPath(p);
    if (over && ! down) { g.setColour(colours::accent.withAlpha(0.3f)); g.fillPath(p); }
}

// ---------------------------------------------------------------- browser

Browser::Browser(Processor& p, const Fonts& f) : proc(p), fonts(f) {}

void Browser::open()
{
    shown = jmax(0, proc.currentCategory());
    hoverCell = -1;
    setVisible(true);
    toFront(false);
    repaint();
}

std::vector<std::pair<int, int>> Browser::shownPresets() const
{
    if (shown == favouritesRow) return proc.favourites();
    std::vector<std::pair<int, int>> out;
    auto& cats = proc.catalog().categories;
    if (shown >= 0 && shown < (int) cats.size())
        for (int i = 0; i < (int) cats[(size_t) shown].presets.size(); ++i) out.push_back({ shown, i });
    return out;
}

Rectangle<float> Browser::panel() const { return { (getWidth() - 780) * 0.5f, 62.0f, 780.0f, 352.0f }; }
Rectangle<float> Browser::row(int i) const { auto p = panel(); return { p.getX() + 10, p.getY() + 44 + (i + 1) * 42.0f, 170, 40 }; }
Rectangle<float> Browser::cell(int i) const
{
    auto p = panel();
    const float colW = (p.getWidth() - 190 - 28 - 12) / 3.0f;
    return { p.getX() + 204 + (i % 3) * (colW + 6), p.getY() + 14 + (i / 3) * 38.0f, colW, 34 };
}

void Browser::paint(Graphics& g)
{
    auto p = panel();
    Path shape;
    shape.addRoundedRectangle(p, 16.0f);
    DropShadow(Colours::black.withAlpha(0.6f), 48, { 0, 20 }).drawForPath(g, shape);
    g.setColour(Colour(0xf2120c09));
    g.fillPath(shape);
    g.setColour(Colour(0x24fff0dc));
    g.strokePath(shape, PathStrokeType(1.0f));
    g.drawVerticalLine((int) (p.getX() + 190), p.getY(), p.getBottom());

    g.setColour(colours::cream.withAlpha(0.55f));
    g.setFont(fonts.ui(11.0f).withExtraKerningFactor(0.24f));
    g.drawText(String::fromUTF8("BIBLIOTHÈQUE"), Rectangle<float>(p.getX() + 22, p.getY() + 14, 160, 22), Justification::centredLeft);

    auto& cats = proc.catalog().categories;
    auto drawRow = [&](int i, const String& name, int count) {
        auto r = row(i);
        const bool sel = i == shown;
        if (sel) { g.setColour(Colour(0x1afff0dc)); g.fillRoundedRectangle(r, 10.0f); }
        auto text = r.reduced(12, 0);
        if (i == favouritesRow) {
            drawStar(g, text.removeFromLeft(14).withSizeKeepingCentre(13, 13), true, colours::accent);
            text.removeFromLeft(8);
        }
        g.setColour(colours::cream.withAlpha(sel ? 1.0f : 0.7f));
        g.setFont(fonts.ui(14.0f));
        g.drawText(name, text, Justification::centredLeft);
        g.setColour(colours::cream.withAlpha(0.45f));
        g.setFont(fonts.ui(12.0f, false));
        g.drawText(String(count), text, Justification::centredRight);
    };
    drawRow(favouritesRow, "Favoris", (int) proc.favourites().size());
    for (int i = 0; i < (int) cats.size(); ++i) drawRow(i, cats[(size_t) i].name, (int) cats[(size_t) i].presets.size());

    auto list = shownPresets();
    for (int i = 0; i < (int) list.size(); ++i) {
        auto [c, pr] = list[(size_t) i];
        auto* info = proc.info(c, pr);
        if (info == nullptr) continue;
        auto r = cell(i);
        const bool current = c == proc.currentCategory() && pr == proc.currentPreset();
        const bool fav = proc.isFavourite(c, pr);
        if (current) { g.setColour(colours::accent); g.fillRoundedRectangle(r, 8.0f); }
        else if (i == hoverCell) { g.setColour(Colour(0x14fff0dc)); g.fillRoundedRectangle(r, 8.0f); }
        auto text = r.reduced(12, 0);
        auto starBox = text.removeFromRight(24.0f); // reserved for the star, so labels never run under it
        text.removeFromRight(4.0f);
        if (fav || i == hoverCell)
            drawStar(g, starBox.withSizeKeepingCentre(12, 12), fav, current ? colours::ink : colours::accent.withAlpha(fav ? 1.0f : 0.6f));
        g.setColour(current ? colours::ink : colours::cream);
        g.setFont(fonts.ui(13.0f, false));
        if (shown == favouritesRow) {
            auto small = fonts.ui(10.0f).withExtraKerningFactor(0.1f);
            auto catText = cats[(size_t) c].name.toUpperCase();
            g.drawText(info->display, text.withTrimmedRight(textWidth(small, catText) + 8), Justification::centredLeft, true);
            g.setFont(small);
            g.setColour((current ? colours::ink : colours::cream).withAlpha(0.5f));
            g.drawText(catText, text, Justification::centredRight);
        } else {
            g.drawText(info->display, text, Justification::centredLeft, true);
        }
    }
    if (list.empty()) {
        g.setColour(colours::cream.withAlpha(0.5f));
        g.setFont(fonts.ui(13.0f, false));
        g.drawText(shown == favouritesRow ? String::fromUTF8("Aucun favori : cliquez sur l'étoile d'un preset")
                                          : String("Aucun preset dans ce dossier"),
                   p.withTrimmedLeft(204).withHeight(60), Justification::centredLeft);
    }
}

void Browser::mouseMove(const MouseEvent& e)
{
    auto pt = e.position;
    for (int i = favouritesRow; i < (int) proc.catalog().categories.size(); ++i)
        if (row(i).contains(pt) && i != shown) { shown = i; repaint(); } // browsing without loading
    int over = -1;
    auto list = shownPresets();
    for (int i = 0; i < (int) list.size(); ++i) if (cell(i).contains(pt)) over = i;
    if (over != hoverCell) { hoverCell = over; repaint(); }
    bool clickable = over >= 0;
    for (int i = favouritesRow; i < (int) proc.catalog().categories.size(); ++i) clickable |= row(i).contains(pt);
    setMouseCursor(clickable ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
}

void Browser::mouseExit(const MouseEvent&) { hoverCell = -1; repaint(); }

void Browser::mouseDown(const MouseEvent& e)
{
    auto pt = e.position;
    if (! panel().contains(pt)) { setVisible(false); if (onClose) onClose(); return; }
    for (int i = favouritesRow; i < (int) proc.catalog().categories.size(); ++i)
        if (row(i).contains(pt)) { shown = i; repaint(); return; }
    auto list = shownPresets();
    for (int i = 0; i < (int) list.size(); ++i) {
        auto r = cell(i);
        if (! r.contains(pt)) continue;
        auto [c, p] = list[(size_t) i];
        if (starArea(r.reduced(12, 0)).expanded(6, 4).contains(pt)) {
            proc.toggleFavourite(c, p);
            if (onFavouritesChanged) onFavouritesChanged();
            repaint();
            return;
        }
        proc.selectPreset(c, p);
        setVisible(false);
        if (onClose) onClose();
        return;
    }
}


// ---------------------------------------------------------------- settings

static void drawPill(Graphics& g, Rectangle<float> r, const String& text, bool over, bool active, const Fonts& fonts)
{
    auto pill = r.reduced(1.0f);
    g.setColour(active ? colours::accent : Colours::white.withAlpha(over ? 0.14f : 0.07f));
    g.fillRoundedRectangle(pill, pill.getHeight() * 0.5f);
    if (! active) { g.setColour(colours::cream.withAlpha(0.22f)); g.drawRoundedRectangle(pill, pill.getHeight() * 0.5f, 1.0f); }
    g.setColour(active ? colours::ink : colours::cream.withAlpha(0.9f));
    g.setFont(fonts.ui(12.0f));
    g.drawText(text, pill, Justification::centred);
}

Settings::Settings(Processor& p, const Fonts& f) : proc(p), fonts(f)
{
    auto label = [this](PaintedButton& b, const String& text) {
        b.setTitle(text);
        b.painter = [this, text](Graphics& g, Rectangle<float> r, bool over, bool) { drawPill(g, r, text, over, false, fonts); };
        addAndMakeVisible(b);
    };
    label(deactivate, String::fromUTF8("Désactiver"));
    label(change, "Changer");
    label(reveal, "Ouvrir");
    label(rescan, "Rescanner");
    close.setTitle("Fermer");
    close.painter = [](Graphics& g, Rectangle<float> r, bool over, bool) {
        auto c = r.withSizeKeepingCentre(12, 12);
        g.setColour(colours::cream.withAlpha(over ? 1.0f : 0.6f));
        g.drawLine(c.getX(), c.getY(), c.getRight(), c.getBottom(), 1.6f);
        g.drawLine(c.getRight(), c.getY(), c.getX(), c.getBottom(), 1.6f);
    };
    addAndMakeVisible(close);
    const float levels[] { 0.75f, 1.0f, 1.25f, 1.5f };
    for (int i = 0; i < 4; ++i) {
        const float z = levels[i];
        auto& b = zooms[(size_t) i];
        b.setTitle(String(roundToInt(z * 100)) + " %");
        b.painter = [this, z](Graphics& g, Rectangle<float> r, bool over, bool) {
            drawPill(g, r, String(roundToInt(z * 100)) + " %", over, std::abs(zoom - z) < 0.02f, fonts);
        };
        b.onClick = [this, z] { zoom = z; repaint(); if (onZoom) onZoom(z); };
        addAndMakeVisible(b);
    }
    close.onClick = [this] { setVisible(false); };
    deactivate.onClick = [this] {
        AlertWindow::showOkCancelBox(MessageBoxIconType::WarningIcon, String::fromUTF8("Désactiver la licence"),
            String::fromUTF8("Swell Essential Guitar sera désactivé sur cet ordinateur et sa place libérée sur ton compte."),
            String::fromUTF8("Désactiver"), "Annuler", this,
            ModalCallbackFunction::create([this](int r) { if (r == 1) licence->deactivate(); }));
    };
    change.onClick = [this] {
        chooser = std::make_unique<FileChooser>("Dossier des samples Swell Essential Guitar", proc.libraryRoot());
        chooser->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories, [this](const FileChooser& fc) {
            if (fc.getResult().isDirectory()) { proc.setLibraryRoot(fc.getResult()); if (onBankChanged) onBankChanged(); repaint(); }
        });
    };
    reveal.onClick = [this] { proc.libraryRoot().revealToUser(); };
    rescan.onClick = [this] { proc.setLibraryRoot(proc.libraryRoot()); if (onBankChanged) onBankChanged(); repaint(); };
    licence->addChangeListener(this);
    refresh();
}

Settings::~Settings() { licence->removeChangeListener(this); }

void Settings::open() { refresh(); setVisible(true); toFront(true); }

void Settings::refresh()
{
    deactivate.setVisible(licence->info().key.isNotEmpty());
    repaint();
}

void Settings::mouseDown(const MouseEvent& e) { if (! card().contains(e.getPosition())) setVisible(false); }

void Settings::paint(Graphics& g)
{
    g.fillAll(Colour(0xc8050302));
    const auto c = card().toFloat();
    Path shape;
    shape.addRoundedRectangle(c, 16.0f);
    DropShadow(Colours::black.withAlpha(0.6f), 48, { 0, 20 }).drawForPath(g, shape);
    g.setColour(Colour(0xfa15100d));
    g.fillPath(shape);
    g.setColour(Colour(0x24fff0dc));
    g.strokePath(shape, PathStrokeType(1.0f));

    auto r = c.reduced(28, 24);
    g.setColour(colours::cream);
    g.setFont(fonts.ui(20.0f).withExtraKerningFactor(0.3f));
    g.drawText(String::fromUTF8("RÉGLAGES"), r.removeFromTop(30), Justification::centredLeft);

    auto section = [&](const String& t) {
        r.removeFromTop(16);
        g.setColour(colours::cream.withAlpha(0.5f));
        g.setFont(fonts.ui(10.5f).withExtraKerningFactor(0.3f));
        g.drawText(t, r.removeFromTop(18), Justification::centredLeft);
        r.removeFromTop(4);
    };
    auto row = [&](const String& a, const String& b, Colour bc = colours::cream.withAlpha(0.65f), float buttons = 0) {
        auto line = r.removeFromTop(36);
        r.removeFromTop(6);
        g.setColour(Colours::white.withAlpha(0.04f));
        g.fillRoundedRectangle(line, 9.0f);
        g.setColour(Colour(0x1afff0dc));
        g.drawRoundedRectangle(line.reduced(0.5f), 9.0f, 1.0f);
        line.reduce(14, 0);
        g.setColour(colours::cream);
        g.setFont(fonts.ui(13.0f));
        g.drawText(a, line, Justification::centredLeft);
        g.setColour(bc);
        g.setFont(fonts.ui(12.0f, false));
        g.drawText(b, line.withTrimmedLeft(140).withTrimmedRight(buttons), Justification::centredLeft, true);
    };

    const auto info = licence->info();
    section("LICENCE");
    if (licence->isUnlocked()) {
        row("Statut", info.isTrial() ? "Essai : " + String(licence->trialDaysLeft()) + " jours restants"
                                     : String::fromUTF8("\xe2\x97\x8f Activée"), Colour(0xff6fdc8c));
        row(String::fromUTF8("Propriétaire"), info.key.isEmpty() ? String::fromUTF8("—")
                                              : (info.name.isNotEmpty() ? info.name + "  ·  " + info.email : info.email), colours::cream.withAlpha(0.65f), 110);
        row(String::fromUTF8("Clé"), info.key.isEmpty() ? String::fromUTF8("—") : info.key.substring(0, 11) + "-XXXX-XXXX");
    } else {
        row("Statut", String::fromUTF8("Non activée"), Colour(0xffff6b6b));
        r.removeFromTop(84);
    }

    section(String::fromUTF8("BANQUE DE SONS"));
    auto path = proc.libraryRoot().getFullPathName().replace(File::getSpecialLocation(File::userHomeDirectory).getFullPathName(), "~");
    row("Dossier", path, proc.catalog().empty() ? Colour(0xffff6b6b) : colours::cream.withAlpha(0.65f), 170);
    row("Presets", String(proc.catalog().total()) + String::fromUTF8(" presets · 6 catégories · ") + String((int) proc.favourites().size()) + " favoris",
        colours::cream.withAlpha(0.65f), 110);

    section(String::fromUTF8("TAILLE DE LA FENÊTRE"));
    r.removeFromTop(40);

    section(String::fromUTF8("À PROPOS"));
    row("Version", "Swell Essential Guitar " JucePlugin_VersionString);
    row("Site", String::fromUTF8("Swell Audio · swell-audio.fr"));
}

void Settings::resized()
{
    const auto c = card();
    close.setBounds(c.getRight() - 52, c.getY() + 20, 32, 32);
    // Rows: title (24+30), each section header 16+18+4, rows 36+6.
    const int x0 = c.getX() + 28, right = c.getRight() - 28;
    int y = c.getY() + 24 + 30 + 38;                       // first licence row
    auto inRow = [&](Button& b, int rowY, int w, int fromRight) { b.setBounds(right - 8 - fromRight - w, rowY + 6, w, 24); };
    inRow(deactivate, y + 42, 100, 0);                     // owner row
    y += 3 * 42 + 38;                                      // first bank row
    inRow(change, y, 76, 0);
    inRow(reveal, y, 76, 82);
    inRow(rescan, y + 42, 100, 0);
    y += 2 * 42 + 38;                                      // zoom row
    for (int i = 0; i < 4; ++i) zooms[(size_t) i].setBounds(x0 + i * 92, y, 84, 30);
}

// ---------------------------------------------------------------- welcome (bank download)

Welcome::Welcome(Processor& p, const Fonts& f) : proc(p), fonts(f)
{
    download.setTitle(String::fromUTF8("Télécharger la banque"));
    download.painter = [this](Graphics& g, Rectangle<float> r, bool over, bool) {
        drawPill(g, r, String::fromUTF8("Télécharger la banque (") + bankSize + ")", over, true, fonts);
    };
    download.onClick = [] { URL(bankDownloadUrl).launchInDefaultBrowser(); };
    choose.setTitle("Choisir le dossier");
    choose.painter = [this](Graphics& g, Rectangle<float> r, bool over, bool) { drawPill(g, r, "Choisir le dossier", over, false, fonts); };
    choose.onClick = [this] {
        chooser = std::make_unique<FileChooser>(String::fromUTF8("Sélectionne le dossier « ") + bankFolderName + String::fromUTF8(" »"),
                                                File::getSpecialLocation(File::userHomeDirectory).getChildFile("Downloads"));
        chooser->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories, [this](const FileChooser& fc) {
            if (fc.getResult().isDirectory()) { proc.setLibraryRoot(fc.getResult()); if (onBankChanged) onBankChanged(); repaint(); }
        });
    };
    retry.setTitle("Rechercher");
    retry.painter = [this](Graphics& g, Rectangle<float> r, bool over, bool) { drawPill(g, r, "Rechercher", over, false, fonts); };
    retry.onClick = [this] { proc.setLibraryRoot(defaultLibrary()); if (onBankChanged) onBankChanged(); repaint(); };
    for (auto* b : { &download, &choose, &retry }) addAndMakeVisible(b);
}

void Welcome::paint(Graphics& g)
{
    g.fillAll(Colour(0xd8070403));
    const auto c = card().toFloat();
    Path shape;
    shape.addRoundedRectangle(c, 18.0f);
    DropShadow(Colours::black.withAlpha(0.6f), 48, { 0, 20 }).drawForPath(g, shape);
    g.setColour(Colour(0xfa15100d));
    g.fillPath(shape);
    g.setColour(Colour(0x24fff0dc));
    g.strokePath(shape, PathStrokeType(1.0f));

    auto r = c.reduced(36, 30);
    g.setColour(colours::cream.withAlpha(0.6f));
    g.setFont(fonts.ui(11.0f).withExtraKerningFactor(0.3f));
    g.drawText(String::fromUTF8("PREMIÈRE UTILISATION"), r.removeFromTop(18), Justification::centredLeft);
    g.setColour(colours::cream);
    g.setFont(fonts.script(44.0f));
    g.drawText("Bienvenue", r.removeFromTop(58), Justification::centredLeft);
    g.setFont(fonts.ui(13.0f, false));
    g.setColour(colours::cream.withAlpha(0.75f));
    g.drawFittedText(String::fromUTF8("Les sons de Swell Essential Guitar se téléchargent à part, une seule fois. Trois étapes :"),
                     r.removeFromTop(24).toNearestInt(), Justification::centredLeft, 1);
    r.removeFromTop(14);

    const String steps[] {
        String::fromUTF8("Télécharge la banque de sons (bouton orange ci-dessous)."),
        String::fromUTF8("Double-clique sur le zip téléchargé. Range le dossier « ") + bankFolderName + String::fromUTF8(" » où tu veux (Musique, par exemple)."),
        String::fromUTF8("Clique sur « Choisir le dossier » et sélectionne-le. C'est fini : le plugin s'en souvient."),
    };
    for (int i = 0; i < 3; ++i) {
        auto line = r.removeFromTop(46);
        auto badge = line.removeFromLeft(30).withSizeKeepingCentre(26, 26);
        g.setColour(colours::accent);
        g.fillEllipse(badge);
        g.setColour(colours::ink);
        g.setFont(fonts.ui(13.0f));
        g.drawText(String(i + 1), badge, Justification::centred);
        line.removeFromLeft(12);
        g.setColour(colours::cream);
        g.setFont(fonts.ui(13.5f, false));
        g.drawFittedText(steps[i], line.toNearestInt(), Justification::centredLeft, 2);
    }

    g.setColour(colours::cream.withAlpha(0.45f));
    g.setFont(fonts.ui(11.0f, false));
    auto foot = c.reduced(36, 24).removeFromBottom(18);
    g.drawText(String::fromUTF8("Le plugin cherche aussi tout seul dans Téléchargements, Bureau et Musique. Dossier modifiable plus tard dans les Réglages."),
               foot, Justification::centredLeft, true);
}

void Welcome::resized()
{
    const auto c = card();
    const int y = c.getBottom() - 30 - 18 - 18 - 40;
    download.setBounds(c.getX() + 36, y, 270, 40);
    choose.setBounds(download.getRight() + 10, y, 170, 40);
    retry.setBounds(choose.getRight() + 10, y, c.getRight() - 36 - choose.getRight() - 10, 40);
}

// ---------------------------------------------------------------- main view

MainView::MainView(Processor& p) : proc(p), keyboard(p.keyboard, fonts), browser(p, fonts), settings(p, fonts), welcome(p, fonts)
{
    fonts.regular = Typeface::createSystemTypefaceFor(BinaryData::geist_regular_ttf, BinaryData::geist_regular_ttfSize);
    fonts.semibold = Typeface::createSystemTypefaceFor(BinaryData::geist_semibold_ttf, BinaryData::geist_semibold_ttfSize);
    setLookAndFeel(&lnf);
    logo = ImageCache::getFromMemory(BinaryData::swell_mark_png, BinaryData::swell_mark_pngSize);
    heroes[0] = ImageCache::getFromMemory(BinaryData::hero_acoustic_jpg, BinaryData::hero_acoustic_jpgSize);
    heroes[1] = ImageCache::getFromMemory(BinaryData::hero_bass_jpg, BinaryData::hero_bass_jpgSize);
    heroes[2] = ImageCache::getFromMemory(BinaryData::hero_electric_jpg, BinaryData::hero_electric_jpgSize);
    heroes[3] = ImageCache::getFromMemory(BinaryData::hero_muted_jpg, BinaryData::hero_muted_jpgSize);
    heroes[4] = ImageCache::getFromMemory(BinaryData::hero_nylon_jpg, BinaryData::hero_nylon_jpgSize);
    heroes[5] = ImageCache::getFromMemory(BinaryData::hero_rock_jpg, BinaryData::hero_rock_jpgSize);

    auto arrow = [](bool left) {
        return [left](Graphics& g, Rectangle<float> r, bool over, bool down) {
            if (over || down) { g.setColour(Colours::white.withAlpha(down ? 0.16f : 0.08f)); g.fillEllipse(r.reduced(2)); }
            auto c = r.getCentre();
            Path a;
            if (left) { a.startNewSubPath(c.x + 2.5f, c.y - 5); a.lineTo(c.x - 2.5f, c.y); a.lineTo(c.x + 2.5f, c.y + 5); }
            else      { a.startNewSubPath(c.x - 2.5f, c.y - 5); a.lineTo(c.x + 2.5f, c.y); a.lineTo(c.x - 2.5f, c.y + 5); }
            g.setColour(colours::cream);
            g.strokePath(a, PathStrokeType(1.8f, PathStrokeType::curved, PathStrokeType::rounded));
        };
    };
    prev.painter = arrow(true);
    next.painter = arrow(false);
    prev.setTitle(String::fromUTF8("Preset précédent"));
    next.setTitle("Preset suivant");
    prev.onClick = [this] { proc.stepPreset(-1); };
    next.onClick = [this] { proc.stepPreset(1); };

    presetButton.setTitle("Liste des presets");
    presetButton.painter = [this](Graphics& g, Rectangle<float> r, bool over, bool) {
        if (over) { g.setColour(Colours::white.withAlpha(0.06f)); g.fillRoundedRectangle(r, r.getHeight() * 0.5f); }
        auto* info = proc.currentInfo();
        int c = proc.currentCategory();
        String catText = c >= 0 && c < (int) proc.catalog().categories.size() ? proc.catalog().categories[(size_t) c].name.toUpperCase() : String();
        String name = info != nullptr ? info->display : String("Aucun preset");
        auto catFont = fonts.ui(11.0f).withExtraKerningFactor(0.14f);
        auto nameFont = fonts.ui(14.0f);
        float wc = textWidth(catFont, catText), wn = jmin(textWidth(nameFont, name), r.getWidth() - wc - 40);
        float x = r.getCentreX() - (wc + 8 + wn + 16) * 0.5f;
        g.setFont(catFont); g.setColour(colours::accent);
        g.drawText(catText, Rectangle<float>(x, r.getY(), wc + 2, r.getHeight()), Justification::centredLeft);
        g.setFont(nameFont); g.setColour(colours::cream);
        g.drawText(name, Rectangle<float>(x + wc + 8, r.getY(), wn + 2, r.getHeight()), Justification::centredLeft, true);
        Path chevron;
        float cx = x + wc + 8 + wn + 10, cy = r.getCentreY();
        chevron.startNewSubPath(cx - 3, cy - 1.5f); chevron.lineTo(cx, cy + 1.5f); chevron.lineTo(cx + 3, cy - 1.5f);
        g.strokePath(chevron, PathStrokeType(1.5f, PathStrokeType::curved, PathStrokeType::rounded));
    };
    presetButton.onClick = [this] { if (browser.isVisible()) browser.setVisible(false); else browser.open(); };

    gear.setTitle(String::fromUTF8("Réglages"));
    gear.setTooltip(String::fromUTF8("Réglages : licence, banque de sons, taille de la fenêtre"));
    gear.painter = [](Graphics& g, Rectangle<float> r, bool over, bool) {
        // Cog: eight teeth around a ring.
        auto c = r.getCentre();
        Path cog;
        const float outer = 9.0f, inner = 6.6f;
        for (int i = 0; i < 16; ++i) {
            const float a0 = MathConstants<float>::twoPi * ((float) i - 0.5f) / 16.0f;
            const float a1 = MathConstants<float>::twoPi * ((float) i + 0.5f) / 16.0f;
            const float rad = (i & 1) ? inner : outer;
            auto p0 = c.getPointOnCircumference(rad, a0), p1 = c.getPointOnCircumference(rad, a1);
            if (i == 0) cog.startNewSubPath(p0); else cog.lineTo(p0);
            cog.lineTo(p1);
        }
        cog.closeSubPath();
        g.setColour(colours::cream.withAlpha(over ? 1.0f : 0.65f));
        g.strokePath(cog, PathStrokeType(1.4f, PathStrokeType::curved, PathStrokeType::rounded));
        g.drawEllipse(Rectangle<float>(6, 6).withCentre(c), 1.4f);
    };
    gear.onClick = [this] { settings.open(); };

    star.setTitle("Ajouter aux favoris");
    star.painter = [this](Graphics& g, Rectangle<float> r, bool over, bool) {
        const bool fav = proc.isFavourite(proc.currentCategory(), proc.currentPreset());
        drawStar(g, r.withSizeKeepingCentre(16, 16), fav, fav ? colours::accent : colours::cream.withAlpha(over ? 0.95f : 0.55f));
    };
    star.onClick = [this] { proc.toggleFavourite(proc.currentCategory(), proc.currentPreset()); repaint(); };

    // On/off pills (same look for EQ and Slide).
    auto pillPainter = [this](PaintedButton& b, const String& text) {
        return [this, &b, text](Graphics& g, Rectangle<float> r, bool over, bool) {
            const bool on = b.getToggleState();
            auto pill = r.reduced(1.0f);
            g.setColour(on ? colours::accent : Colours::white.withAlpha(over ? 0.12f : 0.06f));
            g.fillRoundedRectangle(pill, pill.getHeight() * 0.5f);
            if (! on) { g.setColour(colours::cream.withAlpha(0.25f)); g.drawRoundedRectangle(pill, pill.getHeight() * 0.5f, 1.0f); }
            g.setColour(on ? colours::ink : colours::cream.withAlpha(0.8f));
            g.setFont(fonts.ui(10.5f).withExtraKerningFactor(0.12f));
            g.drawText(text, pill, Justification::centred);
        };
    };
    eq.setTitle("EQ douceur");
    eq.setClickingTogglesState(true);
    eq.painter = pillPainter(eq, "EQ");
    eqAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(proc.params, "eq", eq);
    eq.setTooltip(String::fromUTF8("Adoucit légèrement les bas médiums"));

    slide.setTitle("Slide");
    slide.setClickingTogglesState(true);
    slide.painter = pillPainter(slide, "SLIDE");
    slideAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(proc.params, "slide", slide);
    slide.setTooltip(String::fromUTF8("Notes liées : une note jouée pendant qu'une autre est tenue glisse depuis celle-ci"));

    for (int i = 0; i < 6; ++i) {
        tabs[(size_t) i].setTitle(categoryNames[i]);
        tabs[(size_t) i].painter = [this, i](Graphics& g, Rectangle<float> r, bool over, bool) {
            const bool sel = proc.currentCategory() == i;
            const bool empty = proc.catalog().categories[(size_t) i].presets.empty();
            g.setColour(colours::cream.withAlpha(sel ? 1.0f : (empty ? 0.25f : (over ? 0.85f : 0.6f))));
            g.setFont(fonts.ui(14.0f).withExtraKerningFactor(0.06f));
            g.drawText(categoryNames[i], r.withTrimmedBottom(10), Justification::centred);
            if (sel) { g.setColour(colours::accent); g.fillRoundedRectangle(r.getCentreX() - 9, r.getBottom() - 9, 18, 2, 1); }
        };
        tabs[(size_t) i].onClick = [this, i] {
            if (! proc.catalog().categories[(size_t) i].presets.empty()) proc.selectPreset(i, 0);
        };
    }

    const char* ids[] { "tone", "attack", "release", "chorus", "delay", "reverb", "output" };
    const char* labels[] { "tone", "attack", "release", "chorus", "delay", "reverb", "volume" };
    for (int i = 0; i < 7; ++i) {
        auto& k = knobs[(size_t) i];
        k.id = ids[i];
        k.label = labels[i];
        k.slider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
        k.slider.setRotaryParameters(MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
        k.slider.setMouseCursor(MouseCursor::UpDownResizeCursor);
        k.slider.setTitle(k.label);
        k.attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(proc.params, k.id, k.slider);
        if (auto* prm = proc.params.getParameter(k.id))
            k.slider.setDoubleClickReturnValue(true, prm->convertFrom0to1(prm->getDefaultValue()));
        k.slider.onValueChange = [this] { repaint(); };
        addAndMakeVisible(k.slider);
    }

    for (auto* c : std::initializer_list<Component*> { &prev, &next, &presetButton, &gear, &star, &eq, &slide, &keyboard })
        addAndMakeVisible(c);
    for (auto& t : tabs) addAndMakeVisible(t);
    addChildComponent(browser);
    addChildComponent(settings);
    addChildComponent(welcome);
    welcome.onBankChanged = [this] { resized(); repaint(); refresh(); };
    settings.onZoom = [this](float z) { if (onZoom) onZoom(z); };
    settings.onBankChanged = [this] { resized(); repaint(); };
    browser.onClose = [this] { repaint(); };
    browser.onFavouritesChanged = [this] { star.repaint(); };
    refresh();
}

MainView::~MainView() { setLookAndFeel(nullptr); }

String MainView::infoLine() const
{
    auto& cats = proc.catalog().categories;
    if (proc.catalog().empty())
        return String::fromUTF8("BIBLIOTHÈQUE INTROUVABLE — CLIQUEZ SUR LE DOSSIER EN HAUT À DROITE");
    const int c = proc.currentCategory();
    if (proc.currentInfo() == nullptr) return {};
    auto& list = cats[(size_t) c].presets;
    String line = cats[(size_t) c].name.toUpperCase() + String::fromUTF8("  ·  ")
                + String(proc.currentPreset() + 1).paddedLeft('0', 2) + " / " + String((int) list.size()).paddedLeft('0', 2);
    if (! proc.loading.load() && (proc.loadedCategory.load() != c || proc.loadedPreset.load() != proc.currentPreset()))
        line << String::fromUTF8("  ·  SAMPLES ILLISIBLES");
    return line;
}

void MainView::placeStar()
{
    auto w = textWidth(fonts.ui(13.0f, false).withExtraKerningFactor(0.18f), infoLine());
    star.setBounds(134 + (int) w + 10, 248, 28, 28);
    star.setVisible(proc.currentInfo() != nullptr);
}

void MainView::resized()
{
    browser.setBounds(getLocalBounds());
    settings.setBounds(getLocalBounds());
    welcome.setBounds(getLocalBounds().withTrimmedTop(64)); // the header (settings) stays usable
    settings.zoom = proc.uiScale.load();
    prev.setBounds(width / 2 - 170, 12, 40, 40);
    presetButton.setBounds(width / 2 - 128, 12, 256, 40);
    next.setBounds(width / 2 + 130, 12, 40, 40);
    gear.setBounds(1134, 16, 36, 32);
    placeStar();

    // Category tabs sized to their names, under the title.
    float x = 52;
    for (int i = 0; i < 6; ++i) {
        int w = (int) textWidth(fonts.ui(14.0f).withExtraKerningFactor(0.06f), categoryNames[i]) + 24;
        tabs[(size_t) i].setBounds((int) x, 360, w, 44);
        x += (float) w + 4;
    }

    // Controls: SON (3) | EFFETS (3) | SORTIE (1), centred.
    const int knob = 80, gap = 36, sep = 56;
    const int groupW = 3 * knob + 2 * gap;
    const int total = groupW + sep + groupW + sep + knob;
    const int left = (width - total) / 2;
    for (int i = 0; i < 3; ++i) knobs[(size_t) i].slider.setBounds(left + i * (knob + gap), 478, knob, knob);
    for (int i = 0; i < 3; ++i) knobs[(size_t) (3 + i)].slider.setBounds(left + groupW + sep + i * (knob + gap), 478, knob, knob);
    knobs[6].slider.setBounds(left + 2 * (groupW + sep), 478, knob, knob);
    eq.setBounds(knobs[2].slider.getRight() - 40, 444, 40, 22);
    slide.setBounds(knobs[0].slider.getX(), 444, 56, 22);

    keyboard.setBounds(20, 652, width - 40, 110);
    keyboard.setKeyWidth((float) (width - 40) / 35.0f);
}

String MainView::valueText(const Knob& k) const
{
    const double v = k.slider.getValue();
    if (k.id == "attack" || k.id == "release")
        return v >= 1000.0 ? String(v / 1000.0, 1) + " s" : String(roundToInt(v)) + " ms";
    if (k.id == "output") return (v > 0.05 ? "+" : "") + String(v, 1) + " dB";
    return String(roundToInt(v)) + " %";
}

void MainView::paint(Graphics& g)
{
    g.fillAll(colours::background);

    // ---- hero ----
    auto hero = Rectangle<float>(0, 0, (float) width, 430);
    int c = proc.currentCategory();
    if (c >= 0 && c < 6 && heroes[(size_t) c].isValid()) {
        g.setImageResamplingQuality(Graphics::highResamplingQuality);
        g.drawImage(heroes[(size_t) c], hero, RectanglePlacement::fillDestination);
    }
    {
        ColourGradient side(Colour(0xef0e0705), 0, 0, Colour(0x000e0705), width * 0.62f, 0, false);
        side.addColour(0.30 / 0.62, Colour(0xc80e0705));
        g.setGradientFill(side);
        g.fillRect(hero);
        g.setGradientFill(ColourGradient(colours::background.withAlpha(0.0f), 0, 430 * 0.6f, colours::background, 0, 430, false));
        g.fillRect(hero);
        g.setGradientFill(ColourGradient(Colour(0x99000000), 0, 0, Colour(0x00000000), 0, 96, false)); // keeps the top bar readable
        g.fillRect(hero.withHeight(96));
    }

    // ---- top bar ----
    if (logo.isValid()) g.drawImageWithin(logo, 28, 16, 34, 31, RectanglePlacement::centred);
    g.setColour(colours::cream);
    g.setFont(fonts.ui(17.0f).withExtraKerningFactor(0.32f));
    g.drawText("SWELL", 74, 16, 140, 31, Justification::centredLeft);

    auto pill = Rectangle<float>(width / 2 - 174.0f, 10, 348, 44);
    g.setColour(Colour(0x730a0604));
    g.fillRoundedRectangle(pill, 22.0f);
    g.setColour(Colour(0x24fff0dc));
    g.drawRoundedRectangle(pill, 22.0f, 1.0f);
    // Discreet loading line under the preset name; the previous sound keeps playing meanwhile.
    if (shownLoading && Time::getMillisecondCounter() - loadingSince > 120) {
        auto bar = pill.reduced(26, 0).withY(pill.getBottom() - 3).withHeight(2);
        g.setColour(colours::cream.withAlpha(0.12f));
        g.fillRoundedRectangle(bar, 1);
        g.setColour(colours::accent);
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * jlimit(0.03f, 1.0f, proc.loadProgress.load())), 1);
    }

    // Output meter: 16 thin bars, then the output level.
    {
        const float level = jlimit(0.0f, 1.0f, (Decibels::gainToDecibels(meter, -60.0f) + 60.0f) / 60.0f);
        const int lit = roundToInt(level * 16);
        for (int i = 0; i < 16; ++i) {
            g.setColour(i < lit ? (i < 12 ? colours::cream.withAlpha(0.85f) : colours::accent) : colours::cream.withAlpha(0.16f));
            g.fillRoundedRectangle(990.0f + i * 5.0f, 25, 3, 14, 1);
        }
        g.setColour(colours::cream.withAlpha(0.85f));
        g.setFont(fonts.ui(13.0f));
        g.drawText(valueText(knobs[6]), 1060, 20, 64, 24, Justification::centredRight);
    }

    // ---- title block ----
    auto* info = proc.currentInfo();
    g.setColour(colours::cream.withAlpha(0.85f));
    g.setFont(fonts.ui(13.0f).withExtraKerningFactor(0.42f));
    g.drawText("ESSENTIAL GUITAR", 64, 112, 400, 20, Justification::centredLeft);

    String name = info != nullptr ? info->display : String("Swell Essential Guitar");
    auto scriptFont = fonts.script(84.0f);
    float maxW = 640.0f, w = textWidth(scriptFont, name);
    if (w > maxW) scriptFont = fonts.script(84.0f * maxW / w);
    g.setFont(scriptFont);
    g.setColour(Colours::black.withAlpha(0.35f));
    g.drawText(name, Rectangle<float>(66, 140, 900, 110), Justification::centredLeft, false);
    g.setColour(Colour(0xfffff8ee));
    g.drawText(name, Rectangle<float>(64, 136, 900, 110), Justification::centredLeft, false);

    g.setColour(colours::cream.withAlpha(0.6f));
    g.fillRect(64.0f, 262.0f, 56.0f, 1.0f);
    g.setColour(colours::cream.withAlpha(0.8f));
    g.setFont(fonts.ui(13.0f, false).withExtraKerningFactor(0.18f));
    g.drawText(infoLine(), 134, 252, 900, 20, Justification::centredLeft);

    // ---- controls ----
    auto controls = Rectangle<float>(0, 430, (float) width, 222);
    g.setGradientFill(ColourGradient(colours::background, 0, 430, colours::panel, 0, 652, false));
    g.fillRect(controls);
    {
        ColourGradient glow(Colour(0x47d67a3a), (float) width, 430, Colour(0x00d67a3a), width * 0.45f, 430 + 222 * 0.55f, true);
        g.setGradientFill(glow);
        g.fillRect(controls);
    }
    auto groupTitle = [&](const String& t, const Slider& first, const Slider& last) {
        g.drawText(t, first.getX(), 448, last.getRight() - first.getX(), 16, Justification::centred);
    };
    g.setFont(fonts.ui(11.0f).withExtraKerningFactor(0.3f));
    g.setColour(colours::cream.withAlpha(0.5f));
    groupTitle("SON", knobs[0].slider, knobs[2].slider);
    groupTitle("EFFETS", knobs[3].slider, knobs[5].slider);
    groupTitle("SORTIE", knobs[6].slider, knobs[6].slider);
    g.setColour(Colour(0x1ffff0dc));
    g.fillRect((float) (knobs[2].slider.getRight() + knobs[3].slider.getX()) * 0.5f, 470.0f, 1.0f, 140.0f);
    g.fillRect((float) (knobs[5].slider.getRight() + knobs[6].slider.getX()) * 0.5f, 470.0f, 1.0f, 140.0f);

    for (auto& k : knobs) {
        auto b = k.slider.getBounds();
        g.setColour(colours::cream);
        g.setFont(fonts.ui(15.0f));
        g.drawText(k.label, b.getX() - 20, b.getBottom() + 10, b.getWidth() + 40, 20, Justification::centred);
        g.setColour(colours::cream.withAlpha(0.62f));
        g.setFont(fonts.ui(12.0f, false));
        g.drawText(valueText(k), b.getX() - 20, b.getBottom() + 30, b.getWidth() + 40, 16, Justification::centred);
    }

    // ---- keyboard bed ----
    g.setColour(colours::panel);
    g.fillRect(0, 652, width, height - 652);
}

void MainView::refresh()
{
    const bool needBank = proc.catalog().empty();
    if (needBank != welcome.isVisible()) { welcome.setVisible(needBank); if (needBank) welcome.toFront(false); }
    const int c = proc.currentCategory(), p = proc.currentPreset();
    const bool loading = proc.loading.load();
    if (loading && ! shownLoading) loadingSince = Time::getMillisecondCounter();
    const bool fav = proc.isFavourite(c, p);
    if (c != shownCategory || p != shownPreset || loading != shownLoading || fav != shownFavourite) {
        shownCategory = c; shownPreset = p; shownLoading = loading; shownFavourite = fav;
        placeStar();
        repaint();
    }
    if (loading && std::abs(proc.loadProgress.load() - shownProgress) > 0.01f) {
        shownProgress = proc.loadProgress.load();
        repaint(width / 2 - 180, 40, 360, 20);
    }
    const float peak = proc.outputPeak.load();
    if (std::abs(peak - meter) > 0.002f) { meter = peak; repaint(980, 20, 150, 24); }
    proc.outputPeak = peak * 0.8f; // decay when the audio thread is idle
}

// ---------------------------------------------------------------- editor

Editor::Editor(Processor& p) : AudioProcessorEditor(p), view(p)
{
    const float z = jlimit(0.5f, 2.0f, p.uiScale.load()); // read before any resize callback changes it
    addAndMakeVisible(view);
    view.setBounds(0, 0, MainView::width, MainView::height);
    setResizable(true, true);
    setResizeLimits(MainView::width / 2, MainView::height / 2, MainView::width * 2, MainView::height * 2);
    getConstrainer()->setFixedAspectRatio((double) MainView::width / MainView::height);
    view.onZoom = [this](float z) { setSize(roundToInt(MainView::width * z), roundToInt(MainView::height * z)); };
    setSize(roundToInt(MainView::width * z), roundToInt(MainView::height * z));
    ready = true;
    startTimerHz(30);
}

void Editor::resized()
{
    const float z = (float) getWidth() / MainView::width;
    view.setTransform(AffineTransform::scale(z));
    if (ready) static_cast<Processor&>(processor).uiScale = z;
}

} // namespace essential
