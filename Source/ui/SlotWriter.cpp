#include "SlotWriter.h"

namespace SlotWriter
{
void confirmAndWrite (PeakModel& model, peak::Patch::Location where, juce::Component* parent, std::function<void (const juce::String&)> done)
{
    auto& library = model.getLibrary();
    const auto slot = juce::String (where.name());
    const auto& existing = library.peakSlot (where);
    const auto replacing = existing ? "\"" + juce::String (existing->name()) + "\"" : juce::String ("the patch in that slot (not read yet)");
    const auto canBackup = library.knownSlots (where.bank) == 128;
    const juce::String bankLetter = juce::String::charToString ((juce::juce_wchar) ('A' + where.bank));

    auto message = "Overwrite " + slot + " " + replacing + " on the Peak with \"" + juce::String (model.getPatch().name()) + "\"?\n\n";
    message << (canBackup ? "Bank " + bankLetter + " will be backed up first."
                          : "Bank " + bankLetter + " hasn't been read from the Peak, so it can't be backed up first. "
                                "To enable the automatic backup, cancel and dump it: Settings > Backup, Select = Bank " + bankLetter + ", Go.");

    // Dismissing the dialog maps to the Cancel role (result 0), never to the write.
    const auto options = juce::MessageBoxOptions()
                             .withIconType (juce::MessageBoxIconType::WarningIcon)
                             .withTitle ("Write to " + slot)
                             .withMessage (message)
                             .withButton (juce::MessageBoxButtonOptions (canBackup ? "Back up and write" : "Write without backup", 1)
                                              .withRole (juce::MessageBoxButtonRole::destroy))
                             .withButton (juce::MessageBoxButtonOptions ("Cancel", 0).withRole (juce::MessageBoxButtonRole::cancel))
                             .withAssociatedComponent (parent);
    juce::AlertWindow::showAsync (options, [&model, where, slot, done] (int result) {
        if (result != 1)
            return done ("Write to " + slot + " cancelled");
        bool ok = false;
        done (write (model, where, ok));
    });
}

juce::String writePatch (PeakModel& model, peak::Patch patch, peak::Patch::Location where, bool& ok)
{
    ok = false;
    auto& library = model.getLibrary();
    const auto canBackup = library.knownSlots (where.bank) == 128;
    juce::File backup;
    if (canBackup)
        if (const auto r = library.backupBank (where.bank, &backup); r.failed())
            return "Not written: backup failed (" + r.getErrorMessage() + ")";
    patch.setLocation (where);
    if (! model.getConnection().sendSysex (patch.toSysex()))
        return "Not written: no MIDI output to the Peak";
    library.storePeakSlot (patch);
    library.discardUnsent (where);
    model.getConnection().sendSysex (peak::slotRequest (where.bank, where.slot), 800.0); // read back
    ok = true;
    return "Saved to " + juce::String (where.name()) + (canBackup ? ", backup: " + backup.getFileName() : juce::String());
}

juce::String write (PeakModel& model, peak::Patch::Location where, bool& ok)
{
    ok = false;
    const auto slot = juce::String (where.name());
    const auto canBackup = model.getLibrary().knownSlots (where.bank) == 128;
    juce::File backup;
    if (canBackup)
        if (const auto r = model.getLibrary().backupBank (where.bank, &backup); r.failed())
            return "Not written: backup failed (" + r.getErrorMessage() + ")";

    if (! model.writeToSlot (where))
        return "Not written: no MIDI output to the Peak";
    // Read it back shortly after, so the library holds what the Peak actually stored.
    model.getConnection().sendSysex (peak::slotRequest (where.bank, where.slot), 800.0);

    ok = true;
    return "Wrote " + slot + (canBackup ? ", backup: " + backup.getFileName() : juce::String());
}
} // namespace SlotWriter
