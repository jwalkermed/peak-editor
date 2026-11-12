#include "WavetableBrowser.h"

#include "peak/core/Wavetable.h"
#include "ui/CardStyle.h"

namespace
{
using namespace CardStyle;

const char* const foldersKey = "wavetableFolders"; // the Home list, one path per line
const char* const audioExtensions = "wav;mp3"; // what Components' Audio File import accepts
constexpr int rowHeight = 24;

void drawWaveIcon (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    juce::Path p;
    for (int i = 0; i <= 24; ++i)
    {
        const auto x = r.getX() + r.getWidth() * (float) i / 24.0f;
        const auto y = r.getCentreY() - r.getHeight() * 0.45f * std::sin (juce::MathConstants<float>::twoPi * (float) i / 24.0f);
        i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
    }
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (1.4f));
}
} // namespace

// A small button with a drawn icon: the folder (with a +: it adds one), or a Back/Forward/Up chevron.
class WavetableBrowser::IconButton final : public juce::Button
{
public:
    enum Kind { folderIcon, back, forward, up };
    IconButton (Kind k, const juce::String& tip) : juce::Button (tip), kind (k) { setTooltip (tip); }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (over || down ? cellHover : cell);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (line);
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
        const auto colour = isEnabled() ? (over ? juce::Colours::white : CardStyle::text) : dim.withAlpha (0.4f);
        const auto icon = r.withSizeKeepingCentre (16.0f, 13.0f);
        if (kind == folderIcon)
        {
            g.setColour (amber.withAlpha (isEnabled() ? 1.0f : 0.4f));
            g.fillPath (folderShape (icon.translated (-2.0f, 0.0f)));
            const auto plus = juce::Point<float> (icon.getRight() + 1.0f, icon.getBottom() - 1.0f);
            g.setColour (cell);
            g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (plus));
            g.setColour (juce::Colours::white);
            g.fillRect (juce::Rectangle<float> (6.0f, 1.6f).withCentre (plus));
            g.fillRect (juce::Rectangle<float> (1.6f, 6.0f).withCentre (plus));
            return;
        }
        juce::Path chevron;
        const auto c = r.getCentre();
        if (kind == up)
        {
            chevron.startNewSubPath (c.x - 6.0f, c.y + 3.0f);
            chevron.lineTo (c.x, c.y - 3.0f);
            chevron.lineTo (c.x + 6.0f, c.y + 3.0f);
        }
        else
        {
            const auto d = kind == back ? 1.0f : -1.0f;
            chevron.startNewSubPath (c.x + 3.0f * d, c.y - 6.0f);
            chevron.lineTo (c.x - 3.0f * d, c.y);
            chevron.lineTo (c.x + 3.0f * d, c.y + 6.0f);
        }
        g.setColour (colour);
        g.strokePath (chevron, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

private:
    Kind kind;
};

WavetableBrowser::WavetableBrowser (juce::PropertiesFile* s) : settings (s)
{
    folderButton = std::make_unique<IconButton> (IconButton::folderIcon, "Add a folder of wavetables");
    backButton = std::make_unique<IconButton> (IconButton::back, "Back");
    forwardButton = std::make_unique<IconButton> (IconButton::forward, "Forward");
    upButton = std::make_unique<IconButton> (IconButton::up, "Up to the enclosing folder (from an added folder: Home)");
    homeButton.setTooltip ("The folders you've added");
    folderButton->onClick = [this] { chooseFolder(); };
    backButton->onClick = [this] {
        if (historyIndex > 0)
            show (history[(size_t) --historyIndex]);
    };
    forwardButton->onClick = [this] {
        if (historyIndex + 1 < (int) history.size())
            show (history[(size_t) ++historyIndex]);
    };
    upButton->onClick = [this] {
        if (folder == juce::File())
            return;
        go (added.contains (folder) || folder.getParentDirectory() == folder ? juce::File() : folder.getParentDirectory());
    };
    homeButton.onClick = [this] { go ({}); };

    path.setFont (juce::FontOptions (12.5f));
    path.setColour (juce::Label::textColourId, dim);
    path.setMinimumHorizontalScale (1.0f);

    list.setRowHeight (rowHeight);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff141517));
    list.setColour (juce::ListBox::outlineColourId, line);
    list.setOutlineThickness (1);

    for (auto* c : std::initializer_list<juce::Component*> { folderButton.get(), backButton.get(), forwardButton.get(), upButton.get(), &homeButton, &path, &list })
        addAndMakeVisible (c);

    // The folders added before. They aren't looked at until opened: macOS asks permission for
    // Desktop, Documents and Downloads even to check that something there exists.
    if (settings != nullptr)
        for (const auto& line : juce::StringArray::fromLines (settings->getValue (foldersKey)))
            if (line.isNotEmpty() && juce::File::isAbsolutePath (line))
                added.addIfNotAlreadyThere (juce::File (line));
    go ({});
}

