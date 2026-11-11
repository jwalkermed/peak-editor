#include "PatchLibrary.h"

namespace
{
std::optional<peak::Patch> readPatch (const juce::File& f)
{
    juce::MemoryBlock data;
    if (! f.loadFileAsData (data))
        return std::nullopt;
    const auto* bytes = static_cast<const uint8_t*> (data.getData());
    return peak::Patch::fromSysex (peak::Bytes (bytes, bytes + data.getSize()));
}

bool writeBytes (const juce::File& f, const peak::Bytes& bytes)
{
    return f.getParentDirectory().createDirectory() && f.replaceWithData (bytes.data(), bytes.size());
}

const char bankLetters[] = "ABCD";

// Stable 64-bit FNV-1a fingerprint of a patch's sound (name + parameters, not its slot).
juce::String fingerprint (const peak::Patch& p)
{
    uint64_t h = 1469598103934665603ull;
    for (const auto c : p.body())
    {
        h ^= static_cast<uint8_t> (c);
        h *= 1099511628211ull;
    }
    return juce::String::toHexString ((juce::int64) h);
}
} // namespace

PatchLibrary::PatchLibrary()
    : PatchLibrary (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support/Peak Editor"))
{
}

PatchLibrary::PatchLibrary (const juce::File& rootDir)
{
    root = rootDir;
    libraryDir = root.getChildFile ("Library");
    banksDir = root.getChildFile ("Peak Banks");
    backupsDir = root.getChildFile ("Backups");

    loadIndex();
    {
        juce::MemoryBlock data;
        if (banksDir.getChildFile ("unsent.syx").loadFileAsData (data))
        {
            const auto* bytes = static_cast<const uint8_t*> (data.getData());
            for (const auto& msg : peak::splitSysex (bytes, data.getSize()))
                if (const auto p = peak::Patch::fromSysex (msg); p && p->location())
                    unsentBanks[(size_t) p->location()->bank][(size_t) p->location()->slot] = p;
        }
    }
    if (banks.isEmpty())
        banks.add (defaultBank);

    for (int b = 0; b < 4; ++b)
    {
        juce::MemoryBlock data;
        if (! banksDir.getChildFile (juce::String ("bank-") + bankLetters[b] + ".syx").loadFileAsData (data))
            continue;
        const auto* bytes = static_cast<const uint8_t*> (data.getData());
        for (const auto& msg : peak::splitSysex (bytes, data.getSize()))
            if (const auto p = peak::Patch::fromSysex (msg); p && p->location() && p->location()->bank == b)
                peakBanks[(size_t) b][(size_t) p->location()->slot] = p;
    }
}

const PatchLibrary::Entry* PatchLibrary::find (const juce::String& id) const
{
    for (const auto& e : entries)
        if (e.id == id)
            return &e;
    return nullptr;
}

std::vector<const PatchLibrary::Entry*> PatchLibrary::entriesIn (const juce::String& bank) const
{
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.bank == bank)
            out.push_back (&e);
    return out;
}

juce::String PatchLibrary::addBank (const juce::String& wanted)
{
    auto name = wanted.trim().isEmpty() ? juce::String ("New Bank") : wanted.trim();
    for (int n = 2; banks.contains (name); ++n)
        name = wanted.trim() + " " + juce::String (n);
    banks.add (name);
    saveIndex();
    changed.sendChangeMessage();
    return name;
}

void PatchLibrary::renameBank (const juce::String& from, const juce::String& to)
{
    const auto name = to.trim();
    if (name.isEmpty() || banks.contains (name) || ! banks.contains (from))
        return;
    banks.set (banks.indexOf (from), name);
    for (auto& e : entries)
        if (e.bank == from)
            e.bank = name;
    saveIndex();
    changed.sendChangeMessage();
}

void PatchLibrary::removeBank (const juce::String& name)
{
    for (const auto* e : entriesIn (name))
        libraryDir.getChildFile (e->id + ".syx").moveToTrash();
    entries.erase (std::remove_if (entries.begin(), entries.end(), [&] (const Entry& e) { return e.bank == name; }), entries.end());
    banks.removeString (name);
    saveIndex();
    changed.sendChangeMessage();
}

void PatchLibrary::moveToBank (const juce::String& id, const juce::String& bank)
{
    if (! banks.contains (bank))
        banks.add (bank);
    for (auto& e : entries)
        if (e.id == id)
            e.bank = bank;
    saveIndex();
    changed.sendChangeMessage();
}

