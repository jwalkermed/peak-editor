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
} // namespace

PatchLibrary::PatchLibrary()
{
    root = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support/Peak Editor");
    libraryDir = root.getChildFile ("Library");
    banksDir = root.getChildFile ("Peak Banks");
    backupsDir = root.getChildFile ("Backups");

    loadIndex();

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

juce::String PatchLibrary::add (peak::Patch patch, const juce::String& origin, bool* wasDuplicate)
{
    patch.setLocation (std::nullopt);

    for (const auto& e : entries)
        if (e.patch.body() == patch.body())
        {
            if (wasDuplicate != nullptr)
                *wasDuplicate = true;
            return e.id;
        }

    Entry e;
    e.id = juce::Uuid().toString();
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
            add (*p, file.getFileName() + where, &dup);
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

void PatchLibrary::setTags (const juce::String& id, const juce::StringArray& tags)
{
    for (auto& e : entries)
        if (e.id == id)
            e.tags = tags;
    saveIndex();
    changed.sendChangeMessage();
}

void PatchLibrary::setFavourite (const juce::String& id, bool favourite)
{
    for (auto& e : entries)
        if (e.id == id)
            e.favourite = favourite;
    saveIndex();
    changed.sendChangeMessage();
}

const std::optional<peak::Patch>& PatchLibrary::peakSlot (peak::Patch::Location loc) const
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
    changed.sendChangeMessage();
}

int PatchLibrary::knownSlots (int bank) const
{
    int n = 0;
    for (const auto& s : peakBanks[(size_t) bank])
        n += s.has_value() ? 1 : 0;
    return n;
}

juce::Result PatchLibrary::backupBank (int bank, juce::File* written) const
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
        o->setProperty ("name", juce::String (e.patch.name())); // informational; the .syx is authoritative
        o->setProperty ("tags", e.tags.joinIntoString (","));
        o->setProperty ("favourite", e.favourite);
        o->setProperty ("origin", e.origin);
        o->setProperty ("added", e.added.toISO8601 (true));
        list.add (juce::var (o));
    }
    libraryDir.createDirectory();
    libraryDir.getChildFile ("index.json").replaceWithText (juce::JSON::toString (juce::var (list)));
}

void PatchLibrary::loadIndex()
{
    const auto json = juce::JSON::parse (libraryDir.getChildFile ("index.json"));
    if (const auto* list = json.getArray())
        for (const auto& v : *list)
        {
            Entry e;
            e.id = v["id"].toString();
            const auto p = readPatch (libraryDir.getChildFile (e.id + ".syx"));
            if (e.id.isEmpty() || ! p)
                continue;
            e.patch = *p;
            e.tags = juce::StringArray::fromTokens (v["tags"].toString(), ",", "");
            e.tags.removeEmptyStrings();
            e.favourite = (bool) v["favourite"];
            e.origin = v["origin"].toString();
            e.added = juce::Time::fromISO8601 (v["added"].toString());
            entries.push_back (std::move (e));
        }
}
