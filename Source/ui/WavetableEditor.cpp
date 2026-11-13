#include "WavetableEditor.h"

#include "peak/core/ParamFormat.h"
#include "peak/core/WavFile.h"
#include "ui/CardStyle.h"

namespace
{
using namespace CardStyle;

constexpr int topBar = 46, pad = 18, rightWidth = 380, harmonicBars = 64;
const char* const shapeNames[] { "Sine", "Square", "Pulse", "Saw", "Triangle", "Noise" };
const char* const opNames[] { "Copy all waves", "Blend 1 \xe2\x86\x92 5", "Normalise", "Reverse", "Invert" };
constexpr int opCount = 5;

// An audio file mixed to mono, up to `maxSeconds`. JUCE's reader first; for a WAV it can't use (or
// takes as empty, as with a data chunk longer than the file) the WAV is read directly.
struct MonoAudio
{
    std::shared_ptr<juce::AudioBuffer<float>> samples;
    double rate = 0.0;
};
std::optional<MonoAudio> readMono (const juce::File& file, double maxSeconds)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file)); reader != nullptr && reader->lengthInSamples > 0)
    {
        const auto length = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * maxSeconds));
        juce::AudioBuffer<float> all ((int) juce::jmax (1u, reader->numChannels), length);
        reader->read (&all, 0, length, 0, true, true);
        auto mono = std::make_shared<juce::AudioBuffer<float>> (1, length);
        mono->clear();
        for (int c = 0; c < all.getNumChannels(); ++c)
            mono->addFrom (0, 0, all, c, 0, length, 1.0f / (float) all.getNumChannels());
        return MonoAudio { mono, reader->sampleRate };
    }
    juce::MemoryBlock data;
    if (! file.hasFileExtension ("wav") || ! file.loadFileAsData (data))
        return std::nullopt;
    const auto wav = peak::readWav (static_cast<const uint8_t*> (data.getData()), data.getSize());
    if (! wav || wav->samples.empty())
        return std::nullopt;
    const auto length = (int) std::min<double> ((double) wav->samples.size(), wav->sampleRate * maxSeconds);
    auto mono = std::make_shared<juce::AudioBuffer<float>> (1, length);
    std::copy_n (wav->samples.begin(), length, mono->getWritePointer (0));
    return MonoAudio { mono, wav->sampleRate };
}

// The editor's full scale reaches its top and bottom edges, so the grid has 4 rows above the line and 4 below.
constexpr float editorScale = 0.5f;

void drawWave (juce::Graphics& g, juce::Rectangle<float> r, const std::vector<float>& s, juce::Colour colour, float width, bool fill,
               float scale = 0.46f)
{
    if (s.empty())
        return;
    const auto mid = r.getCentreY(), amp = r.getHeight() * scale;
    juce::Path p;
    for (size_t i = 0; i <= s.size(); ++i) // one cycle, closed by the first point again at the right edge
    {
        const auto x = r.getX() + r.getWidth() * (float) i / (float) s.size();
        const auto y = mid - amp * juce::jlimit (-1.1f, 1.1f, s[i % s.size()]);
        i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
    }
    if (fill)
    {
        juce::Path area (p);
        area.lineTo (r.getRight(), mid);
        area.lineTo (r.getX(), mid);
        area.closeSubPath();
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.28f), 0.0f, r.getY(), colour.withAlpha (0.28f), 0.0f, r.getBottom(), false));
        g.setColour (colour.withAlpha (0.16f));
        g.fillPath (area);
    }
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
// A shape button: its name and picture; a click picks the shape to draw with, a double-click fills
// the wave with it.
struct ShapeButton final : juce::TextButton
{
    explicit ShapeButton (int shape) : index (shape) {}
    const int index;
    std::function<void()> onDoubleClick;

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (onDoubleClick)
            onDoubleClick();
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        juce::TextButton::paintButton (g, over, down);
        const auto b = getLocalBounds().toFloat();
        g.setColour (CardStyle::text);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (shapeNames[index], b.withTrimmedLeft (10.0f).withTrimmedRight (44.0f), juce::Justification::centredLeft);
        std::vector<float> icon (index == peak::Shapes::noise ? 40 : 97);
        unsigned seed = 7;
        for (size_t k = 0; k < icon.size(); ++k)
            icon[k] = 0.8f * peak::Shapes::at ((peak::Shapes::Kind) index, (float) k / (float) icon.size(), seed);
        drawWave (g, { b.getRight() - 40.0f, 7.0f, 30.0f, b.getHeight() - 14.0f }, icon, amber, 1.6f, false);
    }
};
} // namespace

