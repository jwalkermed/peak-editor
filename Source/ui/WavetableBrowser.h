#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// The Wavetables tab's file browser. The folder button adds a folder, chosen in a Finder window (so
// macOS asks no permission), to a list kept between sessions; Home shows that list, and a click opens
// one of them. Inside, one column lists its subfolders and the files the Peak's wavetables can come
// from: the formats Novation Components imports, .wav and .mp3 audio, and .syx/.sysex files holding a
// Peak wavetable. Back and Forward go through the places visited; Up goes to the enclosing folder, and
// from the top of an added folder back Home. Clicking a folder opens it; double-clicking a wavetable
// (or Return) calls onFileChosen. Right-clicking an added folder at Home removes it from the list.
class WavetableBrowser final : public juce::Component,
                               private juce::ListBoxModel
{
public:
    explicit WavetableBrowser (juce::PropertiesFile* settings);
    ~WavetableBrowser() override;

    std::function<void (const juce::File&)> onFileChosen;

    void addFolder (const juce::File&, bool remember = true); // to the Home list (remember: kept between sessions)
    void openFolder (const juce::File&);                      // and adds it to the history
    void select (const juce::File&);                          // a wavetable in the folder: selected (not loaded)
    void resized() override;
    void paintOverChildren (juce::Graphics&) override; // the empty-list messages, over the list

    static bool isWavetableFile (const juce::File&);

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;
    juce::String getTooltipForRow (int row) override; // the full path

    void go (const juce::File& place);   // a folder, or Home (an empty File), added to the history
    void show (const juce::File& place); // without touching the history
    void chooseFolder();
    void choose (int row);
    void updateButtons();
    void saveFolders();
    juce::File rootOf (const juce::File&) const; // the added folder it's in

    class IconButton;
    juce::PropertiesFile* settings;
    std::unique_ptr<IconButton> folderButton, backButton, forwardButton, upButton;
    juce::TextButton homeButton { "Home" };
    juce::Label path;
    juce::ListBox list { "Wavetables", this };

    juce::Array<juce::File> added;   // the Home list
    juce::File folder;               // where it is; empty at Home
    juce::Array<juce::File> folders, files;
    std::vector<juce::File> history; // empty File = Home
    int historyIndex = -1;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WavetableBrowser)
};
