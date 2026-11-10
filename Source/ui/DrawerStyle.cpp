#include "DrawerStyle.h"

#include "peak/core/ParamFormat.h"

#include "BinaryData.h"

namespace DrawerStyle
{
juce::Font font (float height)
{
    static const auto typeface = juce::Typeface::createSystemTypefaceFor (BinaryData::Exo2Bold_ttf, BinaryData::Exo2Bold_ttfSize);
    return juce::Font (juce::FontOptions (typeface).withHeight (height));
}

const peak::Patch& emptyPatch()
{
    static const auto patch = [] {
        const auto* bytes = reinterpret_cast<const uint8_t*> (BinaryData::InitPatch_syx);
        auto p = peak::Patch::fromSysex (peak::Bytes (bytes, bytes + BinaryData::InitPatch_syxSize));
        jassert (p.has_value());
        return p.value_or (peak::Patch());
    }();
    return patch;
}

int initValue (const peak::ParamDef& p)
{
    return p.isStored() ? emptyPatch().get (p) : peak::defaultValue (p);
}

bool isEmpty (const peak::Patch& p)
{
    return p.body() == emptyPatch().body();
}

void drawCheekTab (juce::Graphics& g, juce::Rectangle<float> b, const juce::String& text, bool open)
{
    b = b.reduced (1.0f, 0.0f);
    g.setColour (open ? pinstripe : juce::Colour (0xff1a1b1d).withAlpha (0.75f));
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (pinstripe.withAlpha (open ? 1.0f : 0.6f));
    g.drawRoundedRectangle (b, 3.0f, 1.0f);

    g.setColour (open ? juce::Colour (0xff101113) : silkscreen);
    g.setFont (font (juce::jmin (11.0f, b.getWidth() * 0.55f)));
    juce::Graphics::ScopedSaveState state (g);
    g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, b.getCentreX(), b.getCentreY()));
    g.drawText (text, juce::Rectangle<float> (b.getHeight(), b.getWidth()).withCentre (b.getCentre()), juce::Justification::centred, false);
}

void style (juce::TextButton& b)
{
    b.setColour (juce::TextButton::buttonColourId, button);
    b.setColour (juce::TextButton::textColourOffId, silkscreen);
    b.setColour (juce::ComboBox::outlineColourId, pinstripe.withAlpha (0.5f));
}

void style (juce::TextEditor& e)
{
    e.setColour (juce::TextEditor::backgroundColourId, field);
    e.setColour (juce::TextEditor::textColourId, silkscreen);
    e.setColour (juce::TextEditor::outlineColourId, pinstripe.withAlpha (0.6f));
    e.setColour (juce::TextEditor::focusedOutlineColourId, pinstripe);
}

void style (juce::ToggleButton& t)
{
    t.setColour (juce::ToggleButton::textColourId, silkscreen);
    t.setColour (juce::ToggleButton::tickColourId, star);
}
} // namespace DrawerStyle
