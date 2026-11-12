#include "AudioSampleImporter.h"

#include "ui/CardStyle.h"

namespace
{
using namespace CardStyle;

constexpr int cardWidth = 780, cardHeight = 400, pad = 22;

void drawWave (juce::Graphics& g, juce::Rectangle<float> r, const std::vector<float>& s, juce::Colour c, float width)
{
    if (s.empty())
        return;
    juce::Path p;
    for (size_t i = 0; i <= s.size(); ++i)
    {
        const auto x = r.getX() + r.getWidth() * (float) i / (float) s.size();
        const auto y = r.getCentreY() - r.getHeight() * 0.45f * juce::jlimit (-1.1f, 1.1f, s[i % s.size()]);
        i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
    }
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
} // namespace

// The round transport buttons (Back, Play/Stop, Forward) and the close cross.
class AudioSampleImporter::Transport final : public juce::Button
{
public:
    enum Kind { back, play, forward, close };
    Transport (Kind k, const juce::String& tip) : juce::Button (tip), kind (k) { setTooltip (tip); }
    bool playing = false;

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat();
        const auto c = r.getCentre();
        const auto colour = over || down ? juce::Colours::white : CardStyle::text;
        g.setColour (colour);
        if (kind == close)
        {
            g.drawLine (c.x - 6, c.y - 6, c.x + 6, c.y + 6, 1.6f);
            g.drawLine (c.x - 6, c.y + 6, c.x + 6, c.y - 6, 1.6f);
            return;
        }
        if (kind == play)
        {
            g.drawEllipse (r.reduced (1.5f), 1.6f);
            if (playing)
            {
                g.fillRect (juce::Rectangle<float> (4.0f, 13.0f).withCentre ({ c.x - 4.0f, c.y }));
                g.fillRect (juce::Rectangle<float> (4.0f, 13.0f).withCentre ({ c.x + 4.0f, c.y }));
            }
            else
            {
                juce::Path t;
                t.addTriangle (c.x - 4.0f, c.y - 8.0f, c.x - 4.0f, c.y + 8.0f, c.x + 8.0f, c.y);
                g.fillPath (t);
            }
            return;
        }
        // Two small triangles, pointing back or forward.
        const auto d = kind == back ? -1.0f : 1.0f;
        for (const auto offset : { -5.0f, 4.0f })
        {
            juce::Path t;
            const auto x = c.x + offset * d;
            t.addTriangle (x - 4.5f * d, c.y - 6.0f, x - 4.5f * d, c.y + 6.0f, x + 4.5f * d, c.y);
            g.fillPath (t);
        }
    }

private:
    Kind kind;
};

AudioSampleImporter::AudioSampleImporter (const juce::String& fileName, std::shared_ptr<const juce::AudioBuffer<float>> mono, double sampleRate)
    : name (fileName), audio (std::move (mono)), rate (sampleRate)
{
    setLookAndFeel (&look());
    back = std::make_unique<Transport> (Transport::back, "Back 1 second");
    play = std::make_unique<Transport> (Transport::play, "Play from the chosen location (Space)");
    forward = std::make_unique<Transport> (Transport::forward, "Forward 1 second");
    close = std::make_unique<Transport> (Transport::close, "Close");
    back->onClick = [this] { setStart (start - (int) rate); };
    forward->onClick = [this] { setStart (start + (int) rate); };
    play->onClick = [this] { togglePlay(); };
    close->onClick = [this] { if (onClose) onClose(); };
    cancelButton.onClick = [this] { if (onClose) onClose(); };

    waveformButton.setButtonText (juce::String::fromUTF8 ("+  Import Waveform"));
    waveformButton.setTooltip ("The chosen one of the five waves (outlined), into the editor's selected wave");
    volume.setRange (0.0, 1.0, 0.0);
    volume.setValue (0.6, juce::dontSendNotification);
    volume.setTooltip ("How loud the audition plays here (not on the Peak)");
    volume.onValueChange = [this] { if (onVolume) onVolume ((float) volume.getValue()); };
    playOnPeak.setToggleState (true, juce::dontSendNotification);
    playOnPeak.setTooltip ("What you audition here is also sent to the Peak's user slot, to play from your keyboard. "
                           "Closing without importing puts the Peak back as it was");
    playOnPeak.onClick = [this] {
        if (playOnPeak.getToggleState())
            audition();
        else if (onPeakOff)
            onPeakOff();
    };
    wavetableButton.setButtonText (juce::String::fromUTF8 ("+  Import Wavetable"));
    wavetableButton.setTooltip ("Five successive cycles from the chosen location, into all five waves (they morph with Wave Table Position)");
    wavetableButton.getProperties().set ("primary", true);
    waveformButton.onClick = [this] {
        if (onImport && juce::isPositiveAndBelow (chosenWave, (int) preview.size()))
            onImport ({ preview[(size_t) chosenWave] }, name);
    };
    wavetableButton.onClick = [this] { if (onImport) onImport (preview, name); };

    for (auto* c : std::initializer_list<juce::Component*> { back.get(), play.get(), forward.get(), close.get(), &cancelButton, &waveformButton, &wavetableButton, &playOnPeak, &volume })
        addAndMakeVisible (c);
    setWantsKeyboardFocus (true);
    setSize (cardWidth, cardHeight);
    update();
}

