#pragma once

#include "Params.h"
#include "Sysex.h"

#include <array>
#include <optional>
#include <string>

namespace peak
{
// A single Peak patch, held as the raw 527-byte SysEx dump so that header and
// not-yet-understood bytes survive a decode/encode round trip untouched.
// Layout: docs/sysex-notes.md and docs/peak-patch-map.json.
class Patch
{
public:
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

    std::string name() const; // trailing spaces trimmed
    void setName (std::string_view); // non-printable characters become spaces, padded/truncated to 16

    bool operator== (const Patch&) const = default;

private:
    Raw raw {};
};
} // namespace peak
