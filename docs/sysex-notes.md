# Peak SysEx notes

Running log of what is known about the Peak's SysEx, with where each fact came from.
Unverified items are marked **unverified**.

## Connection (2026-11-09)
- On this rig the Peak is reached over an RTP-MIDI network session named **"Network NovPeak"**, not USB.
- SysEx passes both ways over that session (verified with the identity request below).

## Universal Identity (verified on hardware, 2026-11-09)
Request: `F0 7E 7F 06 01 F7` (standard MIDI, read-only).
Reply:   `F0 7E 7F 06 02 00 20 29 7E 00 00 00 02 01 00 08 F7`

| Bytes | Meaning |
|---|---|
| `00 20 29` | Manufacturer: Novation |
| `7E 00` | Device family (LSB first) |
| `00 00` | Family member |
| `02 01 00 08` | Software revision. The user's Peak runs **firmware 2.2**, so these bytes don't read directly as "2.2". The encoding is **unknown**. |

## From the Peak 1.2 User Guide
- **Settings → Backup page (menu page 9)** sends SysEx from the front panel:
  - `Select`: Current, Bank A–D, A+B+C+D, Settings, ABCD+Set
  - `Send To`: USBport or MIDIout
  - The lower-left soft button `Go` starts the dump.
- Importing a bank dump writes back to the slots it came from: "Patch Banks retain a reference to their original memory location and will be loaded back into that location on import". Treat any received bank dump as **destructive** to re-send.
- **Settings page 5:**
  - `CC/NRPN`: Disabled / Receive / Transmit / Rec+Tran (default Rec+Tran)
  - `Bank/Patch`: Program Change and Bank Change handling
  - `Local`: On/Off
- **"CC pair" parameters** (e.g. Osc 1 Coarse = CC 14,46, range 0–255) are 8-bit values split across an MSB CC and an LSB CC (+32).

## CC pair encoding (verified on hardware, 2026-11-09)
Turning Osc 1 Coarse across its range sent 246 pairs, each CC 14 (MSB) followed by CC 46 (LSB):
- LSB is only ever `0` or `64`.
- `value8 = (msb << 1) | (lsb >> 6)`
- Encode: `msb = value8 >> 1`, `lsb = (value8 & 1) << 6`
- The top of the sweep reached `(127, 64)` = 255.

## Firmware 2.2 (installed on this Peak)
Release notes: https://keyboardchronicles.com/2023/08/13/novation-peak-and-summit-2-2-firmware-release-notes/ . What matters for the editor:
- **The LSB of CC pairs is honoured** (before 2.2 it was ignored). The editor must send both CCs of a pair, MSB then LSB, using the encoding above.
- **Local Control on CC 122** (Peak: 0 = off, 127 = on). Local is now stored in global settings.
- Reverb Size now recalls correctly from patches, and the panel and FX 8/10 menu agree. Captures from 2.2 are the reference.
- CC 120 / CC 123 (all sound off / all notes off) follow the MIDI spec, so they're safe as a panic button.
- LFO FadeSync On/Off works in Mono and Poly. Its NRPN is **unknown**.
- The notes say nothing about SysEx or the patch format. Assume the 2.1 dump layout still applies until a 2.2 capture shows otherwise. (The fixture above is from 2.2.)

## Firmware 2.0/2.1 addendum
The 2.0/2.1 addendum adds NRPNs that the 1.2 chart lacks:
- Pan Posn 0:51, Pan Type 0:52, Spread 0:5
- Chorus mode 0:115, Delay TimeMode 0:98
- Bank 25: tuning table 25:6, FM amounts 25:13/17/21, Arp Chance 25:33, Animate Att/Rel 25:34–37, LFO 3/4 Phase/Slew/Fade 25:38–43
- Patch Cue 64:0 (global)

It also mentions settings with no NRPN listed: delay mode and output, envelope delay, and Noise HPF. Their numbers need capturing.

## Patch dump format
First capture (2026-11-09): Backup page, Select=Current, sent over the network session. Saved as `Tests/fixtures/current-all-in-sync.syx`.

- Total **527 bytes**: `F0 … F7`.
- Header `F0 00 20 29 01 10 00 7E 00 …`. `00 20 29` is Novation. The meaning of `01 10 00` is **unverified**. `7E` matches the identity reply's family byte, but whether that's its meaning here is **unverified**. Which bytes give the bank/slot of a bank dump is **unknown**.
- **Patch name: offset 0x10, 16 ASCII bytes, space-padded** ("All In Sync").
- **Unpacked: one 7-bit value per byte** (verified). 8-bit parameters take two bytes (see the patch map).
- About 0x15E–0x20D is mostly zero. It may be reserved space or FX-matrix/newer data (**unknown**).

During the capture the Peak also sent NRPN **64:8** = 1, then 0, while the Settings menu was used. Bank 64 holds global settings (the addendum lists Patch Cue = 64:0). What 64:8 does is **unknown**.

## Patch map (verified on firmware 2.2, 2026-11-09)
Method: CC/NRPN messages set every charted parameter to its own distinctive value, then front-panel `Go` dumped the current patch. There were 4 rounds plus 2 confirmation rounds. The raw dumps and the values sent are in `Tests/fixtures/mapping/`.