AudioSampleImporter::~AudioSampleImporter()
{
    if (onPlay)
        onPlay (false, 0);
    setLookAndFeel (nullptr);
}

void AudioSampleImporter::setVolume (float v)
{
    volume.setValue (v, juce::dontSendNotification);
}

void AudioSampleImporter::setStart (int sample)
{
    start = juce::jlimit (0, juce::jmax (0, audio->getNumSamples() - 1), sample);
    if (playhead >= 0 && onPlay)
        onPlay (true, start); // scrubbing while it plays: carry on from here
    update();
}

void AudioSampleImporter::update()
{
    const auto* s = audio->getReadPointer (0);
    spans = peak::Slicing::spans (s, audio->getNumSamples(), start, rate, 5);
    preview.clear();
    for (const auto& span : spans)
        preview.push_back (peak::Slicing::waveFrom (s, audio->getNumSamples(), span));
    repaint();
}

void AudioSampleImporter::togglePlay()
{
    if (! onPlay)
        return;
    const bool starting = playhead < 0;
    onPlay (starting, start);
    playhead = starting ? start : -1;
    play->playing = starting;
    play->repaint();
    starting ? startTimerHz (30) : stopTimer();
}

void AudioSampleImporter::timerCallback()
{
    playhead = playPosition ? playPosition() : -1;
    if (playhead < 0)
    {
        stopTimer();
        play->playing = false;
        play->repaint();
    }
    repaint (timeline.expanded (4));
}

float AudioSampleImporter::xFor (int sample) const
{
    return (float) timeline.getX() + (float) timeline.getWidth() * (float) sample / (float) juce::jmax (1, audio->getNumSamples());
}

int AudioSampleImporter::sampleAt (float x) const
{
    return (int) ((x - (float) timeline.getX()) / (float) juce::jmax (1, timeline.getWidth()) * (float) audio->getNumSamples());
}

juce::String AudioSampleImporter::timeText (int sample) const
{
    const auto seconds = (double) sample / rate;
    return juce::String ((int) seconds / 60) + ":" + juce::String (std::fmod (seconds, 60.0), 2).paddedLeft ('0', 5);
}