void PatchLibrary::moveEntries (const juce::StringArray& ids, const juce::String& bank, int index)
{
    if (! banks.contains (bank))
        banks.add (bank);

    std::vector<Entry> moving;
    for (auto it = entries.begin(); it != entries.end();)
        if (ids.contains (it->id))
        {
            moving.push_back (std::move (*it));
            it = entries.erase (it);
        }
        else
            ++it;
    if (moving.empty())
        return;

    // Where the index-th remaining entry of the bank sits; else just after the bank's last entry.
    auto insertAt = entries.end();
    int seen = 0;
    for (auto it = entries.begin(); it != entries.end(); ++it)
        if (it->bank == bank)
        {
            if (seen++ == index)
            {
                insertAt = it;
                break;
            }
            insertAt = std::next (it);
        }

    for (auto& e : moving)
        e.bank = bank;
    entries.insert (insertAt, std::make_move_iterator (moving.begin()), std::make_move_iterator (moving.end()));
    saveIndex();
    changed.sendChangeMessage();
}

juce::String PatchLibrary::add (peak::Patch patch, const juce::String& origin, const juce::String& bank, bool* wasDuplicate)
{
    patch.setLocation (std::nullopt);
    if (! banks.contains (bank))
        banks.add (bank);

    for (const auto& e : entries)
        if (e.bank == bank && e.patch.body() == patch.body())
        {
            if (wasDuplicate != nullptr)
                *wasDuplicate = true;
            return e.id;
        }

    Entry e;
    e.id = juce::Uuid().toString();
    e.bank = bank;
    e.patch = patch;
    e.origin = origin;
    e.added = juce::Time::getCurrentTime();
    writeBytes (libraryDir.getChildFile (e.id + ".syx"), e.patch.toSysex());
    entries.push_back (std::move (e));

    if (wasDuplicate != nullptr)
        *wasDuplicate = false;
    saveIndex();
    changed.sendChangeMessage();
    return entries.back().id;
}

int PatchLibrary::importFile (const juce::File& file, int* duplicates)
{
    juce::MemoryBlock data;
    if (! file.loadFileAsData (data))
        return 0;

    int added = 0, dups = 0;
    const auto* bytes = static_cast<const uint8_t*> (data.getData());
    for (const auto& msg : peak::splitSysex (bytes, data.getSize()))
        if (const auto p = peak::Patch::fromSysex (msg))
        {
            const auto where = p->location() ? " " + juce::String (p->location()->name()) : juce::String();
            bool dup = false;
            add (*p, file.getFileName() + where, file.getFileNameWithoutExtension(), &dup);
            dup ? ++dups : ++added;
        }

    if (duplicates != nullptr)
        *duplicates = dups;
    return added;
}

bool PatchLibrary::exportFile (const juce::StringArray& ids, const juce::File& file) const
{
    std::vector<peak::Bytes> messages;
    for (const auto& id : ids)
        if (const auto* e = find (id))
            messages.push_back (e->patch.toSysex());
    return ! messages.empty() && writeBytes (file, peak::joinSysex (messages));
}

void PatchLibrary::remove (const juce::String& id)
{
    const auto it = std::find_if (entries.begin(), entries.end(), [&] (const Entry& e) { return e.id == id; });
    if (it == entries.end())
        return;

    libraryDir.getChildFile (id + ".syx").moveToTrash();
    entries.erase (it);
    saveIndex();
    changed.sendChangeMessage();
}

void PatchLibrary::rename (const juce::String& id, const juce::String& name)
{
    for (auto& e : entries)
        if (e.id == id)
        {
            e.patch.setName (name.toStdString());
            writeBytes (libraryDir.getChildFile (id + ".syx"), e.patch.toSysex());
            changed.sendChangeMessage();
        }
}

void PatchLibrary::setCategory (const juce::String& id, int category)
{
    for (auto& e : entries)
        if (e.id == id)
        {
            e.patch.set (*peak::findParam ("patch_category"), category);
            writeBytes (libraryDir.getChildFile (id + ".syx"), e.patch.toSysex());
            changed.sendChangeMessage();
        }
}

void PatchLibrary::replacePatch (const juce::String& id, const peak::Patch& patch)
{
    for (auto& e : entries)
        if (e.id == id)
        {
            e.patch = patch;
            e.patch.setLocation (std::nullopt);
            writeBytes (libraryDir.getChildFile (id + ".syx"), e.patch.toSysex());
            changed.sendChangeMessage();
        }
}

void PatchLibrary::setFavourite (const juce::String& id, bool favourite)
{
    for (auto& e : entries)
        if (e.id == id)
            e.favourite = favourite;
    saveIndex();
    changed.sendChangeMessage();
}

bool PatchLibrary::isPeakFavourite (const peak::Patch& p) const
{
    return peakFavourites.contains (fingerprint (p));
}