WavetableEditor::WavetableEditor (PeakModel& m)
    : model (m), keyboard (m.getConnection().keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&look());
    setWantsKeyboardFocus (true);

    helpButton.setTooltip ("How to use the wavetable editor");
    helpButton.onClick = [this] { showHelp(); };
    undoButton.onClick = [this] { undoRedo (true); };
    redoButton.onClick = [this] { undoRedo (false); };
    undoButton.setTooltip ("Undo (Cmd+Z)");
    redoButton.setTooltip ("Redo (Shift+Cmd+Z)");

    nameField.setInputRestrictions (peak::Wavetable::nameLength);
    nameField.setText (table.name, juce::dontSendNotification);
    nameField.setColour (juce::TextEditor::backgroundColourId, cell);
    nameField.setColour (juce::TextEditor::outlineColourId, line);
    nameField.setColour (juce::TextEditor::textColourId, text);
    nameField.setFont (juce::FontOptions (15.0f));
    nameField.setJustification (juce::Justification::centredLeft);
    nameField.setIndents (8, 0);
    nameField.setTooltip ("The wavetable's name on the Peak (up to 7 characters)");
    nameField.onTextChange = [this] {
        table.name = nameField.getText().toStdString();
        edited();
    };

    liveEdit.setTooltip ("Send every change to the selected slot as you make it (to keep it, save it on the Peak)");
    liveEdit.setColour (juce::ToggleButton::textColourId, text);
    liveEdit.onClick = [this] {
        if (liveEdit.getToggleState())
        {
            startTimer (50);
            auditionPosition();
        }
    };

    slotBox.setTooltip ("The user slot to load from and send to");
    slotBox.onChange = [this] { setStatus ({}); };

    loadButton.setTooltip ("Read the selected user slot from the Peak");
    saveButton.setTooltip ("Save as a .syx file (Components can load it)");
    sendButton.setTooltip ("Send to the selected user slot. It plays at once; to keep it, save it on the Peak");
    sendButton.getProperties().set ("primary", true);
    importButton.setTooltip ("A single file from anywhere: a .syx wavetable, or a WAV or MP3 (as a wavetable, or as a sample to take waves from)");
    loadButton.onClick = [this] { loadFromPeak(); };
    importButton.onClick = [this] { import(); };
    saveButton.onClick = [this] { saveFile(); };
    sendButton.onClick = [this] { sendToPeak(); };
    savedButton.setTooltip ("Clears the \"unsaved on Peak\" mark once you've saved it on the Peak");
    savedButton.onClick = [this] {
        if (const auto slot = slotBox.getSelectedId() - 1; slot >= 0)
            unsavedOnPeak[(size_t) slot] = false;
        refreshSlots();
        setStatus ("Marked as saved on the Peak");
    };

    pencilButton.setButtonText (juce::String::fromUTF8 ("\xe2\x9c\x8e"));
    lineButton.setButtonText (juce::String::fromUTF8 ("\xe2\x95\xb1"));
    pencilButton.setTooltip ("Pencil: draw freehand");
    lineButton.setTooltip ("Line: drag a straight line");
    pencilButton.onClick = [this] { selectTool (Tool::pencil); };
    lineButton.onClick = [this] { selectTool (Tool::line); };

    for (int i = 0; i < 6; ++i)
    {
        auto* b = new ShapeButton (i);
        shapeButtons.add (b);
        b->setTooltip (juce::String ("Click: draw ") + shapeNames[i] + " cycles (drag across the wave: length, and height up or down). "
                       "Double-click: fill the wave with one");
        b->onClick = [this, i] { selectTool (Tool::shape, (peak::Shapes::Kind) i); };
        b->onDoubleClick = [this, i] {
            beginEdit();
            setWave (peak::Shapes::make ((peak::Shapes::Kind) i, (unsigned) juce::Time::getMillisecondCounter()));
        };
    }
    selectTool (Tool::pencil);

    snapToGrid.setToggleState (true, juce::dontSendNotification);
    snapToGrid.setColour (juce::ToggleButton::textColourId, text);
    snapToGrid.setTooltip ("Line and shape drawing snap to the grid (1/16 of the cycle across, quarters of full scale up and down)");
    for (int i = 0; i < opCount; ++i)
    {
        auto* b = opButtons.add (new juce::TextButton (juce::String::fromUTF8 (opNames[i])));
        b->onClick = [this, i] {
            beginEdit();
            auto& waves = table.waves;
            const auto w = waves[(size_t) selectedWave];
            switch (i)
            {
                case 0: for (auto& x : waves) x = w; break;
                case 1: for (int k = 1; k < 4; ++k) waves[(size_t) k] = peak::WaveOps::blend (waves[0], waves[4], (float) k / 4.0f); break;
                case 2: waves[(size_t) selectedWave] = peak::WaveOps::normalise (w); break;
                case 3: waves[(size_t) selectedWave] = peak::WaveOps::reverse (w); break;
                case 4: waves[(size_t) selectedWave] = peak::WaveOps::invert (w); break;
            }
            edited();
        };
    }
    opButtons[1]->setTooltip ("Fills waves 2, 3 and 4 with steps between wave 1 and wave 5");

    smoothing.setRange (0.0, 100.0, 1.0);
    smoothing.setValue (50.0, juce::dontSendNotification);
    smoothing.setTextValueSuffix ("%");
    smoothing.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
    smoothing.setTooltip ("How much pencil drawing is smoothed (softens the upper harmonics)");

    positionSlider.setRange (0.0, 4.0, 0.0);
    positionSlider.setTooltip ("Preview the morph across the 5 waves (Wave Table Position)");
    positionSlider.onValueChange = [this] {
        position = (float) positionSlider.getValue();
        repaint (editorArea);
        auditionPosition();
    };

    levelButton.onClick = [this] { phaseView = false; levelButton.getProperties().set ("primary", true); phaseButton.getProperties().set ("primary", false); repaint(); };
    phaseButton.onClick = [this] { phaseView = true; phaseButton.getProperties().set ("primary", true); levelButton.getProperties().set ("primary", false); repaint(); };
    levelButton.getProperties().set ("primary", true);
    resetButton.setTooltip ("Make the selected wave a flat line, to draw on");
    resetButton.onClick = [this] { beginEdit(); setWave (peak::WaveOps::clear()); };
    resetAllButton.setTooltip ("Make all 5 waves flat lines (Undo brings them back)");
    resetAllButton.onClick = [this] {
        beginEdit();
        table.waves.fill (peak::WaveOps::clear());
        edited();
    };

    status.setFont (juce::FontOptions (13.5f));
    status.setColour (juce::Label::textColourId, dim);
    status.setJustificationType (juce::Justification::topLeft);

    keyboard.setAvailableRange (36, 98);
    keyboard.setLowestVisibleKey (36);
    keyboard.setScrollButtonsVisible (false);
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, amber.withAlpha (0.7f));

    for (auto* c : std::initializer_list<juce::Component*> { &helpButton, &undoButton, &redoButton, &nameField, &liveEdit, &slotBox, &loadButton, &importButton,
                                                             &saveButton, &sendButton, &savedButton, &pencilButton, &lineButton, &levelButton,
                                                             &phaseButton, &resetButton, &resetAllButton, &smoothing, &snapToGrid, &positionSlider, &status, &keyboard })
        addAndMakeVisible (c);
    for (auto* b : shapeButtons)
        addAndMakeVisible (b);
    for (auto* b : opButtons)
        addAndMakeVisible (b);

    browser = std::make_unique<WavetableBrowser> (model.getConnection().getUserSettings());
    browser->onFileChosen = [this] (const juce::File& f) {
        if (f.hasFileExtension ("syx;sysex"))
            importFile (f); // the Peak's own format: straight in
        else
            askHowToLoad (f);
    };
    addAndMakeVisible (*browser);

    model.onWavetableRead = [this] (int slot, const peak::Wavetable& read) {
        if (session && session->readingOriginal && slot == session->slot)
        {
            session->original = read; // kept to put back, not loaded
            session->readingOriginal = false;
            return;
        }
        if (slot != slotBox.getSelectedId() - 1)
            return;
        beginEdit();
        table = read;
        nameField.setText (table.name, juce::dontSendNotification);
        selectedWave = 0;
        setStatus ("Read U" + juce::String (slot + 1) + " \"" + juce::String (table.name).trim() + "\" from the Peak");
        repaint();
    };
    model.changed.addChangeListener (this);
    refreshSlots();
    // Start on an empty slot (still named "User n"), else U10.
    int start = 9;
    for (int s = 0; s < peak::Wavetable::slots; ++s)
        if (juce::String (peak::userWavetableNames()[(size_t) s]).startsWith ("User"))
        {
            start = s;
            break;
        }
    slotBox.setSelectedId (start + 1, juce::dontSendNotification);
    setStatus ("Draw on the wave, or click a shape and drag across the wave to draw it (double-click a shape to fill the wave). Send to Peak plays it at once.");
    sendLookAndFeelChange();
}

