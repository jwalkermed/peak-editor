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

### Bytes not yet identified (patch area 0x20–0x20D)
0x022–0x02F, 0x038, 0x03D, 0x051–0x052, 0x066–0x067, 0x07B–0x07C, 0x09A–0x09B, 0x09E–0x09F, 0x0A6–0x0A8, 0x0AF–0x0B0, 0x0B7–0x0B8, 0x0C2–0x0C3, 0x0C6, 0x0D0–0x0D1, 0x0D4, 0x0D6, 0x0D9, 0x0DC, 0x0E5–0x0E6, 0x0F2, 0x0FB–0x0FC, 0x106–0x109, 0x14A–0x16F, 0x171–0x18A, 0x196–0x20D

Parameters known to exist but not yet charted or located: envelope Delay/Hold/Repeats, LFO FadeSync, LFO 3/4 rate/wave/sync, FX mod matrix, Delay mode/output, glide/voice extras, and arp extras. Each needs a turn-a-control-in-the-menu capture: the Peak transmits the NRPN, and a dump shows the byte.