WavetableBrowser::~WavetableBrowser() = default;

bool WavetableBrowser::isWavetableFile (const juce::File& f)
{
    if (f.hasFileExtension (audioExtensions))
        return true;
    if (! f.hasFileExtension ("syx;sysex") || f.getSize() > 64 * 1024)
        return false;
    juce::MemoryBlock data;
    if (! f.loadFileAsData (data))
        return false;
    return peak::Wavetable::fromSysex (peak::splitSysex (static_cast<const uint8_t*> (data.getData()), data.getSize())).has_value();
}

void WavetableBrowser::addFolder (const juce::File& f, bool remember)
{
    if (f == juce::File() || added.contains (f))
        return;
    added.add (f);
    std::sort (added.begin(), added.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
    if (remember)
        saveFolders();
    if (folder == juce::File())
        show ({});
}

void WavetableBrowser::saveFolders()
{
    if (settings == nullptr)
        return;
    juce::StringArray paths;
    for (const auto& f : added)
        paths.add (f.getFullPathName());
    settings->setValue (foldersKey, paths.joinIntoString ("\n"));
}

void WavetableBrowser::openFolder (const juce::File& f)
{
    if (f.isDirectory())
        go (f);
}

void WavetableBrowser::go (const juce::File& place)
{
    history.resize ((size_t) (historyIndex + 1)); // somewhere new drops what Forward would have gone to
    history.push_back (place);
    historyIndex = (int) history.size() - 1;
    show (place);
}

juce::File WavetableBrowser::rootOf (const juce::File& f) const
{
    for (const auto& root : added)
        if (f == root || f.isAChildOf (root))
            return root;
    return {};
}

void WavetableBrowser::select (const juce::File& f)
{
    if (const auto i = files.indexOf (f); i >= 0)
        list.selectRow (folders.size() + i);
}

void WavetableBrowser::show (const juce::File& place)
{
    folder = place;
    folders.clear();
    files.clear();
    if (place == juce::File())
    {
        folders = added; // Home: the added folders
        path.setText ("Home", juce::dontSendNotification);
        path.setTooltip ({});
    }
    else
    {
        for (const auto& entry : juce::RangedDirectoryIterator (place, false, "*", juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles))
        {
            const auto& file = entry.getFile();
            if (entry.isDirectory())
                folders.add (file);
            else if (isWavetableFile (file))
                files.add (file);
        }
        const auto byName = [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; };
        std::sort (folders.begin(), folders.end(), byName);
        std::sort (files.begin(), files.end(), byName);
        // Where it is within its added folder, e.g. "Serum Tables / Analog".
        const auto root = rootOf (place);
        const auto within = root == juce::File() || place == root ? place.getFileName()
                                                                  : root.getFileName() + " / " + place.getRelativePathFrom (root).replace ("/", " / ");
        path.setText (within, juce::dontSendNotification);
        path.setTooltip (place.getFullPathName());
    }
    list.deselectAllRows();
    list.updateContent();
    list.scrollToEnsureRowIsOnscreen (0);
    list.repaint();
    updateButtons();
    repaint();
}

void WavetableBrowser::chooseFolder()
{
    // A Finder window: what's chosen there macOS lets the editor read without asking.
    chooser = std::make_unique<juce::FileChooser> ("Add a folder of wavetables",
                                                   folder != juce::File() ? folder : juce::File::getSpecialLocation (juce::File::userHomeDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc) {
        const auto f = fc.getResult();
        if (! f.isDirectory())
            return;
        addFolder (f);
        go ({}); // Home, with the new folder in the list
        if (const auto i = folders.indexOf (f); i >= 0)
            list.selectRow (i);
    });
}

void WavetableBrowser::updateButtons()
{
    backButton->setEnabled (historyIndex > 0);
    forwardButton->setEnabled (historyIndex + 1 < (int) history.size());
    upButton->setEnabled (folder != juce::File());
    homeButton.setEnabled (folder != juce::File());
}

int WavetableBrowser::getNumRows()
{
    return folders.size() + files.size();
}

void WavetableBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, getNumRows()))
        return;
    const bool isFolder = row < folders.size();
    const auto& file = isFolder ? folders[row] : files[row - folders.size()];
    if (selected)
    {
        g.setColour (barFill.withAlpha (0.55f));
        g.fillRect (0, 0, width, height);
    }
    auto r = juce::Rectangle<float> ((float) width, (float) height).reduced (8.0f, 0.0f);
    const auto icon = r.removeFromLeft (16.0f).withSizeKeepingCentre (16.0f, 11.0f);
    if (isFolder)
    {
        g.setColour (amber.withAlpha (0.85f));
        g.fillPath (folderShape (icon));
    }
    else
        drawWaveIcon (g, icon, selected ? amber : blue);
    r.removeFromLeft (8.0f);
    if (! isFolder)
    {
        g.setColour (dim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (file.getFileExtension().substring (1).toUpperCase(), r.removeFromRight (34.0f), juce::Justification::centredRight);
    }
    else if (folder == juce::File())
    {
        // At Home, the folder each added one is in (to tell apart two of the same name).
        g.setColour (dim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("in " + file.getParentDirectory().getFileName(), r.removeFromRight (r.getWidth() * 0.45f), juce::Justification::centredRight, true);
    }
    g.setColour (selected ? juce::Colours::white : text);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (isFolder ? file.getFileName() : file.getFileNameWithoutExtension(), r, juce::Justification::centredLeft, true);
}

void WavetableBrowser::choose (int row)
{
    if (row < 0)
        return;
    if (row < folders.size())
        go (folders[row]);
    else if (row - folders.size() < files.size() && onFileChosen)
        onFileChosen (files[row - folders.size()]);
}

void WavetableBrowser::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow (row, folders.size()))
        return; // a wavetable loads with a double-click
    if (e.mods.isPopupMenu() && folder == juce::File())
    {
        // At Home: right-click to remove a folder from the list (nothing is deleted).
        const auto f = folders[row];
        juce::PopupMenu menu;
        menu.addItem ("Remove \"" + f.getFileName() + "\" from the list", [this, f] {
            added.removeFirstMatchingValue (f);
            saveFolders();
            show ({});
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
        return;
    }
    choose (row); // a folder opens with one click
}

void WavetableBrowser::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (row >= folders.size())
        choose (row);
}

