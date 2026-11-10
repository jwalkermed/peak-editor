// Tests for PatchLibrary (My Banks, NOT SENT staging, undo/redo, files on disk).
// Runs in a temporary folder; never touches the real library. Run: ./build/.../LibraryTests
#include "peak/PatchLibrary.h"

#include <cstdio>
#include <functional>

namespace
{
int failures = 0;

#define CHECK(cond) \
    do { if (! (cond)) { std::printf ("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

void test (const char* name, const std::function<void()>& body)
{
    const auto before = failures;
    body();
    std::printf ("%s %s\n", failures == before ? "ok  " : "FAIL", name);
}

std::vector<peak::Patch> bankA()
{
    juce::MemoryBlock data;
    juce::File (juce::String (PEAK_FIXTURE_DIR) + "/bank-a.syx").loadFileAsData (data);
    std::vector<peak::Patch> out;
    for (const auto& m : peak::splitSysex (static_cast<const uint8_t*> (data.getData()), data.getSize()))
        out.push_back (*peak::Patch::fromSysex (m));
    return out;
}

juce::StringArray namesIn (const PatchLibrary& lib, const juce::String& bank)
{
    juce::StringArray n;
    for (const auto* e : lib.entriesIn (bank))
        n.add (juce::String (e->patch.name()));
    return n;
}

struct TempRoot
{
    juce::File dir = juce::File::createTempFile ("peak-library-test");
    TempRoot() { dir.createDirectory(); }
    ~TempRoot() { dir.deleteRecursively(); }
};
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const auto bank = bankA();

    test ("banks, adding, duplicate detection per bank", [&] {
        TempRoot t;
        PatchLibrary lib (t.dir);
        CHECK (lib.getBanks().contains (PatchLibrary::defaultBank));
        bool dup = false;
        lib.add (bank[0], "test", "Pads", &dup);
        CHECK (! dup);
        lib.add (bank[0], "test", "Pads", &dup);
        CHECK (dup);
        lib.add (bank[0], "test", "Other", &dup); // same sound in another bank is allowed
        CHECK (! dup);
        CHECK (lib.entriesIn ("Pads").size() == 1 && lib.entriesIn ("Other").size() == 1);
        CHECK (! lib.entriesIn ("Pads")[0]->patch.location().has_value()); // stored without a slot
    });

    test ("reordering within and between banks", [&] {
        TempRoot t;
        PatchLibrary lib (t.dir);
        for (int i = 0; i < 4; ++i)
            lib.add (bank[(size_t) i], "test", "B");
        const auto id = [&] (int i) { return lib.entriesIn ("B")[(size_t) i]->id; };
        // [Utopian, Tardis, All In Sync, Phat Poly] -> move Utopian below All In Sync (drop index 3, minus itself = 2)
        lib.moveEntries ({ id (0) }, "B", 2);
        CHECK ((namesIn (lib, "B") == juce::StringArray { "Tardis", "All In Sync", "Utopian Streams", "Phat Poly" }));
        lib.moveEntries ({ id (3) }, "B", 0); // Phat Poly to the top
        CHECK (namesIn (lib, "B")[0] == "Phat Poly");
        lib.moveEntries ({ id (1), id (2) }, "C", 99); // two into a new bank, keeping their order
        CHECK ((namesIn (lib, "C") == juce::StringArray { "Tardis", "All In Sync" }));
        CHECK (lib.entriesIn ("B").size() == 2);
        PatchLibrary reloaded (t.dir); // order survives a restart
        CHECK ((namesIn (reloaded, "C") == juce::StringArray { "Tardis", "All In Sync" }));
    });

    test ("NOT SENT: staging, showing, clearing when the synth matches", [&] {
        TempRoot t;
        PatchLibrary lib (t.dir);
        for (const auto& p : bank)
            lib.storePeakSlot (p);
        CHECK (lib.knownSlots (0) == 128 && lib.unsentCount() == 0);

        auto renamed = bank[5];
        renamed.setName ("Renamed");
        lib.stagePeakSlot (renamed);
        CHECK (lib.isUnsent ({ 0, 5 }) && lib.unsentCount (0) == 1);
        CHECK (lib.peakSlot ({ 0, 5 })->name() == "Renamed");       // the library shows the change
        CHECK (lib.syncedSlot ({ 0, 5 })->name() == bank[5].name()); // the synth still has the old one

        PatchLibrary reloaded (t.dir); // unsent changes survive a restart
        CHECK (reloaded.isUnsent ({ 0, 5 }));

        lib.stagePeakSlot (bank[5]); // staging what the synth already has is not a change
        CHECK (! lib.isUnsent ({ 0, 5 }));

        lib.stagePeakSlot (renamed);
        lib.storePeakSlot (renamed); // written to the synth: no longer NOT SENT
        CHECK (! lib.isUnsent ({ 0, 5 }) && lib.syncedSlot ({ 0, 5 })->name() == "Renamed");

        auto other = bank[6];
        other.setName ("X");
        lib.stagePeakSlot (other);
        lib.discardAllUnsent();
        CHECK (lib.unsentCount() == 0);
    });

    test ("backups use the synth's contents, not NOT SENT changes", [&] {
        TempRoot t;
        PatchLibrary lib (t.dir);
        for (const auto& p : bank)
            lib.storePeakSlot (p);
        auto changed = bank[0];
        changed.setName ("Changed");
        lib.stagePeakSlot (changed);
        juce::File f;
        CHECK (lib.backupBank (0, &f).wasOk());
        juce::MemoryBlock data;
        f.loadFileAsData (data);
        const auto msgs = peak::splitSysex (static_cast<const uint8_t*> (data.getData()), data.getSize());
        CHECK (msgs.size() == 128 && peak::Patch::fromSysex (msgs[0])->name() == bank[0].name());
    });

    test ("undo/redo restores library, banks, NOT SENT and files", [&] {
        TempRoot t;
        PatchLibrary lib (t.dir);
        CHECK (! lib.canUndo());

        lib.checkpoint();
        const auto id = lib.add (bank[0], "test", "Pads");
        lib.checkpoint();
        lib.rename (id, "New Name");
        lib.checkpoint();
        lib.remove (id);
        CHECK (lib.find (id) == nullptr);

        lib.undo(); // the delete
        CHECK (lib.find (id) != nullptr && lib.find (id)->patch.name() == "New Name");
        CHECK (t.dir.getChildFile ("Library/" + id + ".syx").existsAsFile());
        lib.undo(); // the rename
        CHECK (lib.find (id)->patch.name() == "Utopian Streams");
        lib.redo();
        CHECK (lib.find (id)->patch.name() == "New Name");
        lib.undo();
        lib.undo(); // the add
        CHECK (lib.find (id) == nullptr && ! t.dir.getChildFile ("Library/" + id + ".syx").existsAsFile());
        CHECK (! lib.canUndo() && lib.canRedo());

        lib.checkpoint();
        lib.stagePeakSlot (bank[3]);
        CHECK (lib.isUnsent ({ 0, 3 }));
        lib.undo();
        CHECK (! lib.isUnsent ({ 0, 3 }));
        lib.checkpoint(); // a new action clears redo
        CHECK (! lib.canRedo());
    });

    std::printf (failures == 0 ? "\nAll library tests passed\n" : "\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