WavetableEditor::~WavetableEditor()
{
    endSampleSession (false); // the Peak put back if the sample window was auditioning on it
    importOverlay.reset();    // the sample window stops its playback as it goes
    helpOverlay.reset();
    model.onWavetableRead = nullptr;
    model.changed.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

//==============================================================================
void WavetableEditor::refreshSlots()
{
    const auto selected = slotBox.getSelectedId();
    slotBox.clear (juce::dontSendNotification);
    for (int s = 0; s < peak::Wavetable::slots; ++s)
        slotBox.addItem ("U" + juce::String (s + 1) + "  " + juce::String (peak::userWavetableNames()[(size_t) s])
                             + (unsavedOnPeak[(size_t) s] ? juce::String::fromUTF8 ("  \xe2\x80\xa2 unsaved on Peak") : juce::String()),
                         s + 1);
    slotBox.setSelectedId (selected, juce::dontSendNotification);
    const auto slot = selected - 1;
    savedButton.setVisible (slot >= 0 && unsavedOnPeak[(size_t) slot]);
}

void WavetableEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshSlots(); // user wavetable names may have been read

    // An oscillator's Shape Amount turned (on the Peak or the panel): the preview follows it, if that
    // oscillator plays the selected slot (or none does). Shown only; nothing is sent back.
    const auto* touched = model.getLastTouched();
    const auto slot = slotBox.getSelectedId() - 1;
    if (touched == nullptr || slot < 0 || positionSlider.isMouseButtonDown())
        return;
    const auto param = [] (int osc, const char* name) -> const peak::ParamDef& { return *peak::findParam ("osc" + std::to_string (osc) + name); };
    const auto playing = [&] (int osc) { return model.get (param (osc, "_wave")) == 4 && model.get (param (osc, "_wave_more")) == 64 + slot; };
    for (int osc = 1; osc <= 3; ++osc)
        if (touched == &param (osc, "_manual_shape") && (playing (osc) || ! (playing (1) || playing (2) || playing (3))))
        {
            const auto shown = (double) model.get (*touched) * 4.0 / 127.0;
            if (std::abs (shown - positionSlider.getValue()) > 1.0e-3)
            {
                positionSlider.setValue (shown, juce::dontSendNotification);
                position = (float) shown;
                repaint (editorArea);
            }
        }
}

void WavetableEditor::setStatus (const juce::String& message, bool important)
{
    status.setText (message, juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, important ? amber : dim);
}

void WavetableEditor::beginEdit()
{
    undoStack.push_back (table);
    if (undoStack.size() > 100)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void WavetableEditor::edited()
{
    repaint();
    if (liveEdit.getToggleState())
        startTimer (120); // once the change settles
}

void WavetableEditor::setWave (const peak::Wave& w)
{
    table.waves[(size_t) selectedWave] = w;
    edited();
}

void WavetableEditor::undoRedo (bool undo)
{
    auto& from = undo ? undoStack : redoStack;
    auto& to = undo ? redoStack : undoStack;
    if (from.empty())
        return;
    to.push_back (table);
    table = from.back();
    from.pop_back();
    nameField.setText (table.name, juce::dontSendNotification);
    edited();
}

void WavetableEditor::selectTool (Tool t, peak::Shapes::Kind kind)
{
    tool = t;
    stampKind = kind;
    pencilButton.getProperties().set ("primary", t == Tool::pencil);
    lineButton.getProperties().set ("primary", t == Tool::line);
    for (int i = 0; i < shapeButtons.size(); ++i)
        shapeButtons[i]->getProperties().set ("primary", t == Tool::shape && i == (int) kind);
    repaint();
}

void WavetableEditor::auditionPosition()
{
    // With Live Edit on, the Peak plays the selected wave, or the position being previewed: the Wave
    // Table Position (Manual Shape) of each oscillator playing this user slot, 0-127 across waves 1-5.
    // If none is, Oscillator 1 is switched to it.
    const auto slot = slotBox.getSelectedId() - 1;
    if (! liveEdit.getToggleState() || slot < 0)
        return;
    const auto param = [] (int osc, const char* name) -> const peak::ParamDef& { return *peak::findParam ("osc" + std::to_string (osc) + name); };
    const auto playing = [&] (int osc) { return model.get (param (osc, "_wave")) == 4 && model.get (param (osc, "_wave_more")) == 64 + slot; };
    if (! playing (1) && ! playing (2) && ! playing (3))
    {
        model.set (param (1, "_wave_more"), 64 + slot);
        model.set (param (1, "_wave"), 4);
        setStatus ("Oscillator 1 now plays U" + juce::String (slot + 1));
    }
    for (int osc = 1; osc <= 3; ++osc)
        if (playing (osc))
            model.set (param (osc, "_manual_shape"), juce::roundToInt (position * 127.0f / 4.0f));
}

void WavetableEditor::timerCallback()
{
    stopTimer();
    const auto slot = slotBox.getSelectedId() - 1;
    if (! liveEdit.getToggleState() || slot < 0)
        return;
    if (model.sendUserWavetable (slot, table))
    {
        unsavedOnPeak[(size_t) slot] = true;
        refreshSlots();
    }
}

//==============================================================================
void WavetableEditor::loadFromPeak()
{
    const auto slot = slotBox.getSelectedId() - 1;
    if (slot < 0)
        return;
    setStatus (model.requestUserWavetable (slot) ? "Reading U" + juce::String (slot + 1) + juce::String::fromUTF8 (" from the Peak\xe2\x80\xa6")
                                                 : juce::String ("No MIDI output to the Peak"));
}

void WavetableEditor::sendToPeak()
{
    const auto slot = slotBox.getSelectedId() - 1;
    if (slot < 0)
        return;
    const auto name = juce::String (table.name).trim();
    const auto replacing = juce::String (peak::userWavetableNames()[(size_t) slot]);
    const auto message = "Send \"" + name + "\" to U" + juce::String (slot + 1) + ", replacing \"" + replacing + "\"?\n\n"
                         "It plays straight away. To keep it after the Peak is switched off, save it on the Peak:\n"
                         "Osc menu, page 9 \"User Waves\", select it, then that page's Save button.";
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle ("Send to Peak")
                                      .withMessage (message)
                                      .withButton (juce::MessageBoxButtonOptions ("Send to U" + juce::String (slot + 1), 1).withRole (juce::MessageBoxButtonRole::destroy))
                                      .withButton (juce::MessageBoxButtonOptions ("Cancel", 0).withRole (juce::MessageBoxButtonRole::cancel))
                                      .withAssociatedComponent (this),
                                  [this, slot, name] (int result) {
                                      if (result != 1)
                                          return;
                                      if (! model.sendUserWavetable (slot, table))
                                          return setStatus ("No MIDI output to the Peak", true);
                                      unsavedOnPeak[(size_t) slot] = true;
                                      refreshSlots();
                                      setStatus ("Sent \"" + name + "\" to U" + juce::String (slot + 1) + ". To keep it: on the Peak, Osc menu, page 9 "
                                                     "\"User Waves\", select it, Save.",
                                                 true);
                                  });
}

