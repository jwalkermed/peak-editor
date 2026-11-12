#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <memory>

// What the Wavetables tab's Import Audio Sample window plays, through the editor's audio output (the
// plugin's track, or the standalone app's audio device): the sample from a position, or one cycle
// looped (a position or a wave being auditioned). Changes never cut the sound off mid-wave: a jump in
// the sample crossfades, a new cycle blends into the one playing, and starts and stops fade, so
// scrubbing doesn't click. Shared by every editor in the process (use via juce::SharedResourcePointer).
// play/playCycle/stop on the message thread, render on the audio thread.
class SamplePreview
{
public:
    static constexpr int cycleLength = 256;
    using Cycle = std::array<float, cycleLength>;

    // The sample from `from` (a jump from where it was playing crossfades).
    void play (std::shared_ptr<const juce::AudioBuffer<float>> mono, double rate, int from)
    {
        const juce::SpinLock::ScopedLockType lock (stateLock);
        if (mode == Mode::sample && playing && sample == mono)
            fadingFrom = position; // the old place fades out as the new one fades in
        else
            fadingFrom = -1.0;
        crossfade = fadingFrom >= 0.0 ? xfadeLength : 0;
        sample = std::move (mono);
        sampleRate = rate;
        position = (double) from;
        reported = from;
        mode = Mode::sample;
        gainTarget = 1.0f;
        if (! playing)
            gain = 0.0f; // fade in
        playing = true;
    }

    // One cycle looped at `frequency` Hz for `seconds` (more cycles while it sounds blend in and keep
    // it going).
    void playCycle (const Cycle& cycle, double frequency, double seconds)
    {
        const juce::SpinLock::ScopedLockType lock (stateLock);
        if (mode == Mode::cycle && playing)
        {
            previousCycle = currentCycle;
            cycleBlend = 0.0f; // the new cycle blends in over a few milliseconds
        }
        else
        {
            previousCycle = cycle;
            cycleBlend = 1.0f;
            gain = 0.0f;
        }
        currentCycle = cycle;
        cycleFrequency = frequency;
        holdSeconds = seconds;
        held = 0.0;
        mode = Mode::cycle;
        gainTarget = 1.0f;
        playing = true;
        reported = -1;
    }

    // The audition's level, 0-1 (the Import Audio Sample window's Volume); changes are smoothed.
    void setVolume (float v) { volume = juce::jlimit (0.0f, 1.0f, v); }
    float getVolume() const { return volume.load(); }

    void stop()
    {
        const juce::SpinLock::ScopedLockType lock (stateLock);
        gainTarget = 0.0f; // fades out, then stops
        reported = -1;
    }

    // Where the sample has got to, in its own samples; -1 when stopped or playing a cycle.
    int getPosition() const { return reported.load(); }

    // Mixes into `out` (every channel), resampled to the output's rate.
    void render (juce::AudioBuffer<float>& out, double outRate)
    {
        if (! playing || outRate <= 0.0)
            return;
        const juce::SpinLock::ScopedTryLockType lock (stateLock);
        if (! lock.isLocked())
            return;
        const auto fadeStep = (float) (1.0 / (0.008 * outRate)); // 8 ms fades
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            gain += juce::jlimit (-fadeStep, fadeStep, gainTarget - gain);
            level += juce::jlimit (-fadeStep, fadeStep, volume.load() - level);
            float v = 0.0f;
            if (mode == Mode::sample)
                v = renderSample (outRate);
            else
                v = renderCycle (outRate, fadeStep);
            for (int c = 0; c < out.getNumChannels(); ++c)
                out.addSample (c, i, 0.8f * gain * level * v);
            if (gainTarget <= 0.0f && gain <= 0.0f)
            {
                playing = false;
                break;
            }
        }
        reported = playing && mode == Mode::sample && gainTarget > 0.0f ? (int) position : -1;
    }

private:
    enum class Mode { sample, cycle };
    static constexpr int xfadeLength = 256;

    float sampleAt (double pos) const
    {
        const auto* s = sample->getReadPointer (0);
        const int n = sample->getNumSamples();
        const int a = (int) pos;
        if (a < 0 || a + 1 >= n)
            return 0.0f;
        return s[a] + (s[a + 1] - s[a]) * (float) (pos - a);
    }

    float renderSample (double outRate)
    {
        if (sample == nullptr || (int) position + 1 >= sample->getNumSamples())
        {
            gainTarget = 0.0f; // the end
            return 0.0f;
        }
        const double step = sampleRate / outRate;
        auto v = sampleAt (position);
        if (crossfade > 0)
        {
            const auto t = (float) crossfade / (float) xfadeLength; // 1 -> 0
            v = v * (1.0f - t) + sampleAt (fadingFrom) * t;
            fadingFrom += step;
            --crossfade;
        }
        position += step;
        return v;
    }

    float renderCycle (double outRate, float fadeStep)
    {
        const auto at = [this] (const Cycle& c) {
            const auto p = phase * cycleLength;
            const auto a = (int) p % cycleLength, b = (a + 1) % cycleLength;
            return c[(size_t) a] + (c[(size_t) b] - c[(size_t) a]) * (float) (p - std::floor (p));
        };
        cycleBlend = juce::jmin (1.0f, cycleBlend + fadeStep);
        const auto v = at (currentCycle) * cycleBlend + at (previousCycle) * (1.0f - cycleBlend);
        phase += cycleFrequency / outRate;
        phase -= std::floor (phase);
        held += 1.0 / outRate;
        if (held >= holdSeconds)
            gainTarget = 0.0f;
        return v;
    }

    juce::SpinLock stateLock;
    Mode mode = Mode::sample;
    std::atomic<bool> playing { false };
    std::atomic<int> reported { -1 }; // the sample's position for the window, -1 when it isn't playing
    float gain = 0.0f, gainTarget = 0.0f, level = 0.6f;
    std::atomic<float> volume { 0.6f };

    std::shared_ptr<const juce::AudioBuffer<float>> sample;
    double sampleRate = 44100.0, position = 0.0, fadingFrom = -1.0;
    int crossfade = 0;

    Cycle currentCycle {}, previousCycle {};
    float cycleBlend = 1.0f;
    double cycleFrequency = 130.81, phase = 0.0, holdSeconds = 0.0, held = 0.0;
};
