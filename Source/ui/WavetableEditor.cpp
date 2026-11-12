#include "WavetableEditor.h"

#include "peak/core/ParamFormat.h"
#include "ui/CardStyle.h"

namespace
{
using namespace CardStyle;

constexpr int topBar = 46, pad = 18, rightWidth = 360, harmonicBars = 64;
const char* const shapeNames[] { "Sine", "Square", "Pulse", "Saw", "Triangle", "Noise" };
const char* const opNames[] { "Copy to all waves", "Blend 1 \xe2\x86\x92 5 (morph)", "Normalise", "Reverse", "Invert" };
constexpr int opCount = 5;

// A shape button: a click picks the shape to draw with, a double-click fills the wave with it.
struct ShapeButton final : juce::TextButton
{
    std::function<void()> onDoubleClick;
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (onDoubleClick)
            onDoubleClick();
    }
};

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
    importButton.setTooltip ("A WAV file (a single cycle, a Serum-style wavetable, or any sample cut into 5 waves), or a .syx wavetable");
    saveButton.setTooltip ("Save as a .syx file (Components can load it)");
    sendButton.setTooltip ("Send to the selected user slot. It plays at once; to keep it, save it on the Peak");
    sendButton.getProperties().set ("primary", true);
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
        auto* b = new ShapeButton(); // name and picture drawn in paintOverChildren
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

    model.onWavetableRead = [this] (int slot, const peak::Wavetable& read) {
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
                                                   "*.wav;*.aif;*.aiff;*.syx");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
        const auto file = fc.getResult();
        if (! file.existsAsFile())
            return;
        if (file.hasFileExtension ("syx"))
        {
            juce::MemoryBlock data;
            file.loadFileAsData (data);
            const auto* bytes = static_cast<const uint8_t*> (data.getData());
            if (const auto t = peak::Wavetable::fromSysex (peak::splitSysex (bytes, data.getSize())))
            {
                beginEdit();
                table = *t;
                nameField.setText (table.name, juce::dontSendNotification);
                edited();
                return setStatus ("Imported \"" + juce::String (table.name).trim() + "\" from " + file.getFileName());
            }
            return setStatus (file.getFileName() + " isn't a Peak wavetable (.syx with 5 waves)", true);
        }

        // Audio: a single cycle goes into the selected wave; longer audio fills all 5 (Serum-style
        // 2048-sample frames are taken as frames; anything else is cut into 5 parts).
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->lengthInSamples < 16)
            return setStatus ("Couldn't read " + file.getFileName(), true);
        const auto length = (int) juce::jmin<juce::int64> (reader->lengthInSamples, 1 << 20);
        juce::AudioBuffer<float> buffer (1, length);
        reader->read (&buffer, 0, length, 0, true, false);
        const auto* s = buffer.getReadPointer (0);
        const auto cycle = [&] (int start, int count) {
            return peak::WaveOps::normalise (peak::Wave::fromSamples (std::vector<float> (s + start, s + start + count)));
        };
        beginEdit();
        if (length <= 4096)
        {
            table.waves[(size_t) selectedWave] = cycle (0, length);
            setStatus ("Imported a single cycle into wave " + juce::String (selectedWave + 1));
        }
        else
        {
            const int frame = length % 2048 == 0 && length / 2048 >= 5 ? 2048 : juce::jmin (4096, length / 5);
            const int frames = length / frame;
            for (int w = 0; w < 5; ++w)
                table.waves[(size_t) w] = cycle (juce::jmin (frames - 1, w * (frames - 1) / 4) * frame, frame);
            setStatus ("Imported " + juce::String (frame == 2048 && length % 2048 == 0 ? "5 of " + juce::String (frames) + " frames" : juce::String ("5 parts"))
                       + " of " + file.getFileName());
        }
        table.name = file.getFileNameWithoutExtension().substring (0, peak::Wavetable::nameLength).toStdString();
        nameField.setText (table.name, juce::dontSendNotification);
        edited();
    });
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

void WavetableEditor::showHelp()
{
    auto card = std::make_unique<CardStyle::HelpCard> ("Wavetable editor", std::vector<CardStyle::HelpCard::Section> {
        { "Getting started", { "Choose a user slot (U1 to U10). Load from Peak reads what's in it, or start from a shape.",
                               "A wavetable is 5 waves. The Peak morphs through them as Wave Table Position moves. Click a wave to edit it." } },
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
        { "Files", { "Import a WAV or AIFF (a single cycle goes into the selected wave; a Serum-style wavetable or a longer sample fills "
                     "all five) or a .syx wavetable.",
                     "Save writes a .syx file, which Components can load too." } } }, 720);
    helpOverlay = std::make_unique<CardStyle::Overlay> (std::move (card), [this] { helpOverlay.reset(); });
    addAndMakeVisible (*helpOverlay);
    helpOverlay->setBounds (getLocalBounds());
    helpOverlay->grabKeyboardFocus();
}

