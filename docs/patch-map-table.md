# Peak patch dump map (firmware 2.2)

Generated from `docs/peak-patch-map.json`. The offset is the byte index in the 527-byte dump (F0 = 0). Values are raw MIDI values, as in the manual's chart. Two-CC parameters (shown as CC pair) take 2 bytes: the top 7 bits at the offset, then (value & 1) << 6 in the next byte.

| Offset | Parameter | MIDI | Range |
|---|---|---|---|
| 0x020 | Patch Category | NRPN 0:0 | 0–14 |
| 0x021 | Patch Genre | NRPN 0:1 | 0–9 |
| 0x030 | Voice Mode | NRPN 0:2 | 0–4 |
| 0x031 | Voice Unison | NRPN 0:3 | 0–4 |
| 0x032 | Unison Detune | NRPN 0:4 | 0–127 |
| 0x033 | Spread | NRPN 0:5 | 0–127 |
| 0x034 | Keyboard Octave | NRPN 0:6 | 61–67 |
| 0x035 | Glide Time | CC 5 | 0–127 |
| 0x036 | Pre-Glide | NRPN 0:7 | 52–76 |
| 0x037 | Glide On | CC 35 | 0–1 |
| 0x039 | Osc Divergence | NRPN 0:9 | 0–127 |
| 0x03A | Osc Drift | NRPN 0:10 | 0–127 |
| 0x03B | Noise LPF | NRPN 0:11 | 0–127 |
| 0x03C | Noise HPF | NRPN 0:12 | 0–127 |
| 0x03E | Osc1 Range | CC 3 | 63–66 |
| 0x03F | Osc1 Coarse | PAIR 14 | 0–255 |
| 0x041 | Osc1 Fine | PAIR 15 | 28–228 |
| 0x043 | Osc1 ModEnv2>Pitch | CC 9 | 1–127 |
| 0x044 | Osc1 LFO2>Pitch | PAIR 16 | 1–255 |
| 0x046 | Osc1 Wave | NRPN 0:14 | 0–4 |
| 0x047 | Osc1 Wave More | NRPN 0:15 | 4–63 |
| 0x048 | Osc1 Shape Source | NRPN 0:16 | 0–2 |
| 0x049 | Osc1 Manual Shape | CC 12 | 0–127 |
| 0x04A | Osc1 ModEnv1>Shape | CC 119 | 0–127 |
| 0x04B | Osc1 LFO1>Shape | CC 33 | 0–127 |
| 0x04C | Osc1 Vsync | CC 34 | 0–127 |
| 0x04D | Osc1 Saw Density | NRPN 0:17 | 0–127 |
| 0x04E | Osc1 Saw Detune | NRPN 0:18 | 0–127 |
| 0x04F | Osc1 Fixed Note | NRPN 0:19 | 0–88 |
| 0x050 | Osc1 Bend Range | NRPN 0:20 | 40–88 |
| 0x053 | Osc2 Range | CC 37 | 63–66 |
| 0x054 | Osc2 Coarse | PAIR 17 | 0–255 |
| 0x056 | Osc2 Fine | PAIR 18 | 28–228 |
| 0x058 | Osc2 ModEnv2>Pitch | CC 38 | 1–127 |
| 0x059 | Osc2 LFO2>Pitch | PAIR 19 | 1–255 |
| 0x05B | Osc2 Wave | NRPN 0:23 | 0–4 |
| 0x05C | Osc2 Wave More | NRPN 0:24 | 4–63 |
| 0x05D | Osc2 Shape Source | NRPN 0:25 | 0–2 |
| 0x05E | Osc2 Manual Shape | CC 39 | 0–127 |
| 0x05F | Osc2 ModEnv1>Shape | CC 40 | 0–127 |
| 0x060 | Osc2 LFO1>Shape | CC 41 | 0–127 |
| 0x061 | Osc2 Vsync | CC 42 | 0–127 |
| 0x062 | Osc2 Saw Density | NRPN 0:26 | 0–127 |
| 0x063 | Osc2 Saw Detune | NRPN 0:27 | 0–127 |
| 0x064 | Osc2 Fixed Note | NRPN 0:28 | 0–88 |
| 0x065 | Osc2 Bend Range | NRPN 0:29 | 40–88 |
| 0x068 | Osc3 Range | CC 65 | 63–66 |
| 0x069 | Osc3 Coarse | PAIR 20 | 0–255 |
| 0x06B | Osc3 Fine | PAIR 21 | 28–228 |
| 0x06D | Osc3 ModEnv2>Pitch | CC 43 | 1–127 |
| 0x06E | Osc3 LFO2>Pitch | PAIR 22 | 1–255 |
| 0x070 | Osc3 Wave | NRPN 0:32 | 0–4 |
| 0x071 | Osc3 Wave More | NRPN 0:33 | 4–63 |
| 0x072 | Osc3 Shape Source | NRPN 0:34 | 0–2 |
| 0x073 | Osc3 Manual Shape | CC 71 | 0–127 |
| 0x074 | Osc3 ModEnv1>Shape | CC 72 | 0–127 |
| 0x075 | Osc3 LFO1>Shape | CC 73 | 0–127 |
| 0x076 | Osc3 Vsync | CC 44 | 0–127 |
| 0x077 | Osc3 Saw Density | NRPN 0:35 | 0–127 |
| 0x078 | Osc3 Saw Detune | NRPN 0:36 | 0–127 |
| 0x079 | Osc3 Fixed Note | NRPN 0:37 | 0–88 |
| 0x07A | Osc3 Bend Range | NRPN 0:38 | 40–88 |
| 0x07D | Mix Osc1 | PAIR 23 | 0–255 |
| 0x07F | Mix Osc2 | PAIR 24 | 0–255 |
| 0x081 | Mix Osc3 | PAIR 25 | 0–255 |
| 0x083 | Ring 1*2 | PAIR 26 | 0–255 |
| 0x085 | Noise Level | PAIR 27 | 0–255 |
| 0x087 | Patch Level | NRPN 0:41 | 0–127 |
| 0x088 | VCA Gain | NRPN 0:42 | 0–127 |
| 0x089 | Dry Level | NRPN 0:43 | 0–127 |
| 0x08A | Wet Level | NRPN 0:44 | 0–127 |
| 0x08B | Filter Overdrive | CC 80 | 0–127 |
| 0x08C | Filter Post Drive | CC 36 | 0–127 |
| 0x08D | Filter Slope | NRPN 0:45 | 0–1 |
| 0x08E | Filter Shape | NRPN 0:46 | 0–2 |
| 0x08F | Filter Key Track | CC 75 | 0–127 |
| 0x090 | Filter Resonance | CC 79 | 0–127 |
| 0x091 | Filter Frequency | PAIR 29 | 0–255 |
| 0x093 | LFO1>Filter | PAIR 28 | 1–255 |
| 0x095 | Osc3>Filter | CC 76 | 0–127 |
| 0x096 | Filter Env Select | NRPN 0:47 | 0–1 |
| 0x097 | AmpEnv>Filter | CC 77 | 1–127 |
| 0x098 | ModEnv1>Filter | CC 78 | 1–127 |
| 0x099 | Filter Divergence | NRPN 0:48 | 0–127 |
| 0x09C | Pan Posn | NRPN 0:51 | 0–127 |
| 0x09D | Spread Mode | NRPN 0:52 | 0–3 |
| 0x0A0 | AmpEnv Attack | CC 86 | 0–127 |
| 0x0A1 | AmpEnv Decay | CC 87 | 0–127 |
| 0x0A2 | AmpEnv Sustain | CC 88 | 0–127 |
| 0x0A3 | AmpEnv Release | CC 89 | 0–127 |
| 0x0A4 | AmpEnv Velocity | NRPN 0:55 | 0–127 |
| 0x0A5 | AmpEnv Trigger | NRPN 0:56 | 0–1 |
| 0x0A9 | ModEnv1 Attack | CC 90 | 0–127 |
| 0x0AA | ModEnv1 Decay | CC 91 | 0–127 |
| 0x0AB | ModEnv1 Sustain | CC 92 | 0–127 |
| 0x0AC | ModEnv1 Release | CC 93 | 0–127 |
| 0x0AD | ModEnv1 Velocity | NRPN 0:60 | 0–127 |
| 0x0AE | ModEnv1 Trigger | NRPN 0:61 | 0–1 |
| 0x0B1 | ModEnv2 Attack | CC 94 | 0–127 |
| 0x0B2 | ModEnv2 Decay | CC 95 | 0–127 |
| 0x0B3 | ModEnv2 Sustain | CC 117 | 0–127 |
| 0x0B4 | ModEnv2 Release | CC 103 | 0–127 |
| 0x0B5 | ModEnv2 Velocity | NRPN 0:64 | 0–127 |
| 0x0B6 | ModEnv2 Trigger | NRPN 0:65 | 0–1 |
| 0x0B9 | LFO1 Range | NRPN 0:68 | 0–2 |
| 0x0BA | LFO1 Rate | PAIR 30 | 0–255 |
| 0x0BC | LFO1 Sync Rate | CC 81 | 0–34 |
| 0x0BD | LFO1 Wave | NRPN 0:69 | 0–3 |
| 0x0BE | LFO1 Phase | NRPN 0:70 | 0–120 |
| 0x0BF | LFO1 Slew | NRPN 0:71 | 0–127 |
| 0x0C0 | LFO1 Fade Time | CC 82 | 0–127 |
| 0x0C1 | LFO1 Fade In/Out | NRPN 0:72 | 0–3 |
| 0x0C4 | LFO1 One Shot | NRPN 0:75 | 0–1 |
| 0x0C5 | LFO1 Common | NRPN 0:76 | 0–1 |
| 0x0C7 | LFO2 Range | CC 83 | 0–2 |
| 0x0C8 | LFO2 Rate | PAIR 31 | 0–255 |
| 0x0CA | LFO2 Sync Rate | CC 84 | 0–34 |
| 0x0CB | LFO2 Wave | NRPN 0:78 | 0–3 |
| 0x0CC | LFO2 Phase | NRPN 0:79 | 0–120 |
| 0x0CD | LFO2 Slew | NRPN 0:80 | 0–127 |
| 0x0CE | LFO2 Fade Time | CC 85 | 0–127 |
| 0x0CF | LFO2 Fade In/Out | NRPN 0:81 | 0–3 |
| 0x0D2 | LFO2 One Shot | NRPN 0:84 | 0–1 |
| 0x0D3 | LFO2 Common | NRPN 0:85 | 0–1 |
| 0x0D5 | Distortion Level | CC 104 | 0–127 |
| 0x0D7 | FX Bypass | NRPN 0:88 | 0–1 |
| 0x0D8 | FX Routing | NRPN 0:89 | 0–6 |
| 0x0DA | Delay Level | CC 108 | 0–127 |
| 0x0DB | Delay Time | CC 109 | 0–127 |
| 0x0DD | Delay Width | NRPN 0:92 | 0–127 |
| 0x0DE | Delay Sync | NRPN 0:93 | 0–1 |
| 0x0DF | Delay Sync Time | NRPN 0:94 | 0–18 |
| 0x0E0 | Delay Feedback | CC 110 | 0–127 |
| 0x0E1 | Delay LP Damp | NRPN 0:95 | 0–127 |
| 0x0E2 | Delay HP Damp | NRPN 0:96 | 0–127 |
| 0x0E3 | Delay Slew | NRPN 0:97 | 0–127 |
| 0x0E4 | Delay TimeMode | NRPN 0:98 | 0–4 |
| 0x0E7 | Reverb Level | CC 112 | 0–127 |
| 0x0E8 | Reverb Type | NRPN 0:101 | 0–2 |
| 0x0E9 | Reverb Time | CC 113 | 0–127 |
| 0x0EA | Reverb Damp LP | NRPN 0:102 | 0–127 |
| 0x0EB | Reverb Damp HP | NRPN 0:103 | 0–127 |
| 0x0EC | Reverb Size | NRPN 0:104 | 0–127 |
| 0x0ED | Reverb Mod | NRPN 0:105 | 0–127 |
| 0x0EE | Reverb Mod Rate | NRPN 0:106 | 0–127 |
| 0x0EF | Reverb LowPass | NRPN 0:107 | 0–127 |
| 0x0F0 | Reverb HighPass | NRPN 0:108 | 0–127 |
| 0x0F1 | Reverb PreDelay | NRPN 0:109 | 0–127 |
| 0x0F3 | Chorus Level | CC 105 | 0–127 |
| 0x0F4 | Chorus Type | NRPN 0:111 | 0–2 |
| 0x0F5 | Chorus Rate | CC 118 | 0–127 |
| 0x0F6 | Chorus Depth | NRPN 0:112 | 0–127 |
| 0x0F7 | Chorus Feedback | CC 107 | 0–127 |
| 0x0F8 | Chorus LP | NRPN 0:113 | 0–127 |
| 0x0F9 | Chorus HP | NRPN 0:114 | 0–127 |
| 0x0FA | Chorus Mode | NRPN 0:115 | 0–2 |
| 0x0FD | Arp Sync Rate | NRPN 0:116 | 0–18 |
| 0x0FE | Arp Type | NRPN 0:117 | 0–6 |
| 0x0FF | Arp Rhythm | NRPN 0:118 | 0–32 |
| 0x100 | Arp Octave | NRPN 0:119 | 0–5 |
| 0x101 | Arp Gate | CC 116 | 0–127 |
| 0x102 | Arp Swing | NRPN 0:120 | 20–80 |
| 0x103 | Arp On | NRPN 0:121 | 0–1 |
| 0x104 | Arp Key Latch | NRPN 0:122 | 0–1 |
| 0x105 | Arp Key Sync | NRPN 0:123 | 0–1 |
| 0x10A | Mod1 Source1 | NRPN 1:0 | 0–16 |
| 0x10B | Mod1 Source2 | NRPN 1:1 | 0–16 |
| 0x10C | Mod1 Depth | NRPN 1:2 | 0–127 |
| 0x10D | Mod1 Dest | NRPN 1:3 | 0–36 |
| 0x10E | Mod2 Source1 | NRPN 2:0 | 0–16 |
| 0x10F | Mod2 Source2 | NRPN 2:1 | 0–16 |
| 0x110 | Mod2 Depth | NRPN 2:2 | 0–127 |
| 0x111 | Mod2 Dest | NRPN 2:3 | 0–36 |
| 0x112 | Mod3 Source1 | NRPN 3:0 | 0–16 |
| 0x113 | Mod3 Source2 | NRPN 3:1 | 0–16 |
| 0x114 | Mod3 Depth | NRPN 3:2 | 0–127 |
| 0x115 | Mod3 Dest | NRPN 3:3 | 0–36 |
| 0x116 | Mod4 Source1 | NRPN 4:0 | 0–16 |
| 0x117 | Mod4 Source2 | NRPN 4:1 | 0–16 |
| 0x118 | Mod4 Depth | NRPN 4:2 | 0–127 |
| 0x119 | Mod4 Dest | NRPN 4:3 | 0–36 |
| 0x11A | Mod5 Source1 | NRPN 5:0 | 0–16 |
| 0x11B | Mod5 Source2 | NRPN 5:1 | 0–16 |
| 0x11C | Mod5 Depth | NRPN 5:2 | 0–127 |
| 0x11D | Mod5 Dest | NRPN 5:3 | 0–36 |
| 0x11E | Mod6 Source1 | NRPN 6:0 | 0–16 |
| 0x11F | Mod6 Source2 | NRPN 6:1 | 0–16 |
| 0x120 | Mod6 Depth | NRPN 6:2 | 0–127 |
| 0x121 | Mod6 Dest | NRPN 6:3 | 0–36 |
| 0x122 | Mod7 Source1 | NRPN 7:0 | 0–16 |
| 0x123 | Mod7 Source2 | NRPN 7:1 | 0–16 |
| 0x124 | Mod7 Depth | NRPN 7:2 | 0–127 |
| 0x125 | Mod7 Dest | NRPN 7:3 | 0–36 |
| 0x126 | Mod8 Source1 | NRPN 8:0 | 0–16 |
| 0x127 | Mod8 Source2 | NRPN 8:1 | 0–16 |
| 0x128 | Mod8 Depth | NRPN 8:2 | 0–127 |
| 0x129 | Mod8 Dest | NRPN 8:3 | 0–36 |
| 0x12A | Mod9 Source1 | NRPN 9:0 | 0–16 |
| 0x12B | Mod9 Source2 | NRPN 9:1 | 0–16 |
| 0x12C | Mod9 Depth | NRPN 9:2 | 0–127 |
| 0x12D | Mod9 Dest | NRPN 9:3 | 0–36 |
| 0x12E | Mod10 Source1 | NRPN 10:0 | 0–16 |
| 0x12F | Mod10 Source2 | NRPN 10:1 | 0–16 |
| 0x130 | Mod10 Depth | NRPN 10:2 | 0–127 |
| 0x131 | Mod10 Dest | NRPN 10:3 | 0–36 |
| 0x132 | Mod11 Source1 | NRPN 11:0 | 0–16 |
| 0x133 | Mod11 Source2 | NRPN 11:1 | 0–16 |
| 0x134 | Mod11 Depth | NRPN 11:2 | 0–127 |
| 0x135 | Mod11 Dest | NRPN 11:3 | 0–36 |
| 0x136 | Mod12 Source1 | NRPN 12:0 | 0–16 |
| 0x137 | Mod12 Source2 | NRPN 12:1 | 0–16 |
| 0x138 | Mod12 Depth | NRPN 12:2 | 0–127 |
| 0x139 | Mod12 Dest | NRPN 12:3 | 0–36 |
| 0x13A | Mod13 Source1 | NRPN 13:0 | 0–16 |
| 0x13B | Mod13 Source2 | NRPN 13:1 | 0–16 |
| 0x13C | Mod13 Depth | NRPN 13:2 | 0–127 |
| 0x13D | Mod13 Dest | NRPN 13:3 | 0–36 |
| 0x13E | Mod14 Source1 | NRPN 14:0 | 0–16 |
| 0x13F | Mod14 Source2 | NRPN 14:1 | 0–16 |
| 0x140 | Mod14 Depth | NRPN 14:2 | 0–127 |
| 0x141 | Mod14 Dest | NRPN 14:3 | 0–36 |
| 0x142 | Mod15 Source1 | NRPN 15:0 | 0–16 |
| 0x143 | Mod15 Source2 | NRPN 15:1 | 0–16 |
| 0x144 | Mod15 Depth | NRPN 15:2 | 0–127 |
| 0x145 | Mod15 Dest | NRPN 15:3 | 0–36 |
| 0x146 | Mod16 Source1 | NRPN 16:0 | 0–16 |
| 0x147 | Mod16 Source2 | NRPN 16:1 | 0–16 |
| 0x148 | Mod16 Depth | NRPN 16:2 | 0–127 |
| 0x149 | Mod16 Dest | NRPN 16:3 | 0–36 |
| 0x170 | Tuning Table | NRPN 25:6 | 0–16 |
| 0x18B | Arp Chance | NRPN 25:33 | 1–100 |
| 0x18C | Anim1 Att | NRPN 25:34 | 0–127 |
| 0x18D | Anim1 Rel | NRPN 25:35 | 0–127 |
| 0x18E | Anim2 Att | NRPN 25:36 | 0–127 |
| 0x18F | Anim2 Rel | NRPN 25:37 | 0–127 |
| 0x190 | LFO3 Phase | NRPN 25:38 | 0–120 |
| 0x191 | LFO3 Slew | NRPN 25:39 | 0–127 |
| 0x192 | LFO3 Fade | NRPN 25:40 | 0–127 |
| 0x193 | LFO4 Phase | NRPN 25:41 | 0–120 |
| 0x194 | LFO4 Slew | NRPN 25:42 | 0–127 |
| 0x195 | LFO4 Fade | NRPN 25:43 | 0–127 |
