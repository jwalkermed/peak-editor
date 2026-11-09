#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace peak
{
using Bytes = std::vector<uint8_t>;

// Novation's (Focusrite) MIDI manufacturer ID.
inline constexpr uint8_t novationId[] { 0x00, 0x20, 0x29 };

// Splits a raw byte stream (e.g. a .syx file) into complete F0..F7 messages.
// Stray bytes outside a message and truncated messages are dropped.
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
