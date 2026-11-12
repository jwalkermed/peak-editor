// Minimal self-contained tests for the JUCE-free core. Run: ./build/PeakCoreTests
#include "peak/core/MidiDescribe.h"
#include "peak/core/Nrpn.h"
#include "peak/core/ParamFormat.h"
#include "peak/core/ParamMidi.h"
#include "peak/core/Patch.h"
#include "peak/core/Settings.h"
#include "peak/core/WavetableInfo.h"
#include "peak/core/Wavetable.h"
#include "peak/core/Sysex.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <set>
#include <string>

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

peak::Bytes readFixture (const std::string& name)
{
    std::ifstream in (std::string (PEAK_FIXTURE_DIR) + "/" + name, std::ios::binary);
    return { std::istreambuf_iterator<char> (in), {} };
}

struct Expectation
{
    const char* file;
    const char* param;
    int value;
};

const Expectation mappingExpectations[] {
#include "MappingExpectations.inc"
};

const char* const dumpFixtures[] { "current-all-in-sync.syx",
                                   "mapping/run1-dump0.syx", "mapping/run1-dump1.syx", "mapping/run1-dump2.syx",
                                   "mapping/run1-dump3.syx", "mapping/run1-dump4.syx",
                                   "mapping/run2-dump0.syx", "mapping/run2-dump1.syx", "mapping/run2-dump2.syx",
                                   "mapping/run3-dump0.syx", "mapping/run3-dump1.syx", "mapping/run3-dump2.syx", "mapping/run3-dump3.syx" };
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

    test ("parameter table is consistent", [] {
        std::set<std::string> ids;
        std::set<int> ccs, nrpns, bytes;
        for (const auto& p : allParams())
        {
            CHECK (ids.insert (p.id).second);
            CHECK (p.min <= p.max && p.min >= 0 && p.max <= (p.kind == MidiKind::ccPair ? 255 : 127));

            if (! p.hasMidi())
                CHECK (p.isStored()); // Clock Rate: only reachable through the patch dump
            else if (p.kind == MidiKind::nrpn)
                CHECK (nrpns.insert (p.number).second);
            else
                CHECK (ccs.insert (p.number).second);

            if (p.kind == MidiKind::ccPair && p.hasMidi())
                CHECK (ccs.insert (p.number + 32).second);

            if (p.isStored())
            {
                CHECK (p.offset >= 32 && p.offset < (int) Patch::dumpSize - 2);
                CHECK (bytes.insert (p.offset).second);
                if (p.kind == MidiKind::ccPair)
                    CHECK (bytes.insert (p.offset + 1).second);
            }
        }
        // NRPN plumbing CCs must never be parameters (CC 38 is one on the Peak, so it is not used as data LSB).
        for (int cc : { 6, 98, 99, 100, 101 })
            CHECK (ccs.count (cc) == 0);
        CHECK (allParams().size() > 240);
    });

    test ("every hardware dump decodes and re-encodes byte-identically", [] {
        for (const auto* f : dumpFixtures)
        {
            const auto bytes = readFixture (f);
            const auto patch = Patch::fromSysex (bytes);
            CHECK (patch.has_value());
            if (patch)
                CHECK (patch->toSysex() == bytes);
        }
        CHECK (Patch::fromSysex (readFixture ("current-all-in-sync.syx"))->name() == "All In Sync");
    });

    test ("decoded values match what was sent during hardware mapping", [] {
        int checked = 0;
        std::string lastFile;
        std::optional<Patch> patch;
        for (const auto& e : mappingExpectations)
        {
            if (lastFile != e.file)
            {
                lastFile = e.file;
                patch = Patch::fromSysex (readFixture (std::string ("mapping/") + e.file));
                CHECK (patch.has_value());
            }
            const auto* p = findParam (e.param);
            CHECK (p != nullptr);
            if (p && patch)
            {
                const auto got = patch->get (*p);
                if (got != e.value)
                    std::printf ("  %s %s: expected %d got %d\n", e.file, e.param, e.value, got);
                CHECK (got == e.value);
                ++checked;
            }
        }
        CHECK (checked > 1000);
    });

    test ("set changes only the parameter's own bytes", [] {
        const auto original = *Patch::fromSysex (readFixture ("current-all-in-sync.syx"));
        for (const auto& p : allParams())
        {
            if (! p.isStored())
                continue;
            for (int v : { p.min, p.max, (p.min + p.max) / 2 | 1 })
            {
                auto copy = original;
                copy.set (p, v);
                CHECK (copy.get (p) == std::clamp (v, p.min, p.max));
                for (size_t i = 0; i < Patch::dumpSize; ++i)
                    if (copy.bytes()[i] != original.bytes()[i])
                        CHECK (i == (size_t) p.offset || (p.kind == MidiKind::ccPair && i == (size_t) p.offset + 1));
            }
        }
        auto copy = original;
        copy.set (*findParam ("filter_frequency"), 999);
        CHECK (copy.get (*findParam ("filter_frequency")) == 255);
    });

    test ("patch names", [] {
        Patch p;
        p.setName ("Bass\x01Line 2 with a long name");
        CHECK (p.name() == "Bass Line 2 with");
        p.setName ("Pad");
        CHECK (p.name() == "Pad");
        CHECK (Patch::fromSysex (p.toSysex()).has_value());
    });

    test ("patch -> CC/NRPN stream -> decoder reproduces every stored value", [] {
        for (const auto* f : dumpFixtures)
        {
            const auto source = *Patch::fromSysex (readFixture (f));
            Patch rebuilt;
            ParamMidiDecoder dec;
            for (const auto& m : encodePatch (source, 3))
                if (const auto c = dec.feed (3, m[0], m[1], m[2]))
                    rebuilt.set (*c->param, c->value);

            for (const auto& p : allParams())
                if (p.isStored() && p.hasMidi())
                    CHECK (rebuilt.get (p) == source.get (p));
        }
    });

    test ("patch stream sends Reverb Type before Reverb Size", [] {
        const auto msgs = encodePatch (Patch(), 0);
        const auto type = findParam ("reverb_type")->number, size = findParam ("reverb_size")->number;
        int typeAt = -1, sizeAt = -1;
        for (size_t i = 0; i + 2 < msgs.size(); ++i)
            if (msgs[i][1] == 99 && msgs[i + 1][1] == 98)
            {
                const int n = (msgs[i][2] << 7) | msgs[i + 1][2];
                if (n == type) typeAt = (int) i;
                if (n == size) sizeAt = (int) i;
            }
        CHECK (typeAt >= 0 && sizeAt > typeAt);
    });

    test ("decoder: pairs complete on LSB, CC 38 is a parameter, other channels ignored", [] {
        ParamMidiDecoder dec;
        CHECK (! dec.feed (0, 0xB0, 14, 5));
        const auto c = dec.feed (0, 0xB0, 46, 64);
        CHECK (c && c->param == findParam ("osc1_coarse") && c->value == 11);
        const auto cc38 = dec.feed (0, 0xB0, 38, 70);
        CHECK (cc38 && cc38->param == findParam ("osc2_modenv2_to_pitch") && cc38->value == 70);
        CHECK (! dec.feed (0, 0xB1, 5, 10));
        // A real Peak NRPN from the capture: 0:121 (Arp On) = 1
        dec.feed (0, 0xB0, 99, 0);
        dec.feed (0, 0xB0, 98, 121);
        const auto n = dec.feed (0, 0xB0, 6, 1);
        CHECK (n && n->param == findParam ("arp_on") && n->value == 1);
        // Animate buttons: the Peak sends 127 pressed, 0 released (capture 2026-11-10), and expects the same.
        const auto a = dec.feed (0, 0xB0, 114, 127);
        CHECK (a && a->param == findParam ("animate1_hold") && a->value == 1);
        const auto pressed = encodeParam (*findParam ("animate2_hold"), 1, 0);
        CHECK (pressed.size() == 1 && pressed[0][1] == 115 && pressed[0][2] == 127);
        CHECK (encodeParam (*findParam ("animate2_hold"), 0, 0)[0][2] == 0);
    });

    test ("sameSound: Arp Chance 0 (stored) equals 100 (current sound)", [] {
        auto bytes = readFixture ("current-all-in-sync.syx");
        const auto a = *Patch::fromSysex (bytes);
        bytes[(size_t) findParam ("arp_chance")->offset] = 0; // as in the stored banks (below the 1-100 range)
        auto b = *Patch::fromSysex (bytes);
        CHECK (a.sameSound (b) && b.sameSound (a));
        b.set (*findParam ("arp_chance"), 50);
        CHECK (! a.sameSound (b));
        auto c = a;
        c.set (*findParam ("filter_env_select"), 1 - a.get (*findParam ("filter_env_select")));
        CHECK (a.sameSound (c)); // which depth the Env Depth knob shows isn't an edit
    });

    test ("Clock Rate: in the patch at 0xFB/0xFC, no MIDI (read from the Peak 2026-11-10)", [] {
        const auto& rate = *findParam ("clock_rate");
        CHECK (! rate.hasMidi() && encodeParam (rate, 97, 0).empty());
        CHECK (Patch::fromSysex (readFixture ("current-init-patch.syx"))->get (rate) == 120);
        CHECK (formatValue (rate, 97) == "97 BPM");
    });

    test ("user wavetables: read, rebuilt and sent back exactly (fixtures from the Peak 2026-11-11)", [] {
        const auto noisia = splitSysex (readFixture ("wavetables/user1-noisia.syx"));
        const auto table = Wavetable::fromSysex (noisia);
        CHECK (table.has_value());
        // Re-encoded for slot 0, the 5 wave messages are byte for byte what the Peak sent.
        const auto out = table->toSysex (0);
        CHECK (out.size() == 6 && out[0].size() == 23);
        for (int w = 0; w < 5; ++w)
            CHECK (out[(size_t) w + 1] == noisia[(size_t) w]);
        // An empty slot is a sine in every wave.
        const auto empty = Wavetable::fromSysex (splitSysex (readFixture ("wavetables/user2-empty.syx")));
        CHECK (empty && std::abs (empty->waves[3].sine[1] - 1.0f) < 1.0e-3f && empty->waves[3].level (2) < 1.0e-6f);
        // The layout (as Components writes it): DC, then for each harmonic its cosine part (negated), then its sine.
        Bytes raw (noisia[0]);
        std::fill (raw.begin() + 16, raw.end() - 1, 0);
        raw[16 + 2] = 0x20; // value 1: harmonic 1's cosine part, +0.5
        const auto cos1 = parseWavetableData (raw);
        CHECK (cos1 && std::abs (cos1->second.cosine[1] + 0.5f) < 1.0e-3f && cos1->second.level (2) == 0.0f);
        CHECK (toHex (wavetableDataRequest (9, 4).data(), 15) == "F0 00 20 29 01 10 00 7E 46 00 00 09 04 00 F7");
        // The name message is what the Peak accepted for User 10.
        Wavetable t;
        t.name = "SineSaw";
        CHECK (toHex (t.toSysex (9)[0].data(), 23) == "F0 00 20 29 01 10 00 7E 07 00 00 00 00 00 09 53 69 6E 65 53 61 77 F7");
    });

    test ("wavetable shapes and operations", [] {
        // samples -> harmonics -> samples gives the drawing back (band-limited)
        const auto saw = Shapes::make (Shapes::saw);
        const auto again = Wave::fromSamples (saw.samples());
        for (int h = 1; h <= 20; ++h)
            CHECK (std::abs (again.sine[(size_t) h] - saw.sine[(size_t) h]) < 1.0e-3f);
        // the sawtooth rises then drops (checked on an oscilloscope): just before mid-cycle it's at its top
        const auto s = saw.samples();
        CHECK (s[120] > 0.4f && s[136] < -0.4f);
        CHECK (WaveOps::invert (saw).sine[1] == -saw.sine[1]);
        CHECK (WaveOps::reverse (WaveOps::reverse (saw)) == saw);
        const auto loud = WaveOps::normalise (Shapes::make (Shapes::triangle));
        float peak = 0;
        for (auto v : loud.samples (1024)) peak = std::max (peak, std::abs (v));
        CHECK (std::abs (peak - 1.0f) < 0.01f);
        CHECK (WaveOps::blend (Shapes::make (Shapes::sine), saw, 0.5f).sine[2] == saw.sine[2] * 0.5f);
        CHECK (WaveOps::smooth (saw, 1.0f).level (60) < saw.level (60) * 0.1f);
        // A drawing comes back exactly as drawn at 256 points, offset included (a shape in the top half only).
        std::vector<float> drawn (256, 0.0f);
        for (size_t i = 0; i < 64; ++i)
            drawn[i] = 0.5f + 0.5f * (i < 32 ? 1.0f : -1.0f);
        const auto back = Wave::fromSamples (drawn).samples();
        float worst = 0;
        for (size_t i = 0; i < 256; ++i) worst = std::max (worst, std::abs (back[i] - drawn[i]));
        CHECK (worst < 1.0e-4f);
        // A full-height square is scaled down when sent, so its first harmonic (4/pi) isn't clipped.
        Wavetable sq;
        sq.waves[0] = Shapes::make (Shapes::square);
        const auto first = parseWavetableData (sq.toSysex (0)[1]);
        CHECK (first && std::abs (first->second.sine[1] - 1.0f) < 1.0e-3f && std::abs (first->second.sine[3] - 1.0f / 3.0f) < 2.0e-3f);
    });

    test ("wavetable notes: one for each factory wavetable, none for the user ones", [] {
        for (int v = 4; v <= 63; ++v)
            CHECK (wavetableInfo (v) != nullptr && *wavetableInfo (v)->description != 0 && *wavetableInfo (v)->usage != 0);
        CHECK (wavetableInfo (3) == nullptr && wavetableInfo (64) == nullptr);
        CHECK (std::string (wavetableInfo (37)->description).find ("reverse version of \"Glassy\"") != std::string::npos); // Grow
        CHECK (std::string (wavetableInfo (63)->description) == "Sounds like what the name suggests!");                   // Yeah
    });

    test ("wavetable names: request 0x47, reply type 07 (verified on the Peak 2026-11-11)", [] {
        CHECK (toHex (wavetableNameRequest (64).data(), 15) == "F0 00 20 29 01 10 00 7E 47 00 00 40 00 00 F7");
        const auto reply = *parseHex ("F0 00 20 29 01 10 00 7E 07 00 00 00 00 00 40 4E 6F 69 73 69 61 20 F7"); // "Noisia "
        const auto name = parseWavetableName (reply);
        CHECK (name && name->first == 64 && name->second == "Noisia");
        CHECK (! parseWavetableName (readFixture ("current-all-in-sync.syx")));
        CHECK (formatValue (*findParam ("osc1_wave_more"), 37) == "Grow");
        userWavetableNames()[0] = "Noisia";
        CHECK (formatValue (*findParam ("osc2_wave_more"), 64) == "Noisia");
        CHECK (formatValue (*findParam ("osc2_wave_more"), 73) == "User 10");
        userWavetableNames()[0] = "User 1";
    });

    test ("dump requests (verified on the Peak 2026-11-10)", [] {
        CHECK (toHex (currentSoundRequest().data(), currentSoundRequest().size()) == "F0 00 20 29 01 10 00 7E 40 00 00 00 00 00 F7");
        CHECK (slotRequest (1, 63)[8] == 0x41 && slotRequest (1, 63)[12] == 2 && slotRequest (1, 63)[13] == 63);
        CHECK (settingsRequest()[8] == 0x44);
    });

    test ("bank dumps carry slot locations; edit-buffer dumps do not", [] {
        for (const auto& [file, bank] : { std::pair { "bank-a.syx", 0 }, std::pair { "bank-b.syx", 1 }, std::pair { "bank-c.syx", 2 }, std::pair { "bank-d.syx", 3 } })
        {
            const auto msgs = splitSysex (readFixture (file));
            CHECK (msgs.size() == 128);
            for (size_t i = 0; i < msgs.size(); ++i)
            {
                const auto p = Patch::fromSysex (msgs[i]);
                CHECK (p && p->location() == (Patch::Location { bank, (int) i }));
            }
        }
        const auto a = splitSysex (readFixture ("bank-a.syx"));
        CHECK (Patch::fromSysex (a[0])->name() == "Utopian Streams");
        CHECK (Patch::fromSysex (a[127])->location()->name() == "A127");

        const auto current = *Patch::fromSysex (readFixture ("current-tardis-edited.syx"));
        CHECK (! current.location().has_value());

        // Moving a patch between slots / the edit buffer touches only the header.
        auto moved = *Patch::fromSysex (a[1]);
        moved.setLocation (Patch::Location { 3, 99 });
        CHECK (moved.location()->name() == "D099");
        CHECK (moved.body() == Patch::fromSysex (a[1])->body());
        moved.setLocation (std::nullopt);
        CHECK (! moved.location().has_value());
        const auto edited = current.toSysex(), stored = moved.toSysex();
        for (size_t i = 0; i < 16; ++i)
            CHECK (edited[i] == stored[i]); // same header as a real Current dump
    });

    test ("captured Init Patch is a valid current-sound dump", [] {
        const auto init = Patch::fromSysex (readFixture ("current-init-patch.syx"));
        CHECK (init.has_value() && init->name() == "Init Patch" && ! init->location().has_value());
        CHECK (init->get (*findParam ("patch_category")) == 0);
    });

    test ("FX mod slots have a Source B (second byte, NRPN n:1)", [] {
        const auto p = *Patch::fromSysex (readFixture ("current-fx-sourceb.syx"));
        CHECK (p.get (*findParam ("fxmod2_source")) == 13);
        CHECK (p.get (*findParam ("fxmod2_source2")) == 16);
        CHECK (findParam ("fxmod2_source2")->number == ((18 << 7) | 1));
        CHECK (formatValue (*findParam ("fxmod2_source2"), 16) == "Noise");
    });

    test ("moving within Peak memory swaps instead of overwriting", [] {
        using L = Patch::Location;
        const auto applied = [] (std::vector<std::string> slots, const std::vector<L>& src, const std::vector<L>& dst) {
            const auto before = slots;
            for (const auto& [to, from] : Patch::planSwap (src, dst))
                slots[(size_t) to.slot] = before[(size_t) from.slot];
            return slots;
        };
        const std::vector<std::string> bank { "a", "b", "c", "d", "e", "f" };
        // One patch: a plain swap.
        CHECK ((applied (bank, { { 0, 1 } }, { { 0, 4 } }) == std::vector<std::string> { "a", "e", "c", "d", "b", "f" }));
        // A block onto a block that doesn't overlap: pairwise swap.
        CHECK ((applied (bank, { { 0, 0 }, { 0, 1 } }, { { 0, 3 }, { 0, 4 } }) == std::vector<std::string> { "d", "e", "c", "a", "b", "f" }));
        // Overlapping: shift a block down one; the displaced patch moves into the gap.
        CHECK ((applied (bank, { { 0, 0 }, { 0, 1 }, { 0, 2 } }, { { 0, 1 }, { 0, 2 }, { 0, 3 } }) == std::vector<std::string> { "d", "a", "b", "c", "e", "f" }));
        // Nothing is lost or duplicated.
        auto shuffled = applied (bank, { { 0, 2 }, { 0, 5 } }, { { 0, 0 }, { 0, 1 } });
        std::sort (shuffled.begin(), shuffled.end());
        CHECK (shuffled == bank);
    });

    test ("settings dump: every value matches the Peak's display (photos, 2026-11-10)", [] {
        const auto bytes = readFixture ("settings-dump.syx");
        const auto d = SettingsDump::fromSysex (bytes);
        CHECK (d.has_value());
        CHECK (! Patch::fromSysex (bytes).has_value()); // same size as a patch dump, but not one
        const auto show = [&] (const char* id) { const auto* s = findSetting (id); return formatSetting (*s, d->get (*s)); };
        CHECK (show ("protect") == "Off");
        CHECK (show ("brightness") == "64");
        CHECK (show ("msg_time") == "64");
        CHECK (show ("tune_cents") == "0");
        CHECK (show ("transpose") == "0");
        CHECK (show ("midi_chan") == "1");
        CHECK (show ("arp_to_midi") == "On");
        CHECK (show ("cc_nrpn") == "Rec+Tran");
        CHECK (show ("bank_patch") == "Rec+Tran");
        CHECK (show ("ped1_sense") == "Auto");
        CHECK (show ("ped2_mode") == "Animate2");
        CHECK (show ("vol_range") == "-3dB");
        CHECK (show ("initialise") == "IniPatch");
        CHECK (show ("tuning_mode") == "Note");
        CHECK (formatSetting (*findSetting ("tuning_mode"), 1) == "Octave");
        CHECK (show ("patch_cue") == "Off");
        CHECK (formatSetting (*findSetting ("transpose"), 76) == "+12");
        CHECK (! SettingsDump::fromSysex (readFixture ("current-all-in-sync.syx")).has_value());
        std::set<int> numbers;
        for (const auto& s : allSettings())
            CHECK (numbers.insert (s.nrpn).second && s.nrpn != 7 && s.nrpn != 8); // Backup Select / Send To stay off the tab
    });

    test ("display formatting", [] {
        const auto f = [] (const char* id, int v) { return formatValue (*findParam (id), v); };
        CHECK (f ("osc1_range", 63) == "16'");
        CHECK (f ("osc1_range", 66) == "2'");
        CHECK (f ("osc1_coarse", 128) == "0.0" && f ("osc1_coarse", 127) == "0.0");
        CHECK (f ("osc1_coarse", 140) == "+1.2");
        // As the Peak showed them (read 2026-11-12 by turning its Coarse knob).
        for (const auto& [raw, shown] : std::vector<std::pair<int, const char*>> {
                 { 129, "+0.1" }, { 133, "+0.5" }, { 138, "+1.0" }, { 149, "+2.0" }, { 160, "+3.0" }, { 224, "+9.0" }, { 245, "+11.0" },
                 { 254, "+11.9" }, { 126, "-0.1" }, { 117, "-1.0" }, { 95, "-3.0" }, { 74, "-5.0" }, { 53, "-7.0" }, { 31, "-9.0" }, { 10, "-11.0" } })
            CHECK (f ("osc1_coarse", raw) == shown);
        CHECK (f ("osc1_coarse", 0) == "-12.0" && f ("osc1_coarse", 255) == "+12.0" && f ("osc2_coarse", 192) == "+6.0");
        CHECK (f ("osc2_fine", 28) == "-100");
        CHECK (f ("osc3_wave", 2) == "Sawtooth");
        CHECK (f ("osc1_modenv1_to_shape", 0) == "-64");
        CHECK (f ("pre_glide", 64) == "Off");
        CHECK (f ("osc1_bend_range", 76) == "+12");
        CHECK (f ("glide_on", 1) == "On");
        CHECK (f ("lfo1_phase", 0) == "Free");
        // Named lists, read back from the Peak's screen (2026-11-10) or fixed by Init Patch values.
        CHECK (f ("arp_sync_rate", 3) == "16th");
        CHECK (f ("delay_sync_time", 7) == "4th T");
        CHECK (f ("delay_sync_time", 27) == "24 beats");
        CHECK (f ("lfo1_sync_rate", 0) == "64 beats");
        CHECK (f ("lfo2_sync_rate", 34) == "32nd T");
        CHECK (f ("lfo3_ratesync", 1) == "64 beats");
        CHECK (f ("lfo4_ratesync", 17) == "8 beats");
        CHECK (f ("osc1_wave_more", 5) == "Random");
        CHECK (f ("osc2_wave_more", 19) == "String");
        CHECK (f ("osc3_wave_more", 63) == "Yeah");
        CHECK (f ("voice_mode", 3) == "Poly");
        CHECK (f ("voice_unison", 4) == "8");
        CHECK (valueNames (*findParam ("fx_routing")).size() == 7);
        CHECK (valueNames (*findParam ("filter_resonance")).empty());
        CHECK (f ("lfo1_phase", 61) == "180\u00b0");
        CHECK (f ("filter_shape", 1) == "BP");
        CHECK (f ("filter_frequency", 200) == "200");
        CHECK (categoryNames().size() == 15); // matches the parameter's 0-14 range
        CHECK (modSourceNames().size() == 24 && modDestinationNames().size() == 48);
        CHECK (fxSourceNames().size() == 17 && fxDestinationNames().size() == 24);
        CHECK (f ("mod1_source1", 14) == "Animate 1");
        CHECK (f ("mod3_dest", 18) == "Filter Freq");
        CHECK (f ("mod3_dest", 37) == "Pan Position");
        CHECK (f ("mod3_dest", 47) == "Mod Env 2 Sustain");
        CHECK (f ("mod1_source2", 23) == "Noise");
        CHECK (f ("fxmod2_dest", 9) == "Reverb Time");
        CHECK (f ("fxmod2_dest", 10) == "Reverb Low Damp");
        CHECK (f ("fxmod2_dest", 15) == "Delay Slew");
        CHECK (f ("fxmod2_dest", 23) == "Noise HPF");
        CHECK (f ("mod1_depth", 64) == "0");
        for (const auto& p : allParams()) // each mod list names every value its parameter can take
            if (std::string (p.section).find ("Mod Matrix") != std::string::npos && std::string (p.id).find ("depth") == std::string::npos)
                CHECK (f (p.id, p.max) != std::to_string (p.max));
        CHECK (f ("patch_category", 0) == "None");
        CHECK (f ("patch_category", 8) == "Motion");
        CHECK (f ("patch_category", 14) == "User 2");
        // Factory bank A agrees: A009 Logarhythm = Arp, A004 Major Bass = Bass, A000 Utopian Streams = Pad.
        const auto bank = splitSysex (readFixture ("bank-a.syx"));
        const auto cat = [&] (int slot) { return f ("patch_category", Patch::fromSysex (bank[(size_t) slot])->get (*findParam ("patch_category"))); };
        CHECK (cat (9) == "Arp");
        CHECK (cat (4) == "Bass");
        CHECK (cat (0) == "Pad");
        CHECK (cat (3) == "Poly");
        CHECK (defaultValue (*findParam ("osc1_coarse")) == 128);
        CHECK (defaultValue (*findParam ("filter_resonance")) == 0);
        for (const auto& p : allParams())
            for (int v = p.min; v <= p.max; ++v)
                CHECK (! formatValue (p, v).empty());
    });

    std::printf (failures == 0 ? "\nAll tests passed\n" : "\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
