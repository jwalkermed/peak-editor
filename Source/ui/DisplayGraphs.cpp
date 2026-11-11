#include "DisplayGraphs.h"

namespace DisplayGraph
{
namespace
{
void grid (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
{
    g.setColour (colour.withAlpha (0.07f));
    for (float x = area.getX() + 8.0f; x < area.getRight(); x += 16.0f)
        g.drawVerticalLine (juce::roundToInt (x), area.getY() + 4.0f, area.getBottom() - 4.0f);
    for (float y = area.getY() + 8.0f; y < area.getBottom(); y += 16.0f)
        g.drawHorizontalLine (juce::roundToInt (y), area.getX() + 4.0f, area.getRight() - 4.0f);
}

// A glowing stroke: the line over a soft blur of itself (dimmer when it isn't the part being changed).
void glowStroke (juce::Graphics& g, const juce::Path& p, juce::Colour colour, bool bright)
{
    const float width = bright ? 2.6f : 1.9f;
    juce::Path stroked;
    juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (stroked, p);
    if (bright)
        juce::DropShadow (colour.withAlpha (0.85f), 8, {}).drawForPath (g, stroked);
    g.setColour (colour.withAlpha (bright ? 1.0f : 0.38f));
    g.fillPath (stroked);
}

// Estimated stage time over 0-127: none at 0, then up to about 10 s, on a log scale so short stages
// stay visible. It grows from zero, so 0 -> 1 -> 2 is a gradual change rather than a jump.
float stageWidth (int v)
{
    const double ms = std::pow (10.0, 4.0 * v / 127.0) - 1.0; // 0 ms at 0, ~10 s at 127
    return (float) (std::log10 (ms + 1.0) / 4.0);
}

// An exponential fall from 1 to 0 over t = 0-1 that lands exactly on 0, so the next stage joins it.
float fall (float t)
{
    constexpr float k = 4.0f;
    return (std::exp (-k * t) - std::exp (-k)) / (1.0f - std::exp (-k));
}
} // namespace

void drawEnvelope (juce::Graphics& g, juce::Rectangle<float> area, const Envelope& e, juce::Colour colour)
{
    grid (g, area, colour);
    const float pad = 8.0f, sus = e.sustain / 127.0f;
    const float base = area.getBottom() - pad, top = area.getY() + pad + 4.0f;
    const auto yOf = [&] (float level) { return base + (top - base) * level; };

    // Delay and Hold only take room when they're set (or being changed); attack 0 goes straight up.
    std::vector<std::pair<char, float>> stages;
    if (e.delay > 0 || e.focus == 'y')
        stages.push_back ({ 'y', stageWidth (e.delay) });
    stages.push_back ({ 'a', stageWidth (e.attack) });
    if (e.hold > 0 || e.focus == 'h')
        stages.push_back ({ 'h', stageWidth (e.hold) });
    stages.push_back ({ 'd', stageWidth (e.decay) });
    stages.push_back ({ 's', 0.55f });
    stages.push_back ({ 'r', stageWidth (e.release) });

    float total = 0.0f;
    for (const auto& s : stages)
        total += s.second;
    float x0 = area.getX() + pad;
    const float span = area.getWidth() - 2.0f * pad;
    for (const auto& [stage, w] : stages)
    {
        const float x1 = x0 + span * w / juce::jmax (0.001f, total);
        juce::Path p;
        for (int i = 0; i <= 24; ++i)
        {
            const float t = (float) i / 24.0f, x = x0 + (x1 - x0) * t;
            const float level = stage == 'y' ? 0.0f
                              : stage == 'a' ? 1.0f - (1.0f - t) * (1.0f - t)
                              : stage == 'h' ? 1.0f
                              : stage == 'd' ? sus + (1.0f - sus) * fall (t) // ends exactly at the sustain level
                              : stage == 's' ? sus
                                             : sus * fall (t);
            i == 0 ? p.startNewSubPath (x, yOf (level)) : p.lineTo (x, yOf (level));
        }
        glowStroke (g, p, colour, e.focus == stage || e.focus == '*');
        x0 = x1;
    }
}

double filterGain (double w, double q, int shape, bool steep)
{
    // 2-pole state-variable: D = s^2 + s/Q + 1 with s = jw; LP = 1/D, BP = (s/Q)/D, HP = s^2/D.
    const double dr = 1.0 - w * w, di = w / q, d = std::sqrt (dr * dr + di * di);
    const double g = (shape == 1 ? w / q : shape == 2 ? w * w : 1.0) / d;
    return steep ? g * g : g;
}

void drawFilter (juce::Graphics& g, juce::Rectangle<float> area, const Filter& f, juce::Colour colour)
{
    grid (g, area, colour);
    // Estimated scales: cutoff 20 Hz to 20 kHz over 0-255, Q 0.55 to 18 over resonance 0-127;
    // drawn from 20 Hz to 20 kHz, -36 to +24 dB.
    const double fc = 20.0 * std::pow (1000.0, f.frequency / 255.0), q = 0.55 * std::pow (33.0, f.resonance / 127.0);
    const float pad = 6.0f;
    const auto yOf = [&] (double db) {
        return area.getBottom() - pad - (float) juce::jlimit (0.0, 1.0, (db + 36.0) / 60.0) * (area.getHeight() - 2.0f * pad);
    };
    // 0 dB, dotted.
    juce::Path zero, dotted;
    zero.startNewSubPath (area.getX() + pad, yOf (0.0));
    zero.lineTo (area.getRight() - pad, yOf (0.0));
    const float dashes[] = { 3.0f, 4.0f };
    juce::PathStrokeType (1.0f).createDashedStroke (dotted, zero, dashes, 2);
    g.setColour (colour.withAlpha (0.25f));
    g.fillPath (dotted);

    juce::Path p;
    for (int i = 0; i <= 160; ++i)
    {
        const double hz = 20.0 * std::pow (1000.0, i / 160.0);
        const double db = 20.0 * std::log10 (juce::jmax (1.0e-4, filterGain (hz / fc, q, f.shape, f.steep)));
        const float x = area.getX() + pad + (area.getWidth() - 2.0f * pad) * (float) i / 160.0f;
        i == 0 ? p.startNewSubPath (x, yOf (db)) : p.lineTo (x, yOf (db));
    }
    glowStroke (g, p, colour, true);
}
} // namespace DisplayGraph
