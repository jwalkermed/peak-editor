#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// The pop-up windows over the front panel (menu pages, the wavetable picker): a rounded dark card
// with a blue title, small blue section headings, values in rounded cells and an amber Close.
namespace CardStyle
{
inline const juce::Colour card { 0xff1e1f22 }, border { 0xff464a50 }, cell { 0xff2a2c30 }, cellHover { 0xff383b40 },
    line { 0xff3c3f44 }, text { 0xffe6e6e6 }, dim { 0xff969aa0 }, amber { 0xffffa63c },
    barFill { 0xff344e68 },  // value bars and the scroll bar
    blue { 0xff7098be },     // titles and headings: the bars' blue, lighter so it reads as text
    closeFill { 0xff3a3020 };

// Rounded value bars (fill from the minimum, or from "centre" in the slider's properties; a "label"
// property is written inside the bar at the left),
// drop-downs and buttons ("primary" in a button's properties: the amber Close).
class Look final : public juce::LookAndFeel_V4
{
public:
    Look();

    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
};

Look& look();

// Draws the card: background, border, the title (blue) and an optional note at the top right.
void paintCard (juce::Graphics&, juce::Rectangle<int> bounds, const juce::String& title, const juce::String& note = {});

// A section heading: small blue capitals with a line under them.
void styleHeading (juce::Label&);

// Holds one card over the front panel: clicks outside it close it (like Close); Esc is passed to
// `onEscape` (or closes). The card is centred and kept inside the panel.
class Overlay final : public juce::Component
{
public:
    Overlay (std::unique_ptr<juce::Component> card, std::function<void()> onClosed);
    ~Overlay() override;

    void close(); // safe to call from the card's own buttons
    std::function<void()> onEscape;

    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    juce::Component& getCard() { return *card; }

private:
    std::unique_ptr<juce::Component> card;
    juce::Point<int> preferredSize; // the card's own size; smaller only if the panel is
    std::function<void()> onClosed;
    bool closing = false;
};
// A help card: the title, then sections (a blue heading, then bullet points), and Close. Put it in an
// Overlay; Close closes the overlay.
class HelpCard final : public juce::Component
{
public:
    struct Section
    {
        juce::String heading;
        juce::StringArray points;
    };
    HelpCard (const juce::String& title, std::vector<Section>, int width = 620);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Line
    {
        bool bullet;              // a point (else a heading)
        float y;                  // from the top of the text
        juce::TextLayout layout;  // wrapped beside the bullet, so a long point's lines line up
    };
    juce::String title;
    std::vector<Line> lines;
    juce::TextButton closeButton { "Close" };
    static constexpr int pad = 22, top = 52, bottom = 60, indent = 16;
};
} // namespace CardStyle
