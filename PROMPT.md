# Build a Novation Peak Editor/Librarian

## Goal
Build an editor and librarian for the **Novation Peak** (8-voice hybrid polysynth, **firmware 2.2**) that ships as a **standalone Mac app plus VST3 and AU plugins**, all as **Universal Binaries (arm64 + x86_64)**. It should let me:
1. **Edit** every sound parameter from the computer with two-way sync: moving a control in the app changes the Peak, and turning a knob on the Peak updates the app.
2. **Manage patches**: fetch, store, organize, back up, and restore the Peak's 512 patch slots (banks A–D, 128 patches each), and import or export `.syx` files.

## Tech stack
- C++20 with **JUCE 9** (pinned version, fetched by CMake). A single `juce_add_plugin` target builds `FORMATS Standalone AU VST3`.
- Set `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` with deployment target macOS 12.0 (the minimum Xcode 27 supports). Check every build product with `lipo -archs`.
- Ad-hoc code signing for local use. Leave Developer ID signing and notarization as a documented later step.
- Keep MIDI and patch logic in plain C++ with no JUCE GUI dependencies, so a console test runner can test it without a host.
- The library lives in a folder of `.syx` files plus a JSON index under `~/Library/Application Support/Peak Editor/`, so my data is never locked in.

## How the plugins talk to the Peak
- Every format talks to the Peak **directly over CoreMIDI** (its USB port or a network MIDI session), not through the host's MIDI routing. Hosts handle SysEx inconsistently, and most can't route MIDI out of an AU instrument.
- All plugin instances in a process share one connection object (`SharedResourcePointer`). The chosen ports are saved in a shared settings file, so the standalone app and the plugins agree.
- The plugin registers as an instrument (`IS_SYNTH`) with a silent stereo output, so it can be placed on a track in Logic, Live, and other hosts. Later: optionally forward the track's MIDI notes to the Peak, and expose patch parameters to the host for automation, sent on to the Peak as CC/NRPN.
- The plugin state saves the current patch in the DAW project. On project load, it offers to send the saved patch to the Peak's edit buffer, and never sends without asking.

## Visual design: faithful to the hardware
The reference photo is `docs/reference/peak-front-panel.png`. Unless I say otherwise, the main editor view **reproduces the Peak's front panel**: the same sections, positions, labels, and control types. Draw everything as vectors (don't use the photo as a background image), and keep the window resizable with a fixed aspect ratio.

- **Look**: dark charcoal panel, thin light-blue rule above each section title, white-capped black knobs with dot scales, vertical faders for the envelopes, columns of small amber LEDs to show the selected option, white buttons that glow amber when on, and walnut end cheeks.
- **Top row, left to right**:
  - MASTER (Volume)
  - PATCH (Initialise, Compare, Audition, Save, patch encoder, Patch −/+)
  - MENU (Patch button; Osc / Env / LFO / Arp-Clock / Mod / Voice / FX / Settings; Active Voices LEDs 1–8; a 4-line display with 3 soft buttons on its left; Page/Select ◀ ▶; data encoder; Value −/+)
  - ARP (Gate, Key Latch, On)
  - LFO 1 and LFO 2 (Type LEDs: triangle / saw / square / S+H; Fade Time; Range Low / High / Sync; Rate)
- **Second row**:
  - ANIMATE (1, 2, Hold), below MASTER/PATCH
  - GLIDE (Time, On)
  - AMP ENVELOPE (A / D / S / R faders)
  - MOD ENVELOPES (Select 1 / 2 with LEDs; A / D / S / R faders)
- **Lower half**:
  - OSCILLATOR 1, 2, and 3, as stacked identical strips:
    - Range 16′ / 8′ / 4′ / 2′, Coarse, Fine
    - Wave LEDs: sine / triangle / saw / pulse / "more"
    - Pitch group: Mod Env 2 Depth, LFO 2 Depth
    - Shape Amount with Source: Manual / Mod Env 1 / LFO 1
  - MIXER (Osc 1, Osc 2, Osc 3, Ring 1•2, Noise) and VCA Gain
  - FILTER:
    - LFO 1 Depth, Osc 3 Filter Mod, Key Tracking
    - Env Depth with Source: Amp Env / Mod Env 1
    - Resonance, Overdrive
    - Slope 12 / 24 dB
    - large Frequency knob
    - Shape LP / BP / HP
  - Effects:
    - DISTORTION (Level)
    - CHORUS (Rate, Type 1–3, Level)
    - DELAY (Feedback, Time, Sync, Level)
    - EFFECTS (Bypass)
    - REVERB (Time, Type 1–3, Level)