void AudioSampleImporter::paint (juce::Graphics& g)
{
    paintCard (g, getLocalBounds(), "Import Audio Sample");
    auto r = getLocalBounds().reduced (pad, 0);
    r.removeFromTop (52);
    g.setColour (line);
    g.fillRect (r.getX(), r.getY(), r.getWidth(), 1);
    r.removeFromTop (14);
    auto info = r.removeFromTop (20);
    g.setColour (text);
    g.setFont (juce::FontOptions (13.5f));
    g.drawText ("Select a location in the sample where you would like to begin your wavetable", info, juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (name + juce::String::fromUTF8 ("  \xc2\xb7  ") + juce::String ((double) audio->getNumSamples() / rate, 1) + " s", info, juce::Justification::centredRight);

    // Volume: a small speaker before the slider.
    {
        const auto c = volumeLabel.toFloat().withTrimmedRight (10.0f).removeFromRight (18.0f).withSizeKeepingCentre (18.0f, 14.0f);
        juce::Path speaker;
        speaker.startNewSubPath (c.getX(), c.getCentreY() - 3.0f);
        speaker.lineTo (c.getX() + 4.0f, c.getCentreY() - 3.0f);
        speaker.lineTo (c.getX() + 9.0f, c.getY());
        speaker.lineTo (c.getX() + 9.0f, c.getBottom());
        speaker.lineTo (c.getX() + 4.0f, c.getCentreY() + 3.0f);
        speaker.lineTo (c.getX(), c.getCentreY() + 3.0f);
        speaker.closeSubPath();
        g.setColour (dim);
        g.fillPath (speaker);
        juce::Path wave;
        wave.addCentredArc (c.getX() + 9.0f, c.getCentreY(), 5.0f, 5.0f, 0.0f, 0.7f, 2.45f, true);
        wave.addCentredArc (c.getX() + 9.0f, c.getCentreY(), 8.5f, 8.5f, 0.0f, 0.7f, 2.45f, true);
        g.strokePath (wave, juce::PathStrokeType (1.3f));
    }

    // The waveform, the waves' span (amber), the chosen location and the playhead.
    const auto t = timeline.toFloat();
    g.setColour (juce::Colour (0xff141517));
    g.fillRoundedRectangle (t, 4.0f);
    if (! spans.empty())
    {
        const auto x0 = xFor (spans.front().start), x1 = xFor (spans.back().start + spans.back().length);
        g.setColour (amber.withAlpha (0.18f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x0, t.getY(), juce::jmax (x0 + 2.0f, x1), t.getBottom()));
    }
    g.setColour (blue.withAlpha (0.85f));
    for (int x = 0; x < (int) overview.size(); ++x)
    {
        const auto [lo, hi] = overview[(size_t) x];
        g.drawVerticalLine (timeline.getX() + x, t.getCentreY() - hi * t.getHeight() * 0.46f, t.getCentreY() - lo * t.getHeight() * 0.46f + 1.0f);
    }
    g.setColour (line.brighter (0.2f));
    g.drawHorizontalLine (juce::roundToInt (t.getCentreY()), t.getX(), t.getRight());
    const auto mx = xFor (start);
    g.setColour (amber);
    g.fillRect (juce::Rectangle<float> (2.0f, t.getHeight() + 8.0f).withCentre ({ mx, t.getCentreY() }));
    juce::Path handle;
    handle.addTriangle (mx - 6.0f, t.getY() - 9.0f, mx + 6.0f, t.getY() - 9.0f, mx, t.getY() - 2.0f);
    g.fillPath (handle);
    if (playhead >= 0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.fillRect (juce::Rectangle<float> (1.0f, t.getHeight()).withCentre ({ xFor (playhead), t.getCentreY() }));
    }
    g.setColour (line);
    g.drawRoundedRectangle (t, 4.0f, 1.0f);
    g.setColour (dim);
    g.setFont (juce::FontOptions (12.0f));
    const auto times = timeline.translated (0, timeline.getHeight() + 4).withHeight (16);
    g.setColour (amber);
    g.drawText (timeText (playhead >= 0 ? playhead : start), times, juce::Justification::centredLeft);
    g.setColour (dim);
    g.drawText (timeText (audio->getNumSamples()), times, juce::Justification::centredRight);

    // The waves Import will take from here: the outlined one is Import Waveform's (click one to hear it
    // and choose it); all 5 are Import Wavetable's.
    g.setColour (dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("From here:", previews.withWidth (80), juce::Justification::centredLeft);
    for (int i = 0; i < (int) preview.size(); ++i)
    {
        const auto b = previewBounds (i).toFloat();
        const bool chosen = i == chosenWave;
        g.setColour (juce::Colour (0xff141517));
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (chosen ? amber.withAlpha (0.8f) : line);
        g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, chosen ? 1.5f : 1.0f);
        drawWave (g, b.reduced (6.0f, 5.0f), preview[(size_t) i].samples(), chosen ? amber : text.withAlpha (0.8f), 1.3f);
        g.setColour (dim);
        g.setFont (juce::FontOptions (10.5f));
        g.drawText (juce::String (i + 1), b.reduced (5.0f, 2.0f), juce::Justification::topLeft);
    }
}

juce::Rectangle<int> AudioSampleImporter::previewBounds (int wave) const
{
    const int gap = 8, w = (previews.getWidth() - 80 - 4 * gap) / 5;
    return { previews.getX() + 80 + wave * (w + gap), previews.getY(), w, previews.getHeight() };
}

void AudioSampleImporter::resized()
{
    close->setBounds (getWidth() - 44, 12, 28, 28);
    auto r = getLocalBounds().reduced (pad, 0);
    r.removeFromTop (52 + 14 + 20 + 14);
    auto transportRow = r.removeFromTop (44);
    volume.setBounds (transportRow.removeFromRight (150).withSizeKeepingCentre (150, 24));
    volumeLabel = transportRow.removeFromRight (60);
    auto transport = transportRow.withX (getLocalBounds().getCentreX() - 80).withWidth (160);
    back->setBounds (transport.removeFromLeft (40).withSizeKeepingCentre (30, 30));
    forward->setBounds (transport.removeFromRight (40).withSizeKeepingCentre (30, 30));
    play->setBounds (transport.withSizeKeepingCentre (44, 44));
    r.removeFromTop (16);
    timeline = r.removeFromTop (72);
    r.removeFromTop (26);
    previews = r.removeFromTop (46);
    auto buttons = r.withTrimmedTop (16).removeFromTop (34);
    cancelButton.setBounds (buttons.removeFromLeft (120));
    buttons.removeFromLeft (16);
    playOnPeak.setBounds (buttons.removeFromLeft (150));
    wavetableButton.setBounds (buttons.removeFromRight (210));
    buttons.removeFromRight (12);
    waveformButton.setBounds (buttons.removeFromRight (210));

    // The overview: min and max of each pixel column.
    overview.assign ((size_t) juce::jmax (1, timeline.getWidth()), { 0.0f, 0.0f });
    const auto* s = audio->getReadPointer (0);
    const auto n = audio->getNumSamples();
    for (int x = 0; x < (int) overview.size(); ++x)
    {
        const int a = (int) ((juce::int64) x * n / (int) overview.size()), b = juce::jmax (a + 1, (int) ((juce::int64) (x + 1) * n / (int) overview.size()));
        auto& [lo, hi] = overview[(size_t) x];
        for (int i = a; i < juce::jmin (b, n); ++i)
        {
            lo = juce::jmin (lo, s[i]);
            hi = juce::jmax (hi, s[i]);
        }
    }
    // Drawn to the sample's loudest point, so quiet recordings are visible too.
    float loudest = 1.0e-4f;
    for (const auto& [lo, hi] : overview)
        loudest = juce::jmax (loudest, -lo, hi);
    for (auto& [lo, hi] : overview)
    {
        lo /= loudest;
        hi /= loudest;
    }
}

void AudioSampleImporter::audition()
{
    // Just this point: the chosen wave, heard briefly (while the sample plays, the playback moves here
    // instead), and on the Peak if that's on.
    if (playhead >= 0 || ! onAudition || ! juce::isPositiveAndBelow (chosenWave, (int) preview.size()))
        return;
    lastAuditionMs = juce::Time::getMillisecondCounterHiRes();
    onAudition (preview[(size_t) chosenWave], playOnPeak.getToggleState());
}

void AudioSampleImporter::mouseDown (const juce::MouseEvent& e)
{
    if (timeline.expanded (0, 10).contains (e.getPosition()))
    {
        setStart (sampleAt (e.position.x));
        audition();
        return;
    }
    for (int i = 0; i < (int) preview.size(); ++i)
        if (previewBounds (i).contains (e.getPosition()))
        {
            if (playhead >= 0)
                togglePlay(); // stop the sample to hear this wave
            chosenWave = i;
            repaint (previews.expanded (4));
            audition();
            return;
        }
}

void AudioSampleImporter::mouseDrag (const juce::MouseEvent& e)
{
    if (timeline.expanded (0, 10).contains (e.getMouseDownPosition()))
    {
        setStart (sampleAt (e.position.x));
        if (juce::Time::getMillisecondCounterHiRes() - lastAuditionMs > 150.0) // scrubbing: a new cycle every so often
            audition();
    }
}

bool AudioSampleImporter::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
        return togglePlay(), true;
    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)
        return setStart (start + (key == juce::KeyPress::leftKey ? -1 : 1) * (int) (rate / 100.0)), true; // 10 ms
    return false;
}