void WavetableEditor::resized()
{
    if (helpOverlay != nullptr)
        helpOverlay->setBounds (getLocalBounds());
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
    positionSlider.setBounds (positionRow.withTrimmedLeft (230).withTrimmedRight (90));
    l.removeFromBottom (6);
    editorArea = l;

    // Right card: tools, shapes, settings, wave operations, status.
    auto rr = right.reduced (pad, 0);
    rr.removeFromTop (44);
    auto tools = rr.removeFromTop (44);
    pencilButton.setBounds (tools.removeFromLeft (44));
    tools.removeFromLeft (8);
    lineButton.setBounds (tools.removeFromLeft (44));
    rr.removeFromTop (22 + 40);
    for (int i = 0; i < 6; ++i)
        shapeButtons[i]->setBounds (rr.getX() + (i % 3) * ((rr.getWidth() + 8) / 3), rr.getY() + (i / 3) * 40, (rr.getWidth() - 16) / 3, 32);
    rr.removeFromTop (80 + 44);
    smoothing.setBounds (rr.removeFromTop (28).withTrimmedLeft (90));
    rr.removeFromTop (4);
    snapToGrid.setBounds (rr.removeFromTop (26).withWidth (200));
    rr.removeFromTop (40);
    for (int i = 0; i < opCount; ++i)
        opButtons[i]->setBounds (rr.removeFromTop (34).withTrimmedBottom (4));
    rr.removeFromTop (12);
    savedButton.setBounds (rr.removeFromBottom (30));
    rr.removeFromBottom (8);
    status.setBounds (rr);
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
    g.drawText ("Wave " + juce::String (position + 1.0f, 1), positionRow.withTrimmedLeft (positionRow.getWidth() - 80), juce::Justification::centredRight);

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
    heading (rr.removeFromTop (40).withTrimmedTop (14).withHeight (22), "DRAW");
    rr.removeFromTop (48);
    g.setColour (dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Pencil", pencilButton.getBounds().translated (0, pencilButton.getHeight() + 2).withHeight (16).expanded (10, 0), juce::Justification::centred);
    g.drawText ("Line", lineButton.getBounds().translated (0, lineButton.getHeight() + 2).withHeight (16).expanded (10, 0), juce::Justification::centred);
    heading (shapeButtons[0]->getBounds().translated (0, -38).withHeight (22).withWidth (rr.getWidth()), "SHAPES");
    heading (smoothing.getBounds().translated (-90, -36).withHeight (22).withWidth (rr.getWidth()), "TOOL SETTINGS");
    g.setColour (text);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("Smoothing", smoothing.getBounds().translated (-90, 0).withWidth (88), juce::Justification::centredLeft);
    heading (opButtons[0]->getBounds().translated (0, -36).withHeight (22), "WAVE");
}

void WavetableEditor::paintOverChildren (juce::Graphics& g)
{
    // Each shape's name with its picture.
    for (int i = 0; i < 6; ++i)
    {
        const auto b = shapeButtons[i]->getBounds().toFloat();
        g.setColour (text);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (shapeNames[i], b.withTrimmedLeft (10.0f).withTrimmedRight (44.0f), juce::Justification::centredLeft);
        const auto area = juce::Rectangle<float> (b.getRight() - 40.0f, b.getY() + 7.0f, 30.0f, b.getHeight() - 14.0f);
        std::vector<float> icon (i == peak::Shapes::noise ? 40 : 97);
        unsigned seed = 7;
        for (size_t k = 0; k < icon.size(); ++k)
            icon[k] = 0.8f * peak::Shapes::at ((peak::Shapes::Kind) i, (float) k / (float) icon.size(), seed);
        drawWave (g, area, icon, amber, 1.6f, false);
    }
}

//==============================================================================
void WavetableEditor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto p = e.getPosition();
    for (int w = 0; w < 5; ++w)
        if (thumbBounds (w).contains (p))
        {
            selectedWave = w;
            positionSlider.setValue (w, juce::dontSendNotification);
            position = (float) w;
            auditionPosition();
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

void WavetableEditor::mouseDrag (const juce::MouseEvent& e)
{
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

void WavetableEditor::mouseUp (const juce::MouseEvent&)
{
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