void WavetableEditor::saveFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Save the wavetable", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                                              .getChildFile (juce::String (table.name).trim() + ".syx"), "*.syx");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc) {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;
        juce::MemoryOutputStream out;
        for (const auto& m : table.toSysex (juce::jmax (0, slotBox.getSelectedId() - 1)))
            out.write (m.data(), m.size());
        setStatus (file.replaceWithData (out.getData(), out.getDataSize()) ? "Saved " + file.getFileName() : "Couldn't save " + file.getFileName(), false);
    });
}

void WavetableEditor::import()
{
    chooser = std::make_unique<juce::FileChooser> ("Import a wavetable", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                   "*.wav;*.mp3;*.syx;*.sysex"); // as Components imports
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
        if (const auto f = fc.getResult(); f.existsAsFile())
            f.hasFileExtension ("syx;sysex") ? importFile (f) : askHowToLoad (f);
    });
}

void WavetableEditor::importFile (const juce::File& file)
{
    if (file.hasFileExtension ("syx;sysex"))
    {
        juce::MemoryBlock data;
        file.loadFileAsData (data);
        const auto* bytes = static_cast<const uint8_t*> (data.getData());
        if (const auto t = peak::Wavetable::fromSysex (peak::splitSysex (bytes, data.getSize())))
        {
            beginEdit();
            table = *t;
            if (! peak::hasWavetableName (peak::splitSysex (bytes, data.getSize())))
                table.name = file.getFileNameWithoutExtension().substring (0, peak::Wavetable::nameLength).toStdString();
            nameField.setText (table.name, juce::dontSendNotification);
            edited();
            return setStatus ("Loaded \"" + juce::String (table.name).trim() + "\" from " + file.getFileName());
        }
        return setStatus (file.getFileName() + " isn't a Peak wavetable (.syx with 5 waves)", true);
    }

    // Audio as a wavetable: 2048-sample frames, as Components' 3rd Party Wavetable import. One frame goes
    // into the selected wave; 2-4 fill waves 1, 2, ... with the rest left flat; more are spread over all
    // five. Shorter than a frame: a single cycle, into the selected wave.
    const auto audio = readMono (file, 60.0);
    if (! audio || audio->samples->getNumSamples() < 16)
        return setStatus ("Couldn't read " + file.getFileName(), true);
    const auto length = audio->samples->getNumSamples();
    const auto* s = audio->samples->getReadPointer (0);
    const auto cycle = [&] (int start, int count) {
        return peak::WaveOps::normalise (peak::Wave::fromSamples (std::vector<float> (s + start, s + start + count)));
    };
    beginEdit();
    constexpr int frame = 2048;
    const int frames = length / frame;
    if (frames <= 1)
    {
        table.waves[(size_t) selectedWave] = cycle (0, juce::jmin (length, frame));
        setStatus ("Loaded " + juce::String (frames == 0 ? "a single cycle" : "a frame") + " into wave " + juce::String (selectedWave + 1));
    }
    else
    {
        // 2-4 frames fill waves 1, 2, ... in order and the rest are flat; more are spread over all five.
        const int waves = juce::jmin (frames, peak::Wavetable::waveCount);
        for (int w = 0; w < peak::Wavetable::waveCount; ++w)
            table.waves[(size_t) w] = w < waves ? cycle ((frames > waves ? juce::roundToInt ((double) w * (frames - 1) / (waves - 1)) : w) * frame, frame)
                                                : peak::WaveOps::clear();
        setStatus (frames < peak::Wavetable::waveCount ? "Loaded the " + juce::String (frames) + " waves of " + file.getFileName() + " (waves "
                                                             + juce::String (frames + 1) + "-5 left flat)"
                                                       : "Loaded " + juce::String (waves) + " of " + juce::String (frames) + " frames of " + file.getFileName());
    }
    table.name = file.getFileNameWithoutExtension().substring (0, peak::Wavetable::nameLength).toStdString();
    nameField.setText (table.name, juce::dontSendNotification);
    edited(); // with Live Edit on, it goes to the Peak to be heard
}

//==============================================================================
juce::Rectangle<int> WavetableEditor::thumbBounds (int wave) const
{
    const int gap = 10, w = (thumbs.getWidth() - 4 * gap) / 5;
    return { thumbs.getX() + wave * (w + gap), thumbs.getY(), w, thumbs.getHeight() };
}

int WavetableEditor::harmonicAt (juce::Point<int> p) const
{
    if (! harmonicsArea.contains (p))
        return 0;
    const auto w = (float) (harmonicsArea.getWidth() - 8) / harmonicBars;
    return juce::jlimit (1, harmonicBars, 1 + (int) ((float) (p.x - harmonicsArea.getX() - 4) / w));
}

void WavetableEditor::showOverlay (std::unique_ptr<juce::Component> card, std::function<void()> onClosed)
{
    importOverlay = std::make_unique<CardStyle::Overlay> (std::move (card), [this, onClosed] {
        if (onClosed)
            onClosed();
        importOverlay.reset();
    });
    addAndMakeVisible (*importOverlay);
    importOverlay->setBounds (getLocalBounds());
    importOverlay->getCard().grabKeyboardFocus();
}

void WavetableEditor::askHowToLoad (const juce::File& file)
{
    auto card = std::make_unique<LoadChoiceCard> (file.getFileNameWithoutExtension());
    card->onChoice = [this, file] (bool asAudio) {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<WavetableEditor> (this), file, asAudio] {
            if (safe == nullptr)
                return;
            safe->importOverlay.reset();
            asAudio ? safe->openSample (file) : safe->importFile (file);
        });
    };
    card->onCancel = [this] { if (importOverlay) importOverlay->close(); };
    showOverlay (std::move (card));
}

