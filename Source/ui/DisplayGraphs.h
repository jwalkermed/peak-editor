#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The curves the screen draws while an envelope or filter control moves, as in the OB-6 and TEO-5
// editors: a glowing line over a faint grid, the part being changed brighter. Times and
// frequencies are drawn on estimated scales (the Peak doesn't publish them).
namespace DisplayGraph
{
struct Envelope
{
    int delay = 0, attack = 0, hold = 0, decay = 0, sustain = 127, release = 0; // 0-127, as the Peak stores them
    char focus = '*'; // the stage being changed: 'y' delay, 'a', 'h' hold, 'd', 's', 'r', or '*' (all)
};

struct Filter
{
    int frequency = 255, resonance = 0; // 0-255, 0-127
    int shape = 0;                      // 0 LP, 1 BP, 2 HP
    bool steep = false;                 // 24 dB (4-pole) rather than 12 dB
};

void drawEnvelope (juce::Graphics&, juce::Rectangle<float> area, const Envelope&, juce::Colour);
void drawFilter (juce::Graphics&, juce::Rectangle<float> area, const Filter&, juce::Colour);

// The filter's gain at w = frequency / cutoff (2-pole state-variable, squared for 24 dB).
double filterGain (double w, double q, int shape, bool steep);
} // namespace DisplayGraph