void PatchLibrary::setPeakFavourite (const peak::Patch& p, bool favourite)
{
    const auto f = fingerprint (p);
    if (favourite)
        peakFavourites.addIfNotAlreadyThere (f);
    else
        peakFavourites.removeString (f);
    saveIndex();
    changed.sendChangeMessage();
}

const std::optional<peak::Patch>& PatchLibrary::peakSlot (peak::Patch::Location loc) const
{
    const auto b = (size_t) juce::jlimit (0, 3, loc.bank), s = (size_t) juce::jlimit (0, 127, loc.slot);
    return unsentBanks[b][s] ? unsentBanks[b][s] : peakBanks[b][s];
}

const std::optional<peak::Patch>& PatchLibrary::syncedSlot (peak::Patch::Location loc) const
{
    return peakBanks[(size_t) juce::jlimit (0, 3, loc.bank)][(size_t) juce::jlimit (0, 127, loc.slot)];
}

void PatchLibrary::storePeakSlot (const peak::Patch& patch)
{
    const auto loc = patch.location();
    if (! loc)
        return;

    peakBanks[(size_t) loc->bank][(size_t) loc->slot] = patch;
    savePeakBank (loc->bank);

    // The synth now has exactly this sound: a change waiting to send it is done.
    auto& pending = unsentBanks[(size_t) loc->bank][(size_t) loc->slot];
    if (pending && pending->body() == patch.body())
    {
        pending.reset();
        saveUnsent();
    }
    changed.sendChangeMessage();
}

void PatchLibrary::stagePeakSlot (const peak::Patch& patch)
{
    const auto loc = patch.location();
    if (! loc)
        return;

    auto& pending = unsentBanks[(size_t) loc->bank][(size_t) loc->slot];
    const auto& synced = peakBanks[(size_t) loc->bank][(size_t) loc->slot];
    if (synced && synced->body() == patch.body())
        pending.reset(); // back to what the synth has: nothing to send
    else
        pending = patch;
    saveUnsent();
    changed.sendChangeMessage();
}

bool PatchLibrary::isUnsent (peak::Patch::Location loc) const
{
    return unsentBanks[(size_t) juce::jlimit (0, 3, loc.bank)][(size_t) juce::jlimit (0, 127, loc.slot)].has_value();
}

int PatchLibrary::unsentCount (int bank) const
{
    int n = 0;
    for (int b = 0; b < 4; ++b)
        if (bank < 0 || bank == b)
            for (const auto& s : unsentBanks[(size_t) b])
                n += s.has_value() ? 1 : 0;
    return n;
}

std::vector<peak::Patch> PatchLibrary::unsentPatches() const
{
    std::vector<peak::Patch> out;
    for (const auto& bank : unsentBanks)
        for (const auto& s : bank)
            if (s)
                out.push_back (*s);
    return out;
}

void PatchLibrary::discardUnsent (peak::Patch::Location loc)
{
    unsentBanks[(size_t) juce::jlimit (0, 3, loc.bank)][(size_t) juce::jlimit (0, 127, loc.slot)].reset();
    saveUnsent();
    changed.sendChangeMessage();
}

void PatchLibrary::discardAllUnsent (int bank)
{
    for (int b = 0; b < 4; ++b)
        if (bank < 0 || bank == b)
            for (auto& s : unsentBanks[(size_t) b])
                s.reset();
    saveUnsent();
    changed.sendChangeMessage();
}

void PatchLibrary::saveUnsent() const
{
    std::vector<peak::Bytes> messages;
    for (const auto& p : unsentPatches())
        messages.push_back (p.toSysex());
    const auto f = banksDir.getChildFile ("unsent.syx");
    if (messages.empty())
        f.deleteFile();
    else
        writeBytes (f, peak::joinSysex (messages));
}

PatchLibrary::Snapshot PatchLibrary::snapshot() const
{
    return { entries, banks, peakFavourites, unsentBanks };
}