void WavetableEditor::openSample (const juce::File& file, double startSeconds)
{
    const auto audio = readMono (file, 600.0); // up to 10 minutes
    if (! audio || audio->samples->getNumSamples() < 2048)
        return setStatus ("Couldn't read " + file.getFileName() + " as audio (it needs at least 2048 samples)", true);
    const auto mono = audio->samples;
    const auto rate = audio->rate;
    auto card = std::make_unique<AudioSampleImporter> (file.getFileName(), mono, rate);
    card->onPlay = [this, mono, rate] (bool play, int from) {
        play ? samplePreview->play (mono, rate, from) : samplePreview->stop();
    };
    card->playPosition = [this] { return samplePreview->getPosition(); };
    // The Peak's slot as it is now, to put back if nothing is imported.
    session.emplace();
    session->slot = slotBox.getSelectedId() - 1;
    session->openedMs = juce::Time::getMillisecondCounterHiRes();
    session->readingOriginal = session->slot >= 0 && model.requestUserWavetable (session->slot);

    card->onAudition = [this] (const peak::Wave& wave, bool onPeak) {
        // The cycle looped at C3 (as Components auditions a position) for a moment; while scrubbing,
        // each new cycle blends into the last, so nothing clicks.
        const auto samples = wave.samples (SamplePreview::cycleLength);
        SamplePreview::Cycle cycle;
        std::copy (samples.begin(), samples.end(), cycle.begin());
        samplePreview->playCycle (cycle, 130.81, 0.35);
        if (onPeak && session && session->slot >= 0)
        {
            session->pending = wave;
            if (! peakAudition.isTimerRunning())
                peakAudition.startTimer (session->peakUsed ? 150 : 10); // while scrubbing, at most every 150 ms
        }
    };
    card->onPeakOff = [this] { restorePeak(); };
    // The audition volume, kept between sessions.
    if (auto* settings = model.getConnection().getUserSettings())
        samplePreview->setVolume ((float) settings->getDoubleValue ("auditionVolume", 0.6));
    card->setVolume (samplePreview->getVolume());
    card->onVolume = [this] (float v) {
        samplePreview->setVolume (v);
        if (auto* settings = model.getConnection().getUserSettings())
            settings->setValue ("auditionVolume", v);
    };
    card->setStart ((int) (startSeconds * rate));
    card->onClose = [this] { if (importOverlay) importOverlay->close(); };
    card->onImport = [this] (std::vector<peak::Wave> waves, const juce::String& from) {
        juce::ScopeGuard end { [this] { endSampleSession (true); } }; // after the table has changed
        beginEdit();
        if (waves.size() == 1)
            table.waves[(size_t) selectedWave] = waves.front();
        else
            for (size_t w = 0; w < waves.size() && w < table.waves.size(); ++w)
                table.waves[w] = waves[w];
        setStatus (waves.size() == 1 ? "Imported a waveform from " + from + " into wave " + juce::String (selectedWave + 1)
                                     : "Imported a wavetable (5 waves) from " + from);
        edited(); // with Live Edit on, it goes to the Peak to be heard
        if (importOverlay)
            importOverlay->close();
    };
    showOverlay (std::move (card), [this] { endSampleSession (false); }); // closed without importing (if not already ended)
}

void WavetableEditor::sendPeakAudition()
{
    peakAudition.stopTimer();
    if (! session || ! session->pending)
        return;
    // Not before the slot as it was has been read (to put it back), unless the Peak doesn't answer.
    if (session->readingOriginal && juce::Time::getMillisecondCounterHiRes() - session->openedMs < 1500.0)
        return peakAudition.startTimer (50);
    session->readingOriginal = false;

    if (! session->peakUsed)
    {
        // An oscillator must be playing the slot: as Live Edit's audition, Oscillator 1 is switched to it
        // if none is. What it was is kept to put back.
        const auto param = [] (int osc, const char* name) -> const peak::ParamDef& { return *peak::findParam ("osc" + std::to_string (osc) + name); };
        for (int osc = 1; osc <= 3; ++osc)
            for (const auto* name : { "_wave", "_wave_more" })
                session->oscillators.emplace_back (&param (osc, name), model.get (param (osc, name)));
        const auto playing = [&] (int osc) { return model.get (param (osc, "_wave")) == 4 && model.get (param (osc, "_wave_more")) == 64 + session->slot; };
        if (! playing (1) && ! playing (2) && ! playing (3))
        {
            model.set (param (1, "_wave_more"), 64 + session->slot);
            model.set (param (1, "_wave"), 4);
        }
        session->peakUsed = true;
    }
    peak::Wavetable audition;
    audition.name = session->original ? session->original->name : std::string (peak::userWavetableNames()[(size_t) session->slot]);
    audition.waves.fill (*session->pending);
    session->pending.reset();
    if (model.sendUserWavetable (session->slot, audition))
        setStatus ("Auditioning on the Peak (U" + juce::String (session->slot + 1) + "): play it from your keyboard");
}

void WavetableEditor::restorePeak()
{
    peakAudition.stopTimer();
    if (! session || ! session->peakUsed)
        return;
    // The slot as read when the window opened (or, if the Peak didn't answer, the editor's wavetable).
    model.sendUserWavetable (session->slot, session->original ? *session->original : table);
    for (const auto& [def, value] : session->oscillators)
        if (model.get (*def) != value)
            model.set (*def, value);
    session->oscillators.clear();
    session->pending.reset();
    session->peakUsed = false;
    setStatus ("The Peak is back as it was");
}

void WavetableEditor::endSampleSession (bool imported)
{
    if (! session)
        return;
    peakAudition.stopTimer();
    samplePreview->stop();
    if (imported && session->peakUsed)
    {
        // The Peak gets what was imported, not the audition (its oscillators stay on the slot).
        if (model.sendUserWavetable (session->slot, table))
            unsavedOnPeak[(size_t) session->slot] = true;
        refreshSlots();
    }
    else if (! imported)
        restorePeak();
    session.reset();
}

void WavetableEditor::browse (const juce::File& folder, const juce::File& file)
{
    browser->addFolder (folder, false); // not kept: e.g. a snapshot's folder
    browser->openFolder (folder);
    if (file.existsAsFile())
        browser->select (file);
    if (file.hasFileExtension ("syx;sysex"))
        importFile (file); // as a double-click would
}

void WavetableEditor::browseHome (const juce::Array<juce::File>& folders)
{
    for (const auto& f : folders)
        browser->addFolder (f, false);
}

