#include "CardStyle.h"

namespace CardStyle
{
Look::Look()
{
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, text);
    setColour (juce::Label::textColourId, text);
    setColour (juce::PopupMenu::backgroundColourId, card);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, cellHover);
    setColour (juce::PopupMenu::highlightedTextColourId, amber);
}

void Look::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle style,
                             juce::Slider& slider)
{
    if (style != juce::Slider::LinearBar)
        return LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0.0f, 0.0f, style, slider);

    const auto box = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (0.5f, 1.5f);
    g.setColour (cell);
    g.fillRoundedRectangle (box, 5.0f);

    // From the minimum, or from the centre for values shown as -/+.
    const auto centre = slider.getProperties()["centre"];
    const auto from = centre.isVoid() ? box.getX() + 2.0f : (float) slider.getPositionOfValue ((double) centre);
    const auto fill = juce::Rectangle<float>::leftTopRightBottom (juce::jmin (from, pos), box.getY() + 2.0f, juce::jmax (from, pos), box.getBottom() - 2.0f);
    if (fill.getWidth() > 1.0f)
    {
        g.setColour (barFill);
        g.fillRoundedRectangle (fill, 4.0f);
    }
    if (! centre.isVoid())
    {
        g.setColour (dim);
        g.fillRect (juce::Rectangle<float> (1.0f, box.getHeight() - 8.0f).withCentre ({ from, box.getCentreY() }));
    }
    g.setColour (slider.isMouseOverOrDragging() ? dim : line);
    g.drawRoundedRectangle (box, 5.0f, 1.0f);
}

juce::Label* Look::createSliderTextBox (juce::Slider& slider)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (slider);
    l->setJustificationType (juce::Justification::centredRight);
    l->setBorderSize ({ 1, 4, 1, 10 });
    l->setFont (juce::FontOptions (13.0f));
    return l;
}

void Look::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> ((float) w, (float) h).reduced (0.5f, 1.5f);
    g.setColour (box.isMouseOver (true) ? cellHover : cell);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (line);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    juce::Path arrow;
    const auto c = juce::Point<float> (r.getRight() - 14.0f, r.getCentreY());
    arrow.addTriangle (c.x - 4.0f, c.y - 2.0f, c.x + 4.0f, c.y - 2.0f, c.x, c.y + 3.0f);
    g.setColour (dim);
    g.fillPath (arrow);
}

void Look::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

juce::Font Look::getComboBoxFont (juce::ComboBox&)
{
    return juce::FontOptions (13.0f);
}

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool primary = b.getProperties()["primary"];
    g.setColour (primary ? closeFill.brighter (over ? 0.15f : 0.0f) : (over || down ? cellHover : cell));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (primary ? amber : line);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

juce::Font Look::getTextButtonFont (juce::TextButton&, int)
{
    return juce::FontOptions (14.0f);
}

Look& look()
{
    static Look instance;
    return instance;
}

void paintCard (juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title, const juce::String& note)
{
    const auto b = bounds.toFloat().reduced (0.5f);
    g.setColour (card);
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (border);
    g.drawRoundedRectangle (b, 10.0f, 1.0f);

    auto top = bounds.reduced (18, 0).withHeight (52);
    g.setColour (blue);
    g.setFont (juce::FontOptions (17.0f));
    g.drawText (title, top, juce::Justification::centredLeft);
    if (note.isNotEmpty())
    {
        g.setColour (dim);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (note, top, juce::Justification::centredRight);
    }
}

void styleHeading (juce::Label& l)
{
    l.setFont (juce::FontOptions (12.0f));
    l.setColour (juce::Label::textColourId, blue);
    l.setBorderSize ({ 0, 0, 0, 0 });
}

//==============================================================================
Overlay::Overlay (std::unique_ptr<juce::Component> c, std::function<void()> closed) : card (std::move (c)), onClosed (std::move (closed))
{
    preferredSize = { card->getWidth(), card->getHeight() };
    addAndMakeVisible (*card);
    setWantsKeyboardFocus (true);
}

Overlay::~Overlay() = default;

void Overlay::close()
{
    if (std::exchange (closing, true))
        return;
    // Later, so a button inside the card can call this from its own click.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Overlay> (this)] {
        if (safe != nullptr && safe->onClosed)
            safe->onClosed(); // the owner deletes the overlay
    });
}

void Overlay::resized()
{
    const auto area = getLocalBounds().reduced (12);
    card->setBounds (juce::Rectangle<int> (juce::jmin (preferredSize.x, area.getWidth()), juce::jmin (preferredSize.y, area.getHeight()))
                         .withCentre (area.getCentre()));
}

void Overlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.35f)); // the panel stays visible behind
}

void Overlay::mouseDown (const juce::MouseEvent& e)
{
    if (! card->getBounds().contains (e.getPosition()))
        close();
}

bool Overlay::keyPressed (const juce::KeyPress& key)
{
    if (key.isKeyCode (juce::KeyPress::escapeKey))
    {
        if (onEscape)
            onEscape();
        else
            close();
        return true;
    }
    return false;
}
} // namespace CardStyle