void PatchLibrary::checkpoint()
{
    undoStack.push_back (snapshot());
    if (undoStack.size() > maxUndo)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void PatchLibrary::undo()
{
    if (undoStack.empty())
        return;
    redoStack.push_back (snapshot());
    const auto previous = std::move (undoStack.back());
    undoStack.pop_back();
    restore (previous);
}

void PatchLibrary::redo()
{
    if (redoStack.empty())
        return;
    undoStack.push_back (snapshot());
    const auto next = std::move (redoStack.back());
    redoStack.pop_back();
    restore (next);
}

void PatchLibrary::restore (const Snapshot& s)
{
    entries = s.entries;
    banks = s.banks;
    peakFavourites = s.peakFavourites;
    unsentBanks = s.unsent;

    // Make the files match: every entry's .syx rewritten, any others removed.
    for (const auto& e : entries)
        writeBytes (libraryDir.getChildFile (e.id + ".syx"), e.patch.toSysex());
    for (const auto& f : libraryDir.findChildFiles (juce::File::findFiles, false, "*.syx"))
        if (find (f.getFileNameWithoutExtension()) == nullptr)
            f.deleteFile();

    saveIndex();
    saveUnsent();
    changed.sendChangeMessage();
}

int PatchLibrary::knownSlots (int bank) const
{
    int n = 0;
    for (const auto& s : peakBanks[(size_t) bank])
        n += s.has_value() ? 1 : 0;
    return n;
}

juce::Result PatchLibrary::backupBank (int bank, juce::File* written) const // the synth's contents, not NOT SENT changes
{
    if (knownSlots (bank) < 128)
        return juce::Result::fail (juce::String ("Bank ") + bankLetters[bank] + " has not been fully read from the Peak yet");

    std::vector<peak::Bytes> messages;
    for (const auto& s : peakBanks[(size_t) bank])
        messages.push_back (s->toSysex());

    const auto file = backupsDir.getChildFile (juce::String ("bank-") + bankLetters[bank] + "-"
                                               + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".syx");
    if (! writeBytes (file, peak::joinSysex (messages)))
        return juce::Result::fail ("Could not write " + file.getFullPathName());

    if (written != nullptr)
        *written = file;
    return juce::Result::ok();
}

juce::Result PatchLibrary::backupPatches (const std::vector<peak::Patch>& patches, const juce::String& name, juce::File* written) const
{
    std::vector<peak::Bytes> messages;
    for (const auto& p : patches)
        messages.push_back (p.toSysex());
    const auto file = backupsDir.getChildFile (name + "-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".syx");
    if (! writeBytes (file, peak::joinSysex (messages)))
        return juce::Result::fail ("Could not write " + file.getFullPathName());
    if (written != nullptr)
        *written = file;
    return juce::Result::ok();
}

void PatchLibrary::savePeakBank (int bank) const
{
    std::vector<peak::Bytes> messages;
    for (const auto& s : peakBanks[(size_t) bank])
        if (s)
            messages.push_back (s->toSysex());
    writeBytes (banksDir.getChildFile (juce::String ("bank-") + bankLetters[bank] + ".syx"), peak::joinSysex (messages));
}

void PatchLibrary::saveIndex() const
{
    juce::Array<juce::var> list;
    for (const auto& e : entries)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", e.id);
        o->setProperty ("bank", e.bank);
        o->setProperty ("name", juce::String (e.patch.name())); // informational; the .syx is authoritative
        o->setProperty ("favourite", e.favourite);
        o->setProperty ("origin", e.origin);
        o->setProperty ("added", e.added.toISO8601 (true));
        list.add (juce::var (o));
    }
    juce::Array<juce::var> bankList;
    for (const auto& b : banks)
        bankList.add (b);

    auto* index = new juce::DynamicObject();
    index->setProperty ("banks", bankList);
    index->setProperty ("entries", list);
    juce::Array<juce::var> favs;
    for (const auto& f : peakFavourites)
        favs.add (f);
    index->setProperty ("peakFavourites", favs);
    libraryDir.createDirectory();
    libraryDir.getChildFile ("index.json").replaceWithText (juce::JSON::toString (juce::var (index)));
}

void PatchLibrary::loadIndex()
{
    // Index format: { banks: [...], entries: [...] }. The first version was a bare entries array.
    const auto json = juce::JSON::parse (libraryDir.getChildFile ("index.json"));
    const auto entryList = json.isArray() ? json : json["entries"];
    if (const auto* favs = json["peakFavourites"].getArray())
        for (const auto& f : *favs)
            peakFavourites.add (f.toString());
    if (const auto* names = json["banks"].getArray())
        for (const auto& b : *names)
            banks.addIfNotAlreadyThere (b.toString());

    if (const auto* list = entryList.getArray())
        for (const auto& v : *list)
        {
            Entry e;
            e.id = v["id"].toString();
            const auto p = readPatch (libraryDir.getChildFile (e.id + ".syx"));
            if (e.id.isEmpty() || ! p)
                continue;
            e.patch = *p;
            e.favourite = (bool) v["favourite"];
            e.origin = v["origin"].toString();
            e.added = juce::Time::fromISO8601 (v["added"].toString());
            e.bank = v["bank"].toString();
            if (e.bank.isEmpty()) // from before banks: group by where the patch came from
                e.bank = e.origin.startsWith ("Peak ") ? "Peak Bank " + e.origin.substring (5, 6)
                       : e.origin.containsIgnoreCase (".syx") ? e.origin.upToFirstOccurrenceOf (".syx", false, true)
                                                               : defaultBank;
            banks.addIfNotAlreadyThere (e.bank);
            entries.push_back (std::move (e));
        }
}
