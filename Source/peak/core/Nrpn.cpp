#include "Nrpn.h"

#include <algorithm>

namespace peak
{
namespace
{
uint8_t clamp7 (int v) { return static_cast<uint8_t> (std::clamp (v, 0, 127)); }
}

std::vector<ShortMessage> encodeNrpn (int channel, int param, int dataMsb, int dataLsb)
{
    const auto status = static_cast<uint8_t> (0xB0 | (channel & 0x0F));
    param = std::clamp (param, 0, 16383);

    std::vector<ShortMessage> out {
        { status, 99, static_cast<uint8_t> (param >> 7) },
        { status, 98, static_cast<uint8_t> (param & 0x7F) },
        { status, 6, clamp7 (dataMsb) },
    };

    if (dataLsb >= 0)
        out.push_back ({ status, 38, clamp7 (dataLsb) });

    return out;
}

std::optional<NrpnEvent> NrpnDecoder::feed (uint8_t status, uint8_t data1, uint8_t data2)
{
    if ((status & 0xF0) != 0xB0)
        return std::nullopt;

    const int ch = status & 0x0F;
    auto& s = channels[(size_t) ch];

    switch (data1)
    {
        case 99: s.paramMsb = data2; s.rpnSelected = false; s.dataMsb = -1; break;
        case 98: s.paramLsb = data2; s.rpnSelected = false; s.dataMsb = -1; break;
        case 101:
        case 100: s.rpnSelected = true; break;

        case 6:
            if (s.rpnSelected || s.paramMsb < 0 || s.paramLsb < 0)
                break;
            s.dataMsb = data2;
            return NrpnEvent { ch, (s.paramMsb << 7) | s.paramLsb, data2, -1 };

        case 38:
            if (s.rpnSelected || s.paramMsb < 0 || s.paramLsb < 0 || s.dataMsb < 0)
                break;
            return NrpnEvent { ch, (s.paramMsb << 7) | s.paramLsb, s.dataMsb, data2 };

        default: break;
    }

    return std::nullopt;
}
} // namespace peak
