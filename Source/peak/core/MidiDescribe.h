#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace peak
{
// One-line human-readable description of a MIDI message for the monitor.
std::string describeMidi (const uint8_t* data, size_t size);

// True for clock, active sensing and other real-time bytes the monitor can hide.
bool isRealtime (const uint8_t* data, size_t size);
} // namespace peak
