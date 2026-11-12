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
| 12 | bank: A = `01`, B = `02`, C = `03`, D = `04` (all verified) | `00` |
| 13 | slot `00`–`7F` | `00` |

- A bank dump is 128 separate 527-byte messages in slot order. Nothing else is sent.
- The Current dump of Tardis (A001) matched A001 byte for byte apart from the header, except for Filter Slope (just toggled on the panel) and Arp Chance. That confirms the dump is the **edited** sound, not the stored one.
- **Writing a slot (verified 2026-11-09):** sending a stored-patch dump overwrites the slot named in its header. A127 was renamed to "Organic Tones *" and the Peak showed it; the original bytes were then sent back and A127 was restored.
- **Loading the current sound (verified 2026-11-09):** sending a dump with byte 8 = `00` (and bytes 12/13 = 0) replaces the Peak's current sound immediately, without saving. The screen showed the sent name "Audition Test", and A000 was unchanged. The editor uses this for Audition and Send to Peak instead of CC/NRPN, because it carries every byte, including unmapped ones.

## Patch category / genre names
Byte 0x20 holds Patch Category (NRPN 0:0, 0-14), and byte 0x21 holds Genre (0:1, 0-9).

**Category names (verified 2026-11-09)** come from the Peak's own category list, captured by the user. They are in the same order as the values:
0 None, 1 Arp, 2 Bass, 3 Bell, 4 Classic, 5 Drum, 6 Keyboard, 7 Lead, 8 Motion, 9 Pad, 10 Poly, 11 SFX, 12 String, 13 User 1, 14 User 2.
That's 15 names for the 0-14 range. Novation Components shows A089 "E. StringMachine" as String, which confirms 12 = String (and so 13/14 = User 1/User 2). They also agree with factory bank A: Logarhythm = Arp, Major Bass = Bass, Bells = Bell, Digi Timps = Drum, Tardis = Motion, Utopian Streams = Pad, Phat Poly = Poly, Tech Noise = SFX. The editor shows them, filters by them, and lets you change them. On a Peak slot, a change rewrites the slot.

Genre names are still unknown. Every factory bank A patch has genre 0.

## Patch selection (verified 2026-11-09, firmware 2.2)
When the patch changes on the Peak (Patch +/-, the encoder), it sends **CC 32 (Bank Select LSB) = bank** (A = 1 ... D = 4), then **Program Change = slot** (0-127), on its MIDI channel. No CC 0 is sent. Going from A127 to B000 sent CC 32 = 2 with PC 0. The editor sends the same pair for Patch +/-, and follows the Peak when it changes patch.

## Mod matrix lists (verified 2026-11-10, firmware 2.2)
Read from the Peak's Mod menu by stepping through each list. Value = position in the list.
- **Sources (0-23):** Direct, ModWheel, AftTouch, ExprPED1, BrthPED2, Velocity, Keyboard, Lfo1+, Lfo1+/-, Lfo2+, Lfo2+/-, AmpEnv, ModEnv1, ModEnv2, Animate1, Animate2, CV+/-, Lfo3+, Lfo3+/-, Lfo4+, Lfo4+/-, BndWhl+, BndWhl-, Noise. Factory banks use 0-20; for example, bank C uses LFO 3/4.
- **Destinations (0-47):** the 37 in the User Guide (O123Ptch ... Ns>FiltF), then Pan Posn, O123Shpe, AEnv Del, AEnv Hld, AEnv Sus, MEnv1Del, MEnv1Hld, MEnv1Sus, MEnv2Del, MEnv2Hld, MEnv2Sus. Across all 512 patches, destination 18 (FiltFreq) is the most used.
- **FX matrix (verified 2026-11-10):**
  - Each FX slot is [Source A, Source B, Depth, Destination] at 0x14A + 4n. Source B is NRPN `n:1`, verified by stepping 18:1 and by a Current dump.
  - Sources 0-16: as the main list, but without the LFO 1/2 and envelope entries.
  - Destinations 0-23: Dist Lev, Chor Lev, ChorRate, Chor Dep, Chor FB, Del Lev, Del Time, Del FB, Rev Lev, Rev Time, RevLdamp, RevHdamp, Rev Size, RevLpass, RevHpass, Del Slew, DelLdamp, DelHdamp, DelWidth, DelRatio, Lfo3Rate, Lfo4Rate, NoiseLPF, NoiseHPF.
  - The User Guide's Rev LPF/HPF (10/11) now read RevLdamp/RevHdamp.

