#include "MidiDescribe.h"

#include "Sysex.h"

namespace peak
{
namespace
{
std::string ch (uint8_t status) { return "ch " + std::to_string ((status & 0x0F) + 1); }
}

bool isRealtime (const uint8_t* data, size_t size)
{
    return size == 1 && data[0] >= 0xF8;
}

std::string describeMidi (const uint8_t* data, size_t size)
{
    if (size == 0)
        return "(empty)";

    const auto status = data[0];
    const auto d1 = size > 1 ? data[1] : 0;
    const auto d2 = size > 2 ? data[2] : 0;

    switch (status & 0xF0)
    {
        case 0x80: return "Note Off  " + ch (status) + "  note " + std::to_string (d1) + "  vel " + std::to_string (d2);
        case 0x90: return (d2 == 0 ? "Note Off  " : "Note On   ") + ch (status) + "  note " + std::to_string (d1) + "  vel " + std::to_string (d2);
        case 0xA0: return "Poly AT   " + ch (status) + "  note " + std::to_string (d1) + "  " + std::to_string (d2);
        case 0xB0: return "CC        " + ch (status) + "  #" + std::to_string (d1) + " = " + std::to_string (d2);
        case 0xC0: return "Program   " + ch (status) + "  " + std::to_string (d1);
        case 0xD0: return "Chan AT   " + ch (status) + "  " + std::to_string (d1);
        case 0xE0: return "Pitch Bend " + ch (status) + "  " + std::to_string (((d2 << 7) | d1) - 8192);
        default: break;
    }

    switch (status)
    {
        case 0xF0:
            return "SysEx     " + std::to_string (size) + " bytes"
                 + (isNovationSysex (data, size) ? " (Novation)  " : "  ")
                 + toHex (data, size, 24);
        case 0xF2: return "Song Position " + std::to_string ((d2 << 7) | d1);
        case 0xF3: return "Song Select " + std::to_string (d1);
        case 0xF8: return "Clock";
        case 0xFA: return "Start";
        case 0xFB: return "Continue";
        case 0xFC: return "Stop";
        case 0xFE: return "Active Sensing";
        case 0xFF: return "Reset";
        default:   return "Other     " + toHex (data, size, 16);
    }
}
} // namespace peak