//==============================================================================
LoadChoiceCard::LoadChoiceCard (const juce::String& fileName) : name (fileName)
{
    setLookAndFeel (&look());
    asWavetable.setButtonText ("Wavetable");
    asAudio.setButtonText (juce::String::fromUTF8 ("Audio file\xe2\x80\xa6"));
    asWavetable.onClick = [this] { if (onChoice) onChoice (false); };
    asAudio.onClick = [this] { if (onChoice) onChoice (true); };
    cancel.onClick = [this] { if (onCancel) onCancel(); };
    for (auto* b : { &asWavetable, &asAudio, &cancel })
        addAndMakeVisible (b);
    setSize (460, 262);
}

void LoadChoiceCard::paint (juce::Graphics& g)
{
    paintCard (g, getLocalBounds(), juce::String::fromUTF8 ("Load \xe2\x80\x9c") + name + juce::String::fromUTF8 ("\xe2\x80\x9d"));
    g.setColour (line);
    g.fillRect (22, 52, getWidth() - 44, 1);
    g.setColour (dim);
    g.setFont (juce::FontOptions (12.5f));
    const auto describe = [&] (const juce::Button& b, const juce::String& t) {
        g.drawFittedText (t, b.getBounds().withX (b.getRight() + 14).withRight (getWidth() - 22), juce::Justification::centredLeft, 3);
    };
    describe (asWavetable, "As it is: a single cycle goes into the selected wave; a longer file (e.g. a Serum wavetable) fills all five.");
    describe (asAudio, "Play and scrub through it, choose where to begin, then import one waveform or a wavetable.");
}

void LoadChoiceCard::resized()
{
    asWavetable.setBounds (22, 72, 130, 48);
    asAudio.setBounds (22, 134, 130, 48);
    cancel.setBounds (getWidth() - 22 - 100, getHeight() - 52, 100, 32);
}