## Sending settings to the Peak (verified 2026-11-10, firmware 2.2)
- The Peak **sends** NRPN 64:n when a setting changes on its panel, but it **ignores** incoming NRPN 64:n (Brightness 64:11 = 5 had no effect).
- A Settings dump (header byte 8 = 0x04, value of setting n at byte 16 + n) sent back to the Peak **is applied** at once (Brightness changed). The editor sends its whole cached dump after an edit, so every setting takes the cached value.
- TuningMode (64:30) is 0 = Note, 1 = Octave.

## Named value lists (verified 2026-11-10, firmware 2.2)
Read from the Peak's screen after sending values, or fixed by the Init Patch's values:
- **LFO 1/2 Sync Rate (0–34)** and **LFO 3/4 RateSync (0–35)** run slowest first: 0 = 64 beats … 34 = 32nd T. LFO 3/4 add Off at 0, so 1 = 64 beats.
- **Arp SyncRate (0–18)** runs fastest first: 0 = 32nd T, 3 = 16th (Init), 18 = 8 beats.
- **Delay SyncRate runs 0–27**, not 0–18 as the chart says. It's the LFO list fastest first, from 32nd T up to 24 beats (7 = 4th T, Init).
- **WaveMore (4–63)**: the guide's 4-column wavetable grid reads down the columns (5 = Random, 19 = String).

## Animate buttons and MIDI volume (verified 2026-11-10, firmware 2.2)
- The Peak's Animate 1/2 buttons send **CC 114/115 = 127** when pressed and **0** when released, and the Peak responds to the same. A value of 1 does nothing. Hold sends nothing of its own: while an Animate is held, the release (0) just isn't sent until Hold is released.
- The Peak ignores **CC 7** (MIDI volume): CC 7 = 0 left it at full volume. Master Volume has no MIDI control.

## Dump requests (verified 2026-11-10, firmware 2.2)
The dump header with a request command in byte 8 makes the Peak send a dump:
- `F0 00 20 29 01 10 00 7E 40 00 00 00 00 00 F7`: the current sound (type 00 reply, no location).
- `... 7E 41 00 00 00 <bank> <slot> F7`: the patch in a slot, bank A = 1 (type 01 reply, located).
- `... 7E 44 00 00 00 00 00 F7`: the global settings (type 04 reply).

Stored patches hold **Arp Chance 0** where the same patch as the current sound reports **100** (the default), so patches are compared with that difference ignored.

## Clock (verified 2026-11-10, firmware 2.2)
- **Clock Rate** (40–240 BPM) is stored in each patch at 0xFB/0xFC with the CC-pair encoding (120 = `3C 00`, 97 = `30 40`). It has **no MIDI control** (the chart says NA), so the editor changes it by sending the whole patch.
- **Clock Source** is a global setting, NRPN **64:6**: 0 Auto, 1 Internal, 2 Ext-Auto, 3 MIDI, 4 USB.
- The status line ("INT 97.00bpm") is only on the Peak's screen. The Peak sent **no MIDI clock** while idle, and nothing reports whether it is locked to an external clock.

## Reading memory directly (verified 2026-11-10/11)
- Slot requests (`41`) answer in about 12 ms each over the network session, so a bank reads in about 2 s.
- Nothing announces changes made to the Peak's memory from elsewhere (e.g. Components). The editor compares its copy with the Peak at startup and whenever the Peak selects a slot.

## Wavetables (verified 2026-11-11, firmware 2.2)
- **WaveMore runs 4–73**, not 4–63: 64–73 are the 10 user wavetables (loaded with Components).
- **Name request:** `F0 00 20 29 01 10 00 7E 47 00 00 <n> 00 00 F7`, with n = the WaveMore number (0–73; 0–3 are Sine, Triangle, Sawtooth, Pulse). The reply has type 07, n in byte 14 and up to **7** name characters from byte 15 (e.g. `Noisia`, `User 2`).
- Command **46** returns wavetable sample data (531 bytes); not used yet.
- The Peak spells two factory tables differently from the User Guide: **Grow** (guide: Drow) and **E.Piano**.

