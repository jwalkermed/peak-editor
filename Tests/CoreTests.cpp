// Minimal self-contained tests for the JUCE-free core. Run: ./build/PeakCoreTests
#include "peak/core/MidiDescribe.h"
#include "peak/core/Nrpn.h"
#include "peak/core/Sysex.h"

#include <cstdio>
#include <functional>

namespace
{
int failures = 0;

#define CHECK(cond) \
    do { if (! (cond)) { std::printf ("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

void test (const char* name, const std::function<void()>& body)
{
    const auto before = failures;
    body();
    std::printf ("%s %s\n", failures == before ? "ok  " : "FAIL", name);
}
} // namespace

int main()
{
    using namespace peak;

    test ("splitSysex separates messages and drops junk", [] {
        const Bytes stream { 0x01, 0xF0, 0x00, 0x20, 0x29, 0xF7, 0x55, 0xF0, 0x7E, 0xF8, 0x01, 0xF7, 0xF0, 0x01 };
        const auto msgs = splitSysex (stream);
        CHECK (msgs.size() == 2);
        CHECK ((msgs[0] == Bytes { 0xF0, 0x00, 0x20, 0x29, 0xF7 }));
        CHECK ((msgs[1] == Bytes { 0xF0, 0x7E, 0x01, 0xF7 })); // interleaved clock removed
    });

    test ("splitSysex aborts on a non-realtime status byte", [] {
        const Bytes stream { 0xF0, 0x01, 0x90, 0xF0, 0x02, 0xF7 };
        const auto msgs = splitSysex (stream);
        CHECK (msgs.size() == 1);
        CHECK ((msgs[0] == Bytes { 0xF0, 0x02, 0xF7 }));
    });

    test ("joinSysex round-trips", [] {
        const std::vector<Bytes> msgs { { 0xF0, 0x01, 0xF7 }, { 0xF0, 0x02, 0x03, 0xF7 } };
        CHECK (splitSysex (joinSysex (msgs)) == msgs);
    });

    test ("hex parse/format", [] {
        CHECK ((parseHex ("F0 00 20 29 F7") == Bytes { 0xF0, 0x00, 0x20, 0x29, 0xF7 }));
        CHECK ((parseHex ("f0,00,20") == Bytes { 0xF0, 0x00, 0x20 }));
        CHECK ((parseHex ("F00020") == Bytes { 0xF0, 0x00, 0x20 }));
        CHECK (! parseHex ("F0 0"));
        CHECK (! parseHex ("F0 0G"));
        const Bytes b { 0xF0, 0x0A, 0xF7 };
        CHECK (toHex (b.data(), b.size()) == "F0 0A F7");
        CHECK (toHex (b.data(), b.size(), 2) == "F0 0A …");
    });

    test ("Novation ID detection", [] {
        const Bytes yes { 0xF0, 0x00, 0x20, 0x29, 0x01, 0xF7 };
        const Bytes no { 0xF0, 0x41, 0x10, 0xF7 };
        CHECK (isNovationSysex (yes.data(), yes.size()));
        CHECK (! isNovationSysex (no.data(), no.size()));
    });

    test ("NRPN encode", [] {
        const auto msgs = encodeNrpn (2, 300, 64, 5);
        CHECK (msgs.size() == 4);
        CHECK ((msgs[0] == ShortMessage { 0xB2, 99, 2 }));
        CHECK ((msgs[1] == ShortMessage { 0xB2, 98, 44 }));
        CHECK ((msgs[2] == ShortMessage { 0xB2, 6, 64 }));
        CHECK ((msgs[3] == ShortMessage { 0xB2, 38, 5 }));
        CHECK (encodeNrpn (0, 1, 1).size() == 3);
    });

    test ("NRPN decode round-trip and channel isolation", [] {
        NrpnDecoder dec;
        std::optional<NrpnEvent> last;
        for (const auto& m : encodeNrpn (2, 300, 64, 5))
            if (auto e = dec.feed (m[0], m[1], m[2]))
                last = e;
        CHECK ((last == NrpnEvent { 2, 300, 64, 5 }));

        // Data entry on another channel without a selected NRPN does nothing.
        CHECK (! dec.feed (0xB3, 6, 10));
        // RPN selection disables NRPN data entry on that channel.
        dec.feed (0xB2, 101, 0);
        CHECK (! dec.feed (0xB2, 6, 10));
    });

    test ("describeMidi", [] {
        const uint8_t cc[] { 0xB0, 74, 64 };
        CHECK (describeMidi (cc, 3) == "CC        ch 1  #74 = 64");
        const uint8_t clk[] { 0xF8 };
        CHECK (isRealtime (clk, 1));
        CHECK (describeMidi (clk, 1) == "Clock");
    });

    std::printf (failures == 0 ? "\nAll tests passed\n" : "\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