- **Menu-only parameters** (anything without its own knob, e.g. Osc "WaveMore", FixedNote, BendRange, the mod matrix, voice and arp settings) are reached the way the hardware reaches them:
  - The display shows the last-touched parameter name and value, like the real 4-line display.
  - The menu buttons and the ▶ arrows on the section headers open the matching parameter pages. On the computer these can be larger pop-over panels that keep the hardware's page grouping and naming.
- The panel buttons work as they do on the hardware: Compare toggles between the edited and stored patch, Initialise loads the init patch, and so on.
- The **librarian** is a separate view (tab or slide-out drawer). It must not clutter the panel.

## Ground rules (important)
- **Don't invent MIDI data.** The CC/NRPN chart in the official Peak 1.2 User Guide appendix, the 2.0/2.1 firmware addendum, and the 2.2 release notes (all summarized in `docs/sysex-notes.md`) are the source of truth for real-time control. Ask me for the PDF or the relevant pages if you don't have them. Don't fill gaps from memory.
- **The SysEx patch format isn't officially documented.** Work it out empirically:
  1. Capture a patch dump.
  2. Change exactly one parameter on the Peak, dump again, and diff.
  3. Record each finding (byte offset, bit packing, range, signedness) with the capture it came from.
  4. Mark unknown bytes as `unknown` and pass them through unchanged. Never zero them out.
- When you need a capture or a hardware test, tell me the exact steps to run with the app's MIDI monitor. Don't guess.
- **Hardware writes are destructive.** Before writing to any patch slot, show a confirmation that names the slot. Before any bulk write, automatically back up the affected bank to a timestamped `.syx` file.

## Architecture
- **One data-driven parameter table** (`Source/peak/core/Params.*`) is the single source of truth. Each entry has: id, display name, panel section, CC and/or NRPN number, range, value-to-display formatter (semitones, ms, waveform names), and SysEx offset/encoding. The panel, MIDI encoder, and patch codec all read from it. No parameter numbers are hard-coded anywhere else.
- **Patch codec**: `decode(sysex) -> Patch` and `encode(Patch) -> sysex`. Encoding must reproduce the original bytes exactly; test this with real captured fixtures in `Tests/fixtures/`.
- **MIDI layer**: port selection with Peak auto-detect, MIDI channel, CC/NRPN send with throttling (no MIDI flooding while dragging), incoming parsing including NRPN reassembly, and a MIDI monitor for debugging.

## Build in phases (stop and demo after each)
1. **Scaffold and MIDI plumbing**:
   - The CMake project builds Standalone, AU, and VST3 as Universal Binaries, and `auval` passes.
   - Port selection, live MIDI monitor, a raw CC/SysEx sender, and "save captured SysEx to `.syx`".
2. **Parameter table and real-time control**: map every parameter from the User Guide, and verify two-way sync on hardware with a plain generic control list.
3. **Patch codec**: decode/encode dumps, with round-trip tests passing on at least 5 real patches.
4. **Front-panel editor**: the faithful panel described above, plus menu-page pop-overs, undo/redo, and highlighting of parameters that differ from the stored patch. Knobs support drag, scroll wheel, double-click to default, and typed entry.
5. **Librarian**:
   - Fetch all 512 slots with a progress bar and support for cancelling.
   - Library with names, categories, tags, search, favorites, and duplicate detection.
   - Drag patches between library and bank slots, rename (respecting the Peak's name length/charset), audition in the edit buffer without saving, and write to a slot (with confirmation).
   - Full backup/restore, and import/export of single and bank `.syx` files, including Novation Components exports if the format matches.
6. **Plugin integration and polish**: host automation of parameters, MIDI forwarding from the track, A/B compare, randomize with per-section lock.

## Quality bar
- Core logic has unit tests: NRPN encode/decode, SysEx framing, parameter-table sanity (no duplicate numbers, valid ranges), and codec round-trips.
- The editor works without hardware attached, either offline or with a loopback/mock port.
- Show clear errors for a missing device, wrong MIDI channel, or Peak global settings that block CC/NRPN transmit (and tell me which Peak setting to change).
- Keep a `docs/sysex-notes.md` log of everything learned about the dump format.