- **241 of 246** charted patch parameters located. Machine-readable map: `docs/peak-patch-map.json`. Readable table: `docs/patch-map-table.md`.
- Every value is stored **as the raw MIDI value** (no offsets or rescaling), so the CC/NRPN value and the dump byte are the same number.
- Layout follows NRPN order: voice 0x30, oscillators 0x3E/0x53/0x68, mixer 0x7D, filter 0x8B, pan 0x9C, envelopes 0xA0/0xA9/0xB1, LFOs 0xB9/0xC7, FX 0xD5–0xFA, arp 0xFD–0x105, mod matrix 0x10A–0x149 (16 × [Src1, Src2, Depth, Dest]), tuning table 0x170, bank-25 parameters 0x18B–0x195.
- **8-bit (CC pair) parameters:** byte[o] = value >> 1, byte[o+1] = (value & 1) << 6. All 18 decode correctly.
- **Reverb Type (0xE8) and Reverb Size (0xEC) are coupled.** Setting Type loads a preset Size (Type 1 → 64, Type 2 → 127). Setting Size re-derives Type (sizes 9 and 26 → Type 0, 63 → 1, 92 → 2). When restoring a patch over CC/NRPN, send Type before Size.
- **Animate 1/2 Hold (CC 114/115)** are not stored in the patch.
- **FM amounts (NRPN 25:13/17/21)** had no effect on the dump. They're probably Summit-only.
- **Osc 3 Fine (CC 21/53):** the value 45 was ignored in two separate runs, while other values were stored. This needs a targeted test (it could be the Peak or the network bridge).
- Patch Category 0x20, Genre 0x21.


## Menu parameters (run 3, verified 2026-11-09)
Found by walking every Osc/Env/LFO/Arp/Voice/FX menu page (each value nudged up one click and back while recording; log in `Tests/fixtures/mapping/run3-menu-walk.log`), then 3 mapping rounds.

- **Osc KeySync** NRPN 0:13 → 0x03D
- **Envelopes:** HoldTime and Repeats at 0:57/58 (Amp), 0:62/63 (Mod 1), 0:66/67 (Mod 2) → 0xA6/A7, 0xAF/B0, 0xB7/B8. **Delay** (firmware 2.1) at 25:30/31/32 → 0x188/189/18A.
- **LFO 1/2:** FadeSync 0:73/0:82 → 0xC2/0xD0; MonoTrig 0:74/0:83 → 0xC3/0xD1. **The chart's "LFO One Shot" (0:75/0:84) is really Repeats** (Off, 1–127; 0xC4/0xD2).
- **LFO 3/4:** Wave/Rate/RateSync at 25:0–2 and 25:3–5 → 0x16A–0x16F.
- **Delay:** LR Ratio 0:91 → 0xDC; Mode 25:44 → 0x196; Output 25:45 → 0x197.
- **FX mod matrix: 4 slots**, NRPN banks 17–20, at 0x14A + 4·n as [Source, ?, Depth, Dest]. The second byte is never written by the menu, and banks 21–24 have no effect.
- **NRPN 62:0** is sent when a menu button is pressed (which menu is open). It's UI state, not a patch parameter. **NRPN 64:6** came from the Arp/Clock menu and is global (bank 64).
- **Osc 3 Fine:** the Peak ignored CC 21 (MSB) for values 44, 45 and 47 (MSB 22/23) in every run, while 108–200 apply. This looks like a firmware quirk. If it matters, check what the Peak itself sends when its knob is turned to the bottom.
- The ranges of FX-matrix sources/destinations, LFO 3/4 RateSync and LR Ratio are not documented. Their upper bounds in the map are placeholders.

### Bytes not yet identified (patch area 0x20–0x20D)
0x022–0x02F, 0x038, 0x051–0x052, 0x066–0x067, 0x07B–0x07C, 0x09A–0x09B, 0x09E–0x09F, 0x0A8, 0x0C6, 0x0D4, 0x0D6, 0x0D9, 0x0E5–0x0E6, 0x0F2, 0x0FB–0x0FC, 0x106–0x109, 0x14B, 0x14F, 0x153, 0x157, 0x15A–0x169, 0x171–0x187, 0x198–0x20D

## Patch location header (verified 2026-11-09, firmware 2.2)
Bank A and bank B dumps from the Backup page (`Tests/fixtures/bank-a.syx`, `bank-b.syx`) plus a Current dump:

| Byte | Stored patch (bank dump) | Current / edit buffer |
|---|---|---|
| 8 | `01` | `00` |
| 12 | bank: A = `01`, B = `02` (C = `03`, D = `04` inferred, **unverified**) | `00` |
| 13 | slot `00`–`7F` | `00` |

- A bank dump is 128 separate 527-byte messages in slot order. Nothing else is sent.
- The Current dump of Tardis (A001) matched A001 byte for byte apart from the header, except for Filter Slope (just toggled on the panel) and Arp Chance. That confirms the dump is the **edited** sound, not the stored one.
- **Writing a slot (verified 2026-11-09):** sending a stored-patch dump overwrites the slot named in its header. A127 was renamed to "Organic Tones *" and the Peak showed it; the original bytes were then sent back and A127 was restored.
- **Loading the current sound (verified 2026-11-09):** sending a dump with byte 8 = `00` (and bytes 12/13 = 0) replaces the Peak's current sound immediately, without saving. The screen showed the sent name "Audition Test", and A000 was unchanged. The editor uses this for Audition and Send to Peak instead of CC/NRPN, because it carries every byte, including unmapped ones.

## Patch category / genre names
Byte 0x20 (Patch Category, NRPN 0:0, 0-14) and 0x21 (Genre, 0:1, 0-9) hold numbers only. Factory patches in bank A use categories 1-12, and every one has genre 0. The User Guide gives no names, and the Peak's own patch menu showed "All" for factory patches, so the editor shows the numbers. Names suggested by example patches (1 Arp: Logarhythm, 2 Bass: Major Bass, 3 Bell: Bells, 5 Drum: Digi Timps, 9 Pad, 10 Poly, 11 SFX) are **unverified**.
