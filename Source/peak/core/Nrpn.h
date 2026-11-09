#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace peak
{
// A 3-byte channel message.
using ShortMessage = std::array<uint8_t, 3>;

struct NrpnEvent
{
    int channel = 0;   // 0-15
    int param = 0;     // 0-16383 (CC 99 << 7 | CC 98)
    int dataMsb = 0;   // CC 6
    int dataLsb = -1;  // CC 38, or -1 if not (yet) received

    bool operator== (const NrpnEvent&) const = default;
};

// Builds CC 99, 98, 6 and optionally 38. Whether the Peak expects the data LSB
// must come from the User Guide's NRPN chart, not from this function.
std::vector<ShortMessage> encodeNrpn (int channel, int param, int dataMsb, int dataLsb = -1);

// Reassembles NRPNs from an incoming CC stream, tracking each channel separately.
class NrpnDecoder
{
public:
    // Returns an event when CC 6 (coarse) or CC 38 (fine) completes one.
    std::optional<NrpnEvent> feed (uint8_t status, uint8_t data1, uint8_t data2);

private:
    struct ChannelState
    {
        int paramMsb = -1, paramLsb = -1, dataMsb = -1;
        bool rpnSelected = false;
    };

    std::array<ChannelState, 16> channels {};
};
} // namespace peak