## User wavetable data (read 2026-11-11, firmware 2.2; branch wave-table-editor)
- **Request:** `F0 00 20 29 01 10 00 7E 46 00 00 <slot> <wave> 00 F7`, slot 0–9 = User 1–10, wave 0–4.
  Slots 11+ don't answer. Slot 10 answers with data that doesn't decode to musical waves (perhaps a work buffer).
  The factory wavetables can't be read this way.
- **Reply:** 531 bytes, type 06 (byte 8), the slot in byte 14 and the wave in byte 15, then 514 data bytes from byte 16.
  It answers in about 20 ms, so a whole table (5 waves) reads in about 0.1 s.
- **Data:** 257 signed 14-bit values (two 7-bit bytes, high first). They're a **spectrum**: value 0 is DC, then for
  harmonic h (1–128) value 2h−1 is its cosine part and value 2h its sine part, full scale 8191 (this is how
  Components' `generateSyx` writes it: DC, then x and y for partials 1–128). An earlier reading paired values 2h
  and 2h+1, which put every cosine part one harmonic up: sine-only waves (saw, square) were right, a pulse wasn't.
  Value 0 is the offset (DC), at the same scale. With it, the spectrum gives back a 256-sample drawing exactly.
  Components scales each wave down when sending if any value or point would pass full scale (so a full-height
  square, whose first harmonic is 4/π, isn't clipped); the editor does the same.
  An empty slot is one harmonic at 8191 in all 5 waves (a sine). Noisia (User 1) rebuilds to 5 distinct waves
  (`Tests/fixtures/wavetables/`, `docs/screenshots/wavetables-decoded.png`).
- **Uploading (verified 2026-11-11):** send what the Peak sends back, in Components' order:
  a **name** `F0 00 20 29 01 10 00 7E 07 00 00 00 00 00 <slot 0-9> <7 ASCII chars> F7`, then the **5 waves**
  `F0 00 20 29 01 10 00 7E 06 00 00 00 00 00 <slot> <wave 0-4> <514 data bytes> F7`, about 20 ms apart
  (this is exactly what Components' Send to Peak and Live Edit send). The Peak plays it at once, and reads back
  exactly what was sent.
- **Uploads are temporary:** they're lost at power-off, over the network session or USB alike, with or without a
  wait, and with or without the 0x49 message Components' files may end with. Components behaves the same.
  **To keep a wavetable, save it on the Peak:** Osc menu, page 9 "User Waves", select the wavetable, press that
  page's own Save (not the patch Save). Novation's article "Saving User Wavetables to Peak/Summit" describes this.
- **Saving sends nothing:** pressing that Save sends no MIDI (only the usual NRPN 62:0 when the Osc menu opens),
  so the editor can't trigger or detect it; the save stays a step on the Peak.
- **Polarity:** a sawtooth sent as sine parts (-1)^(h+1)/h rises slowly and drops sharply on an oscilloscope, so
  positive sine parts are drawn upward (as decoded). Components draws a wave as Σ x·cos − y·sin, the opposite
  way up, so the editor negates the cosine part to match: what the scope shows is −(Components' drawing).
  (To confirm with a 25% pulse: high for a quarter of the cycle, not three quarters.)

## Oscillator Coarse display (read 2026-11-12, firmware 2.2)
- Coarse is a CC pair (raw 0–255). The Peak shows it as semitones to a tenth: −12.0 at 0, 0.0 at 127 and 128,
  +12.0 at 255. Readings taken by turning the Peak's knob to a shown value and recording the raw value it sent:
  +0.1 = 129, +0.5 = 133, +1.0 = 138, +2.0 = 149, +3.0 = 160, +5.0 = 182, +7.0 = 203, +9.0 = 224, +11.0 = 245,
  +11.9 = 254, +12.0 = 255; −0.1 = 126, −1.0 = 117, −3.0 = 95, −5.0 = 74, −7.0 = 53, −9.0 = 31, −11.0 = 10, −12.0 = 0.
- Best fit: round((raw − 127.5) × 120 / 128) tenths, exact for 17 of 19; +5.0 and +7.0 read 0.1 lower than it.
  No rule fits all 19, which suggests the knob is finer than the 256 values sent, so a raw value can show either
  of two neighbouring tenths.
