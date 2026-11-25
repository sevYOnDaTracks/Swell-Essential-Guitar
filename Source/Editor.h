#pragma once
#include "Processor.h"

namespace essential {

namespace colours {
    inline const juce::Colour background { 0xff14110f }, panel { 0xff1c1d22 }, cream { 0xfff4eee6 },
                              accent { 0xfff2a65a }, ink { 0xff1a110b };
}

struct Fonts {
    juce::Typeface::Ptr regular, semibold;
    juce::Font ui(float height, bool bold = true) const;
    juce::Font script(float height) const;
};

class LookAndFeel final : public juce::LookAndFeel_V4 {
public:
    explicit LookAndFeel(const Fonts& f) : fonts(f) {}
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
private:
    const Fonts& fonts;
};

// A button drawn by a lambda, so each control keeps its own look in one place.
struct PaintedButton final : public juce::Button {
    PaintedButton() : juce::Button({}) {}
    std::function<void(juce::Graphics&, juce::Rectangle<float>, bool over, bool down)> painter;
    void paintButton(juce::Graphics& g, bool over, bool down) override { if (painter) painter(g, getLocalBounds().toFloat(), over, down); }
};

void drawStar(juce::Graphics&, juce::Rectangle<float>, bool filled, juce::Colour);

class Keyboard final : public juce::MidiKeyboardComponent {
public:
    Keyboard(juce::MidiKeyboardState& s, const Fonts& f);
    void drawWhiteNote(int note, juce::Graphics&, juce::Rectangle<float>, bool down, bool over, juce::Colour, juce::Colour) override;
    void drawBlackNote(int note, juce::Graphics&, juce::Rectangle<float>, bool down, bool over, juce::Colour) override;
private:
    const Fonts& fonts;
};

// Preset menu: favourites and categories on the left, presets on the right (star = favourite).
class Browser final : public juce::Component {
public:
    Browser(Processor& p, const Fonts& f);
    void open();
    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    std::function<void()> onClose, onFavouritesChanged;
private:
    static constexpr int favouritesRow = -1;
    std::vector<std::pair<int, int>> shownPresets() const;
    juce::Rectangle<float> panel() const;
    juce::Rectangle<float> row(int i) const;   // i = -1 favourites, 0..5 categories
    juce::Rectangle<float> cell(int i) const;
    static juce::Rectangle<float> starArea(juce::Rectangle<float> cell) { return cell.removeFromRight(30.0f); }
    Processor& proc;
    const Fonts& fonts;
    int shown = 0, hoverCell = -1;
};

// Settings: licence, sample bank folder, window size, version.
class Settings final : public juce::Component, private juce::ChangeListener {
public:
    Settings(Processor&, const Fonts&);
    ~Settings() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void open();
    std::function<void(float)> onZoom;
    std::function<void()> onBankChanged;
    float zoom = 1.0f;
private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }
    void refresh();
    juce::Rectangle<int> card() const { return getLocalBounds().withSizeKeepingCentre(620, 560); }
    Processor& proc;
    const Fonts& fonts;
    juce::SharedResourcePointer<swell::Licence> licence;
    PaintedButton close, deactivate, change, reveal, rescan;
    std::array<PaintedButton, 4> zooms;
    std::unique_ptr<juce::FileChooser> chooser;
};

// First run (or bank not found): how to download the sound bank and point the plugin at it.
class Welcome final : public juce::Component {
public:
    Welcome(Processor&, const Fonts&);
    void paint(juce::Graphics&) override;
    void resized() override;
    std::function<void()> onBankChanged;
private:
    juce::Rectangle<int> card() const { return getLocalBounds().withSizeKeepingCentre(660, 420); }
    Processor& proc;
    const Fonts& fonts;
    PaintedButton download, choose, retry;
    std::unique_ptr<juce::FileChooser> chooser;
};

class MainView final : public juce::Component {
public:
    explicit MainView(Processor&);
    ~MainView() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh(); // called by the editor timer
    std::function<void(float)> onZoom;
    static constexpr int width = 1200, height = 780;
private:
    struct Knob {
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        juce::String label, id;
    };
    void placeStar();
    juce::String infoLine() const;
    juce::String valueText(const Knob&) const;

    Processor& proc;
    Fonts fonts;
    LookAndFeel lnf { fonts };
    juce::Image logo;
    std::array<juce::Image, 6> heroes;
    PaintedButton prev, next, presetButton, gear, star, eq, slide;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> eqAttachment, slideAttachment;
    std::array<PaintedButton, 6> tabs;
    std::array<Knob, 7> knobs;
    Keyboard keyboard;
    Browser browser;
    Settings settings;
    Welcome welcome;
    int shownCategory = -2, shownPreset = -2;
    bool shownLoading = false, shownFavourite = false;
    juce::uint32 loadingSince = 0;
    float shownProgress = -1.0f;
    float meter = 0.0f;
};

class Editor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit Editor(Processor&);
    void resized() override;
private:
    void timerCallback() override { view.refresh(); }
    MainView view;
    bool ready = false;
};

} // namespace essential