void WavetableEditor::showHelp()
{
    auto card = std::make_unique<CardStyle::HelpCard> ("Wavetable editor", std::vector<CardStyle::HelpCard::Section> {
        { "Getting started", { "Choose a user slot (U1 to U10). Load from Peak reads what's in it, or start from a shape.",
                               "A wavetable is 5 waves. The Peak morphs through them as Wave Table Position moves. Click a wave to edit it; "
                               "drag it onto another wave to swap the two." } },
        { "Drawing", { "Pencil draws freehand. Smoothing softens pencil drawing; at 0% it stays as drawn.",
                       "Line draws a straight line where you drag.",
                       "Click a shape, then drag across the wave to draw one cycle of it in the box you drag. Start on the centre line "
                       "to keep it in one half; drag from top to bottom to use the full height.",
                       "Double-click a shape to fill the whole wave with it.",
                       "Snap to grid makes lines and shapes snap to the grid: 16 steps across, quarters up and down." } },
        { "Harmonics", { "The bars are harmonics 1 to 64 of the selected wave. Drag a bar to change its level, or switch to Phase to turn it." } },
        { "Wave tools", { "Copy to all waves; Blend 1 to 5 makes waves 2, 3 and 4 morph between waves 1 and 5; Normalise; Reverse; Invert.",
                          "Reset wave makes the selected wave a flat line; Reset all does all five.",
                          "Undo and Redo: Cmd+Z and Shift+Cmd+Z." } },
        { "Hearing it", { "Send to Peak sends the wavetable to the selected slot, and it plays at once. Play the keyboard below to hear it.",
                          "Live Edit sends every change as you make it. Clicking a wave, or moving Wave Table Position, moves the Peak "
                          "there too (if no oscillator is playing the slot, Oscillator 1 is switched to it)." } },
        { "Keeping it on the Peak", { "A sent wavetable is lost when the Peak is switched off, unless you save it on the Peak: Osc menu, "
                                      "page 9 \"User Waves\", select it, then that page's Save.",
                                      "The editor can't tell when you've saved, so the slot is marked \"unsaved on Peak\" until you click "
                                      "\"I've saved it on the Peak\"." } },
        { "Files", { "File Browser: the folder button adds a folder (chosen in Finder) to the Home list; click one to browse it. Back, "
                     "Forward and Up move around, Home returns to the list, and right-clicking a folder there removes it. Double-click a file to load it.",
                     "Files are those Components imports: .syx/.sysex wavetables load at once; for a WAV or MP3 choose Wavetable "
                     "(a single cycle, or 2048-sample frames such as a Serum wavetable; with fewer than 5 frames, the rest are left flat) or Audio file.",
                     "Audio file: click the waveform to hear the cycle at that point (drag to scrub; Play plays the sample), then Import Waveform (one cycle into the selected wave) or Import "
                     "Wavetable (five successive cycles into all five waves). The sample plays through the editor's audio output.",
                     "Click one of the five waves under the timeline to hear it and choose it for Import Waveform. Play on the Peak "
                     "sends what you audition to the selected slot, to play from your keyboard; closing without importing puts the "
                     "Peak back as it was.",
                     "Import opens a single file from anywhere, the same way. Save writes a .syx file, which Components can load too." } } }, 720);
    helpOverlay = std::make_unique<CardStyle::Overlay> (std::move (card), [this] { helpOverlay.reset(); });
    addAndMakeVisible (*helpOverlay);
    helpOverlay->setBounds (getLocalBounds());
    helpOverlay->grabKeyboardFocus();
}

void WavetableEditor::resized()
{
    if (helpOverlay != nullptr)
        helpOverlay->setBounds (getLocalBounds());
    if (importOverlay != nullptr)
        importOverlay->setBounds (getLocalBounds());
    // The header (as every tab's): ? and the title, over the pinstripe.
    helpButton.setBounds (12, 10, 20, 20);
    auto r = getLocalBounds().withTrimmedTop (36).reduced (16, 8);
    auto top = r.removeFromTop (topBar - 8).withSizeKeepingCentre (r.getWidth(), 30);
    undoButton.setBounds (top.removeFromLeft (34));
    top.removeFromLeft (4);
    redoButton.setBounds (top.removeFromLeft (34));
    top.removeFromLeft (12);
    nameField.setBounds (top.removeFromLeft (180));
    top.removeFromLeft (16);
    liveEdit.setBounds (top.removeFromLeft (110));
    top.removeFromLeft (8);
    slotBox.setBounds (top.removeFromLeft (250));
    for (auto* b : { &sendButton, &saveButton, &importButton, &loadButton })
    {
        b->setBounds (top.removeFromRight (b == &loadButton ? 150 : 128));
        top.removeFromRight (10);
    }
    r.removeFromTop (8);

    right = r.removeFromRight (rightWidth);
    r.removeFromRight (16);
    left = r;

    // Left card: title, the 5 waves, the drawing area, position, harmonics, keyboard.
    auto l = left.reduced (pad, 0);
    auto titleRow = l.removeFromTop (44);
    resetAllButton.setBounds (titleRow.removeFromRight (110).withSizeKeepingCentre (110, 26));
    titleRow.removeFromRight (8);
    resetButton.setBounds (titleRow.removeFromRight (110).withSizeKeepingCentre (110, 26));
    thumbs = l.removeFromTop (64);
    l.removeFromTop (22);
    keyboard.setBounds (l.removeFromBottom (78));
    keyboard.setKeyWidth ((float) keyboard.getWidth() / 36.0f); // C3-C8: 36 white keys
    l.removeFromBottom (10);
    auto toggles = l.removeFromBottom (26);
    levelButton.setBounds (toggles.removeFromLeft (72));
    toggles.removeFromLeft (4);
    phaseButton.setBounds (toggles.removeFromLeft (72));
    l.removeFromBottom (4);
    harmonicsArea = l.removeFromBottom (juce::jlimit (90, 160, l.getHeight() / 3));
    l.removeFromBottom (8);
    positionRow = l.removeFromBottom (30);
    positionSlider.setBounds (positionRow.withTrimmedLeft (230));
    l.removeFromBottom (6);
    editorArea = l;

    // Right card: Draw (Pencil, Line) beside the Wave buttons, Shapes, Tool Settings, File Browser, status.
    auto rr = right.reduced (pad, 0);
    rr.removeFromTop (44);
    auto tools = rr.removeFromTop (102);
    pencilButton.setBounds (tools.getX(), tools.getY(), 44, 44);
    lineButton.setBounds (tools.getX() + 52, tools.getY(), 44, 44);
    tools.removeFromLeft (96 + 18);
    const int buttonWidth = (tools.getWidth() - 6) / 2;
    for (int i = 0; i < 4; ++i)
        opButtons[i]->setBounds (tools.getX() + (i % 2) * (buttonWidth + 6), tools.getY() + (i / 2) * 36, buttonWidth, 30);
    opButtons[4]->setBounds (tools.getX(), tools.getY() + 72, tools.getWidth(), 30);
    rr.removeFromTop (44);
    for (int i = 0; i < 6; ++i)
        shapeButtons[i]->setBounds (rr.getX() + (i % 3) * ((rr.getWidth() + 8) / 3), rr.getY() + (i / 3) * 40, (rr.getWidth() - 16) / 3, 32);
    rr.removeFromTop (72 + 44);
    smoothing.setBounds (rr.removeFromTop (28).withTrimmedLeft (90));
    rr.removeFromTop (2);
    snapToGrid.setBounds (rr.removeFromTop (26).withWidth (200));
    rr.removeFromTop (44);
    rr.removeFromBottom (10);
    savedButton.setBounds (rr.removeFromBottom (30));
    rr.removeFromBottom (6);
    status.setBounds (rr.removeFromBottom (48));
    rr.removeFromBottom (10);
    browser->setBounds (rr);
}

