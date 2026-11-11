#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace peak
{
using Bytes = std::vector<uint8_t>;

// Novation's (Focusrite) MIDI manufacturer ID.
inline constexpr uint8_t novationId[] { 0x00, 0x20, 0x29 };

// Splits a raw byte stream (e.g. a .syx file) into complete F0..F7 messages.
// Stray bytes outside a message and truncated messages are dropped.
// Asks the Peak for a dump (verified on firmware 2.2, 2026-11-10): the dump header with
// command 0x40 = current sound, 0x41 = the patch in a slot (bank A = 1), 0x44 = global settings.
Bytes currentSoundRequest();
Bytes slotRequest (int bank, int slot); // bank 0-3 (A-D), slot 0-127
Bytes settingsRequest();
// Wavetable names (verified 2026-11-11): command 0x47 with the wavetable number (WaveMore value,
// 0-73) in byte 11. The reply has type 07, the number in byte 14 and up to 7 name characters from
// byte 15. 64-73 are the user wavetables.
Bytes wavetableNameRequest (int index);
bool isWavetableName (const uint8_t* data, size_t size);
std::optional<std::pair<int, std::string>> parseWavetableName (const Bytes&);

std::vector<Bytes> splitSysex (const uint8_t* data, size_t size);
inline std::vector<Bytes> splitSysex (const Bytes& b) { return splitSysex (b.data(), b.size()); }

// Concatenates messages back into a single .syx stream.
Bytes joinSysex (const std::vector<Bytes>& messages);

bool isNovationSysex (const uint8_t* data, size_t size);

// "F0 00 20 29 ..." — stops after maxBytes and appends "…" if truncated.
std::string toHex (const uint8_t* data, size_t size, size_t maxBytes = SIZE_MAX);

// Parses hex like "F0 00 20 29 F7", "f0,00,20" or "F0002029F7".
// Returns nullopt on anything that is not whole hex bytes.
std::optional<Bytes> parseHex (std::string_view text);
} // namespace peak
