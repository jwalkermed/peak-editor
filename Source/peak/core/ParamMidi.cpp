#include "ParamMidi.h"

#include "Patch.h"

#include <algorithm>
#include <array>

namespace peak
{
namespace
{
struct Lookup
{
    std::array<const ParamDef*, 128> byCc {};    // plain CCs and pair MSBs
    std::array<const ParamDef*, 128> byLsbCc {}; // pair LSBs (MSB + 32)
    std::vector<std::pair<int, const ParamDef*>> byNrpn;

    Lookup()
    {
        for (const auto& p : allParams())
        {
            if (p.kind == MidiKind::nrpn)
                byNrpn.emplace_back (p.number, &p);
            else
                byCc[(size_t) p.number] = &p;

            if (p.kind == MidiKind::ccPair)
                byLsbCc[(size_t) p.number + 32] = &p;
        }
        std::sort (byNrpn.begin(), byNrpn.end());
    }

    const ParamDef* nrpn (int number) const
    {
        const auto it = std::lower_bound (byNrpn.begin(), byNrpn.end(), std::make_pair (number, (const ParamDef*) nullptr));
        return it != byNrpn.end() && it->first == number ? it->second : nullptr;
    }
};

const Lookup& lookup()
{
    static const Lookup l;
    return l;
}
} // namespace

std::vector<ShortMessage> encodeParam (const ParamDef& def, int value, int channel)
{
    value = std::clamp (value, def.min, def.max);
    const auto status = static_cast<uint8_t> (0xB0 | (channel & 0x0F));

    switch (def.kind)
    {
        case MidiKind::cc:
            return { { status, (uint8_t) def.number, (uint8_t) value } };
        case MidiKind::ccPair:
            return { { status, (uint8_t) def.number, (uint8_t) (value >> 1) },
                     { status, (uint8_t) (def.number + 32), (uint8_t) ((value & 1) << 6) } };
        case MidiKind::nrpn:
            return encodeNrpn (channel, def.number, value);
    }

    return {};
}

std::vector<ShortMessage> encodePatch (const Patch& patch, int channel)
{
    std::vector<const ParamDef*> stored;
    for (const auto& p : allParams())
        if (p.isStored())
            stored.push_back (&p);

    std::sort (stored.begin(), stored.end(), [] (auto* a, auto* b) { return a->offset < b->offset; });

    std::vector<ShortMessage> out;
    for (auto* p : stored)
    {
        const auto msgs = encodeParam (*p, patch.get (*p), channel);
        out.insert (out.end(), msgs.begin(), msgs.end());
    }
    return out;
}

std::optional<ParamChange> ParamMidiDecoder::feed (int channel, uint8_t status, uint8_t data1, uint8_t data2)
{
    if (! initialised)
    {
        std::fill (std::begin (pendingMsb), std::end (pendingMsb), -1);
        initialised = true;
    }

    if (status != (0xB0 | (channel & 0x0F)) || data1 > 127)
        return std::nullopt;

    const auto& l = lookup();

    if (data1 == 99 || data1 == 98 || data1 == 6 || data1 == 101 || data1 == 100)
    {
        if (const auto e = nrpn.feed (status, data1, data2))
            return ParamChange { l.nrpn (e->param), e->dataMsb, e->param };
        return std::nullopt;
    }

    if (const auto* p = l.byLsbCc[data1])
    {
        const auto msb = pendingMsb[p->number];
        pendingMsb[p->number] = -1;
        if (msb >= 0)
            return ParamChange { p, (msb << 1) | (data2 >> 6) };
        return std::nullopt;
    }

    if (const auto* p = l.byCc[data1])
    {
        if (p->kind == MidiKind::ccPair)
        {
            pendingMsb[data1] = data2; // completed by the LSB, which the Peak always sends next
            return std::nullopt;
        }
        return ParamChange { p, data2 };
    }

    return std::nullopt;
}
} // namespace peak