void WavetableEditor::paint (juce::Graphics& g)
{
    CardStyle::paintTabHeader (g, getLocalBounds(), "Wavetables", 40);
    for (auto card : { left, right })
    {
        g.setColour (CardStyle::card);
        g.fillRoundedRectangle (card.toFloat(), 10.0f);
        g.setColour (border);
        g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 10.0f, 1.0f);
    }

    const auto heading = [&] (juce::Rectangle<int> r, const juce::String& t) {
        g.setColour (blue);
        g.setFont (CardStyle::headingFont (11.5f));
        g.drawText (t, r, juce::Justification::centredLeft);
        g.setColour (line);
        g.fillRect (r.getX(), r.getBottom(), r.getWidth(), 1);
    };
    g.setColour (blue);
    g.setFont (CardStyle::headingFont (15.0f));
    g.drawText (juce::String::fromUTF8 ("WAVETABLE  \xc2\xb7  5 WAVES"), left.reduced (pad, 0).removeFromTop (44), juce::Justification::centredLeft);

    // The 5 waves.
    for (int w = 0; w < 5; ++w)
    {
        const auto b = thumbBounds (w).toFloat();
        const bool sel = w == selectedWave;
        g.setColour (juce::Colour (0xff141517));
        g.fillRoundedRectangle (b, 5.0f);
        g.setColour (sel ? amber : line);
        g.drawRoundedRectangle (b.reduced (sel ? 1.0f : 0.5f), 5.0f, sel ? 2.0f : 1.0f);
        drawWave (g, b.reduced (6.0f, 6.0f), table.waves[(size_t) w].samples(), sel ? amber : dim, sel ? 1.8f : 1.2f, false);
        g.setColour (sel ? amber : dim);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText ("Wave " + juce::String (w + 1), b.translated (0.0f, b.getHeight() + 2.0f).withHeight (16.0f).toNearestInt(), juce::Justification::centredLeft);
    }
    // A wave being dragged: where it would land (outlined) and the wave itself under the pointer.
    if (draggingWave >= 0 && dragPoint.getDistanceFrom (thumbBounds (draggingWave).getCentre()) > 8)
    {
        if (const auto to = thumbAt (dragPoint); to >= 0 && to != draggingWave)
        {
            juce::Path outline;
            outline.addRoundedRectangle (thumbBounds (to).toFloat().expanded (2.0f), 6.0f);
            const float dashes[] { 5.0f, 3.0f };
            juce::PathStrokeType (1.6f).createDashedStroke (outline, outline, dashes, 2);
            g.setColour (amber);
            g.fillPath (outline);
        }
        const auto ghost = thumbBounds (draggingWave).toFloat().withCentre (dragPoint.toFloat());
        g.setColour (juce::Colour (0xff141517).withAlpha (0.85f));
        g.fillRoundedRectangle (ghost, 5.0f);
        g.setColour (amber.withAlpha (0.7f));
        g.drawRoundedRectangle (ghost.reduced (1.0f), 5.0f, 1.5f);
        drawWave (g, ghost.reduced (6.0f, 6.0f), table.waves[(size_t) draggingWave].samples(), amber.withAlpha (0.8f), 1.6f, false);
    }

    // The selected wave, large, on a grid; while the position preview is away from it, the morph as a faint line.
    const auto e = editorArea.toFloat();
    g.setColour (juce::Colour (0xff101113));
    g.fillRect (e);
    // The grid (what drawing snaps to): 1/16 of the cycle across, quarters of full scale up and down.
    const auto plot = e.reduced (2.0f, 0.0f);
    for (int i = 1; i < 16; ++i)
    {
        g.setColour (juce::Colour (i % 2 ? 0xff1a1b1e : 0xff232529));
        g.drawVerticalLine (juce::roundToInt (plot.getX() + plot.getWidth() * (float) i / 16.0f), e.getY(), e.getBottom());
    }
    g.setColour (juce::Colour (0xff232529));
    for (int k = -3; k <= 3; ++k)
        g.drawHorizontalLine (juce::roundToInt (plot.getCentreY() - plot.getHeight() * editorScale * (float) k / 4.0f), e.getX(), e.getRight());
    g.setColour (line.brighter (0.2f));
    g.drawHorizontalLine (juce::roundToInt (e.getCentreY()), e.getX(), e.getRight());
    g.setColour (line);
    g.drawRect (e);
    const auto lo = (int) std::floor (position), hi = juce::jmin (4, lo + 1);
    {
        const juce::Graphics::ScopedSaveState clip (g); // anything past full scale is cut off at the edges
        g.reduceClipRegion (editorArea);
        if (std::abs (position - (float) selectedWave) > 0.01f)
            drawWave (g, plot, peak::WaveOps::blend (table.waves[(size_t) lo], table.waves[(size_t) hi], position - (float) lo).samples(),
                      blue.withAlpha (0.6f), 1.2f, false, editorScale);
        drawWave (g, plot, drawing.empty() ? table.waves[(size_t) selectedWave].samples() : drawing, amber, 2.6f, true, editorScale);
    }

    // Position preview row.
    g.setColour (text);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("Wave Table Position (preview)", positionRow.withWidth (220), juce::Justification::centredLeft);

    // Harmonics 1-64: level or phase.
    const auto hb = harmonicsArea.toFloat();
    g.setColour (juce::Colour (0xff101113));
    g.fillRect (hb);
    g.setColour (line);
    g.drawRect (hb);
    const auto& wave = table.waves[(size_t) selectedWave];
    const auto bw = (hb.getWidth() - 8.0f) / harmonicBars;
    for (int h = 1; h <= harmonicBars; ++h)
    {
        const auto x = hb.getX() + 4.0f + (float) (h - 1) * bw;
        const auto v = phaseView ? (wave.level (h) > 1.0e-4f ? (wave.phase (h) / juce::MathConstants<float>::pi + 1.0f) * 0.5f : 0.0f)
                                 : juce::jlimit (0.0f, 1.0f, wave.level (h));
        const auto barH = v * (hb.getHeight() - 8.0f);
        g.setColour (v > 0.002f ? amber : juce::Colour (0xff2a2c30));
        g.fillRect (juce::Rectangle<float> (x + 1.0f, hb.getBottom() - 4.0f - juce::jmax (2.0f, barH), juce::jmax (1.0f, bw - 2.0f), juce::jmax (2.0f, barH)));
    }
    g.setColour (dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (phaseView ? "Phase of harmonics 1-64: drag a bar to turn it" : "Harmonics 1-64: drag a bar to change it; the wave updates",
                juce::Rectangle<int> (phaseButton.getRight() + 14, phaseButton.getY(), 500, phaseButton.getHeight()), juce::Justification::centredLeft);

    // Right card headings.
    auto rr = right.reduced (pad, 0);
    heading (rr.removeFromTop (40).withTrimmedTop (14).withHeight (22).withWidth (96), "DRAW");
    heading (opButtons[0]->getBounds().withY (rr.getY() - 26).withHeight (22).withRight (opButtons[1]->getRight()), "WAVE");
    g.setColour (dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Pencil", pencilButton.getBounds().translated (0, pencilButton.getHeight() + 2).withHeight (16).expanded (10, 0), juce::Justification::centred);
    g.drawText ("Line", lineButton.getBounds().translated (0, lineButton.getHeight() + 2).withHeight (16).expanded (10, 0), juce::Justification::centred);
    heading (shapeButtons[0]->getBounds().translated (0, -38).withHeight (22).withWidth (rr.getWidth()), "SHAPES");
    heading (smoothing.getBounds().translated (-90, -36).withHeight (22).withWidth (rr.getWidth()), "TOOL SETTINGS");
    g.setColour (text);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("Smoothing", smoothing.getBounds().translated (-90, 0).withWidth (88), juce::Justification::centredLeft);
    heading (browser->getBounds().translated (0, -36).withHeight (22), "FILE BROWSER");
}


//==============================================================================
void WavetableEditor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto p = e.getPosition();
    if (const auto w = thumbAt (p); w >= 0)
    {
        selectedWave = w;
        positionSlider.setValue (w, juce::dontSendNotification);
        position = (float) w;
        auditionPosition();
        draggingWave = w; // dragged onto another wave, they swap places
        dragPoint = p;
        repaint();
        return;
    }
    if (editorArea.contains (p))
    {
        beginEdit();
        drawing = table.waves[(size_t) selectedWave].samples (peak::Wave::samplesPerCycle);
        drawBase = drawing;
        lastPoint = lineStart = snapped (e.position);
        mouseDrag (e);
        return;
    }
    if (harmonicAt (p) > 0)
    {
        beginEdit();
        draggingHarmonics = true;
        mouseDrag (e);
    }
}