juce::String WavetableBrowser::getTooltipForRow (int row)
{
    if (juce::isPositiveAndBelow (row, folders.size()))
        return folders[row].getFullPathName();
    if (juce::isPositiveAndBelow (row - folders.size(), files.size()))
        return files[row - folders.size()].getFullPathName();
    return {};
}

void WavetableBrowser::returnKeyPressed (int row)
{
    choose (row);
}

void WavetableBrowser::paintOverChildren (juce::Graphics& g)
{
    g.setColour (dim);
    g.setFont (juce::FontOptions (13.0f));
    if (folder == juce::File())
    {
        if (added.isEmpty())
            g.drawFittedText ("Add a folder of wavetables with the folder button.", list.getBounds().reduced (16), juce::Justification::centred, 3);
    }
    else if (files.isEmpty())
    {
        // Over the empty part of the list (its folders, if any, are still listed above).
        const auto r = list.getBounds().withTrimmedTop (folders.size() * rowHeight + 1);
        g.drawFittedText ("No compatible wavetables identified.", r.reduced (12), juce::Justification::centred, 2);
    }
}

void WavetableBrowser::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (28);
    folderButton->setBounds (top.removeFromLeft (34));
    top.removeFromLeft (6);
    backButton->setBounds (top.removeFromLeft (28));
    top.removeFromLeft (4);
    forwardButton->setBounds (top.removeFromLeft (28));
    top.removeFromLeft (4);
    upButton->setBounds (top.removeFromLeft (28));
    top.removeFromLeft (4);
    homeButton.setBounds (top.removeFromLeft (54));
    top.removeFromLeft (8);
    path.setBounds (top);
    r.removeFromTop (8);
    list.setBounds (r);
}
