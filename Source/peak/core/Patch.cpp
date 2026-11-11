#include "Patch.h"

#include <algorithm>
#include <cstdio>

namespace peak
{
namespace
{
// Header of a "Current" dump from firmware 2.2 (meaning of bytes 4-15 still unverified).
constexpr uint8_t currentHeader[] { 0xF0, 0x00, 0x20, 0x29, 0x01, 0x10, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
} // namespace

Patch::Patch()
{
    std::copy (std::begin (currentHeader), std::end (currentHeader), raw.begin());
    raw.back() = 0xF7;
    setName ("Init");
}

bool Patch::looksLikePatchDump (const uint8_t* data, size_t size)
{
    return size == dumpSize && isNovationSysex (data, size) && data[size - 1] == 0xF7 && data[8] <= 1
        && std::all_of (data + 1, data + size - 1, [] (uint8_t b) { return b < 0x80; });
}

std::optional<Patch> Patch::fromSysex (const Bytes& message)
{
    if (! looksLikePatchDump (message.data(), message.size()))
        return std::nullopt;

    Patch p;
    std::copy (message.begin(), message.end(), p.raw.begin());
    return p;
}

int Patch::get (const ParamDef& def) const
{
    if (! def.isStored())
        return -1;

    const auto o = static_cast<size_t> (def.offset);
    if (def.kind == MidiKind::ccPair)
        return (raw[o] << 1) | (raw[o + 1] >> 6);

    return raw[o];
}

void Patch::set (const ParamDef& def, int value)
{
    if (! def.isStored())
        return;

    value = std::clamp (value, def.min, def.max);
    const auto o = static_cast<size_t> (def.offset);

    if (def.kind == MidiKind::ccPair)
    {
        raw[o] = static_cast<uint8_t> (value >> 1);
        raw[o + 1] = static_cast<uint8_t> ((value & 1) << 6);
    }
    else
    {
        raw[o] = static_cast<uint8_t> (value);
    }
}

namespace
{
constexpr size_t storedFlagByte = 8, bankByte = 12, slotByte = 13; // see docs/sysex-notes.md
}

std::string Patch::Location::name() const
{
    char s[8];
    std::snprintf (s, sizeof (s), "%c%03d", 'A' + bank, slot);
    return s;
}

std::optional<Patch::Location> Patch::location() const
{
    if (raw[storedFlagByte] == 0 || raw[bankByte] < 1 || raw[bankByte] > 4)
        return std::nullopt;
    return Location { raw[bankByte] - 1, raw[slotByte] };
}

void Patch::setLocation (std::optional<Location> loc)
{
    raw[storedFlagByte] = loc ? 1 : 0;
    raw[bankByte] = loc ? static_cast<uint8_t> (std::clamp (loc->bank, 0, 3) + 1) : 0;
    raw[slotByte] = loc ? static_cast<uint8_t> (std::clamp (loc->slot, 0, 127)) : 0;
}

std::vector<std::pair<Patch::Location, Patch::Location>> Patch::planSwap (const std::vector<Location>& src, const std::vector<Location>& dst)
{
    const auto contains = [] (const std::vector<Location>& v, Location l) { return std::find (v.begin(), v.end(), l) != v.end(); };

    std::vector<std::pair<Location, Location>> moves;
    for (size_t i = 0; i < src.size() && i < dst.size(); ++i)
        moves.emplace_back (dst[i], src[i]);

    std::vector<Location> vacated, displaced;
    for (size_t i = 0; i < src.size() && i < dst.size(); ++i)
    {
        if (! contains (dst, src[i]))
            vacated.push_back (src[i]);
        if (! contains (src, dst[i]))
            displaced.push_back (dst[i]);
    }
    for (size_t i = 0; i < vacated.size() && i < displaced.size(); ++i)
        moves.emplace_back (vacated[i], displaced[i]);

    return moves;
}

std::string_view Patch::body() const
{
    return { reinterpret_cast<const char*> (raw.data()) + nameOffset, dumpSize - nameOffset - 1 };
}

bool Patch::sameSound (const Patch& other) const
{
    static const auto chance = static_cast<size_t> (findParam ("arp_chance")->offset);
    const auto normal = [] (uint8_t v) { return v == 0 ? uint8_t (100) : v; };
    for (size_t i = nameOffset; i < dumpSize - 1; ++i)
        if (i == chance ? normal (raw[i]) != normal (other.raw[i]) : raw[i] != other.raw[i])
            return false;
    return true;
}

std::string Patch::name() const
{
    std::string s (raw.begin() + nameOffset, raw.begin() + nameOffset + nameLength);
    s.erase (s.find_last_not_of (' ') + 1);
    return s;
}

void Patch::setName (std::string_view newName)
{
    for (size_t i = 0; i < nameLength; ++i)
    {
        const auto c = i < newName.size() ? static_cast<uint8_t> (newName[i]) : uint8_t (' ');
        raw[nameOffset + i] = (c >= 0x20 && c < 0x7F) ? c : uint8_t (' ');
    }
}
} // namespace peak