int WavetableEditor::thumbAt (juce::Point<int> p) const
{
    for (int w = 0; w < peak::Wavetable::waveCount; ++w)
        if (thumbBounds (w).expanded (4, 8).contains (p))
            return w;
    return -1;
}

void WavetableEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingWave >= 0)
    {
        dragPoint = e.getPosition();
        if (e.getDistanceFromDragStart() > 4)
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        repaint (thumbs.expanded (40, 30));
        return;
    }
    const auto e2 = editorArea.toFloat().reduced (2.0f, 0.0f);
    const auto toIndex = [&] (float x) { return juce::jlimit (0, (int) drawing.size() - 1, (int) ((x - e2.getX()) / e2.getWidth() * (float) drawing.size())); };
    const auto toValue = [&] (float y) { return juce::jlimit (-1.0f, 1.0f, (e2.getCentreY() - y) / (e2.getHeight() * editorScale)); };

    if (! drawing.empty() && tool == Tool::shape)
    {
        // One cycle of the shape filling the dragged box: across from start to end, and from the lower to the
        // higher of the two heights (start on the line and drag up: the top half only; drag from top to bottom: all of it).
        drawing = drawBase;
        const auto to = snapped (e.position);
        const auto x0 = juce::jmin (lineStart.x, to.x), x1 = juce::jmax (lineStart.x, to.x);
        const auto top = juce::jmax (toValue (lineStart.y), toValue (to.y)), bottom = juce::jmin (toValue (lineStart.y), toValue (to.y));
        const auto n = (float) drawing.size();
        const auto p0 = (x0 - e2.getX()) / e2.getWidth(), p1 = (x1 - e2.getX()) / e2.getWidth();
        unsigned seed = 7;
        for (size_t i = 0; i < drawing.size(); ++i)
            if (const auto p = (float) i / n; p >= p0 && p < p1)
                drawing[i] = bottom + (top - bottom) * (peak::Shapes::at (stampKind, (p - p0) / (p1 - p0), seed) + 1.0f) * 0.5f;
        repaint (editorArea);
        return;
    }
    if (! drawing.empty())
    {
        const auto to = tool == Tool::line ? snapped (e.position) : e.position;
        const auto from = tool == Tool::line ? lineStart : lastPoint;
        if (tool == Tool::line)
            drawing = drawBase;
        const auto a = toIndex (from.x), b = toIndex (to.x);
        const auto va = toValue (from.y), vb = toValue (to.y);
        for (int i = juce::jmin (a, b); i <= juce::jmax (a, b); ++i)
            drawing[(size_t) i] = a == b ? vb : va + (vb - va) * (float) (i - a) / (float) (b - a);
        lastPoint = e.position;
        repaint (editorArea);
        return;
    }
    if (draggingHarmonics)
    {
        const auto h = harmonicAt ({ juce::jlimit (harmonicsArea.getX() + 5, harmonicsArea.getRight() - 5, e.x), harmonicsArea.getCentreY() });
        const auto v = juce::jlimit (0.0f, 1.0f, (float) (harmonicsArea.getBottom() - 4 - e.y) / (float) (harmonicsArea.getHeight() - 8));
        auto& w = table.waves[(size_t) selectedWave];
        if (phaseView)
            w.setPhase (h, (v * 2.0f - 1.0f) * juce::MathConstants<float>::pi);
        else
            w.setLevel (h, v);
        repaint();
    }
}

void WavetableEditor::mouseUp (const juce::MouseEvent& e)
{
    if (const auto from = std::exchange (draggingWave, -1); from >= 0)
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
        // Dropped on another wave: the two swap places (one undo step), and the dragged one stays selected.
        if (const auto to = thumbAt (e.getPosition()); to >= 0 && to != from && e.getDistanceFromDragStart() > 4)
        {
            beginEdit();
            std::swap (table.waves[(size_t) from], table.waves[(size_t) to]);
            selectedWave = to;
            positionSlider.setValue (to, juce::dontSendNotification);
            position = (float) to;
            setStatus ("Wave " + juce::String (from + 1) + " and wave " + juce::String (to + 1) + " swapped places");
            edited();
            auditionPosition();
        }
        repaint();
        return;
    }
    if (! drawing.empty())
    {
        // The drawing becomes harmonics (pencil drawing smoothed as set), which is what the Peak plays. Without
        // smoothing it's kept exactly as drawn; it's only scaled down when sent, if it goes past full scale.
        table.waves[(size_t) selectedWave] = peak::WaveOps::smooth (peak::Wave::fromSamples (drawing), tool == Tool::pencil ? (float) smoothing.getValue() / 100.0f : 0.0f);
        drawing.clear();
        edited();
    }
    if (std::exchange (draggingHarmonics, false))
        edited();
}

juce::Point<float> WavetableEditor::snapped (juce::Point<float> p) const
{
    if (! snapToGrid.getToggleState() || tool == Tool::pencil)
        return p;
    const auto e2 = editorArea.toFloat().reduced (2.0f, 0.0f);
    const auto amp = e2.getHeight() * editorScale;
    const auto column = juce::jlimit (0.0f, 16.0f, std::round ((p.x - e2.getX()) / e2.getWidth() * 16.0f));
    const auto level = juce::jlimit (-4.0f, 4.0f, std::round ((e2.getCentreY() - p.y) / amp * 4.0f));
    return { e2.getX() + e2.getWidth() * column / 16.0f, e2.getCentreY() - amp * level / 4.0f };
}

bool WavetableEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
        return undoRedo (true), true;
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
        return undoRedo (false), true;
    return false;
}
