#pragma once

#include "Params.h"
#include "Sysex.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace peak
{
// A single Peak patch, held as the raw 527-byte SysEx dump so that header and
// not-yet-understood bytes survive a decode/encode round trip untouched.
// Layout: docs/sysex-notes.md and docs/peak-patch-map.json.
class Patch
{
public:
    // A memory slot on the Peak: bank 0-3 (A-D), slot 0-127.
    struct Location
    {
        int bank = 0, slot = 0;

        std::string name() const; // "A000"
        bool operator== (const Location&) const = default;
    };

    static constexpr size_t dumpSize = 527;
    static constexpr size_t nameOffset = 16;
    static constexpr size_t nameLength = 16;

    using Raw = std::array<uint8_t, dumpSize>;

    // A zeroed body with a valid Novation single-patch header copied from a real
    // "Current" dump. Only for building patches from scratch.
    Patch();

    static std::optional<Patch> fromSysex (const Bytes& message);
    static bool looksLikePatchDump (const uint8_t* data, size_t size);

    Bytes toSysex() const { return { raw.begin(), raw.end() }; }
    const Raw& bytes() const noexcept { return raw; }

    int get (const ParamDef&) const;      // -1 for parameters not stored in the patch
    void set (const ParamDef&, int value); // clamped to the parameter's range; ignored if not stored

    // Where a dump came from / will be written to. A patch without a location is
    // the Peak's current sound: sending it changes the sound without saving, while
    // sending a located patch overwrites that slot.
    std::optional<Location> location() const;
    void setLocation (std::optional<Location>);

    // Bytes 16-525 (name + sound), for duplicate detection independent of location.
    std::string_view body() const;
    // Same name and sound. Stored patches hold Arp Chance 0 where the Peak's current sound
    // reports 100 (its default) for the same patch (seen 2026-11-10), so those count as equal.
    bool sameSound (const Patch&) const;

    std::string name() const; // trailing spaces trimmed
    void setName (std::string_view); // non-printable characters become spaces, padded/truncated to 16

    bool operator== (const Patch&) const = default;

    // Moving patches within Peak memory swaps rather than overwrites: patch i goes from
    // src[i] to dst[i], and the patches displaced from dst fill the slots src leaves
    // empty, in order. Returns (to, from) pairs: "slot `to` receives what was in `from`".
    static std::vector<std::pair<Location, Location>> planSwap (const std::vector<Location>& src, const std::vector<Location>& dst);

private:
    Raw raw {};
};
} // namespace peak
