#!/usr/bin/env python3
"""Measures the Peak front panel from docs/reference/peak-front-panel.png.

Approximate positions below were read off the photo on a coordinate grid. Knobs
are snapped to their detected white caps. Then:
  * knob caps stand ~2 cm proud of the panel, so they lean away from the camera
    axis (parallax). Fitting cap offset vs. printed scale-dot ring centre gives
    cap = ring + k * (ring - axis); the inverse gives the true knob position.
  * buttons are low, so they get a proportionally smaller parallax correction.
  * everything is then passed through the lens model from panel_geometry.py,
    which straightens the printed section rules.
Output: docs/panel-layout.json and Source/ui/PanelLayout.cpp (generated).
Run from the repo root with Pillow/NumPy/SciPy available.
"""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage, optimize

sys.path.insert(0, str(Path(__file__).resolve().parent))
import panel_geometry as pg  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent

# ---------------------------------------------------------------- element spec
# Positions are raw photo pixels (2191 x 1117). bind = parameter id(s) or action.
E = []
def knob(id, x, y, label, bind, big=False): E.append(dict(type="knob", id=id, x=x, y=y, label=label, bind=bind, big=big))
def button(id, x, y, label, bind, w=62, h=26, style="dark"): E.append(dict(type="button", id=id, x=x, y=y, w=w, h=h, label=label, bind=bind, style=style))
def led(id, x, y, label, bind, value): E.append(dict(type="led", id=id, x=x, y=y, label=label, bind=bind, value=value))
def fader(id, x, y0, y1, label, bind): E.append(dict(type="fader", id=id, x=x, y=y0, y1=y1, label=label, bind=bind))
def text(id, x, y, label, size, align="left", bold=False): E.append(dict(type="text", id=id, x=x, y=y, label=label, size=size, align=align, bold=bold))
def frame(id, x0, y0, x1, y1, shape): E.append(dict(type="frame", id=id, x=x0, y=y0, x1=x1, y1=y1, style=shape))
def arrow(id, x, y, bind): E.append(dict(type="arrow", id=id, x=x, y=y, bind=bind))
def bracket(id, x0, x1, y, label): E.append(dict(type="bracket", id=id, x=x0, x1=x1, y=y, label=label))

# Top row
knob("master_volume", 116, 157, "Volume", "")
button("initialise", 251, 131, "Initialise", "action:initialise")
button("compare", 335, 131, "Compare", "action:compare")
button("audition", 251, 189, "Audition", "action:audition")
button("save", 335, 189, "Save", "action:save")
knob("patch_encoder", 284, 320, "", "action:patch_encoder")
button("patch_down", 251, 437, "", "action:patch_down")
button("patch_up", 335, 437, "", "action:patch_up")
text("patch_pm_label", 293, 471, "–      Patch      +", 12, "centre")
button("anim1", 135, 333, "1", "animate1_hold", w=56, h=56, style="animate")
button("anim2", 135, 409, "2", "animate2_hold", w=56, h=56, style="animate")
button("anim_hold", 135, 470, "Hold", "", w=56, h=18, style="hold")

button("menu_patch", 432, 165, "Patch", "menu:Patch", style="white")
for i, (name, x, y) in enumerate([("Osc", 540, 133), ("Env", 624, 133), ("LFO", 708, 133), ("Arp/Clock", 793, 133),
                                  ("Mod", 540, 192), ("Voice", 624, 192), ("FX", 708, 192), ("Settings", 793, 192)]):
    button("menu_" + name.lower().replace("/", "_"), x, y, name, "menu:" + name, style="white")
for i in range(8):
    led(f"voice{i + 1}", 866 + 33.3 * (i % 4), 152 if i < 4 else 171, "", "voices", i)
    text(f"voice{i + 1}_num", 866 + 33.3 * (i % 4), 131 if i < 4 else 191, str(i + 1), 11, "centre")
text("active_voices", 916, 209, "Active Voices", 11, "centre")
for i, y in enumerate((287, 342, 396)):
    button(f"soft{i + 1}", 432, y, "", f"action:soft{i + 1}")
E.append(dict(type="display", id="display", x=497, y=278, x1=836, y1=382))
button("page_prev", 624, 437, "", "action:page_prev", style="lit")
button("page_next", 708, 437, "", "action:page_next", style="lit")
text("page_select", 666, 471, "◀  Page/Select  ▶", 12, "centre")
knob("data_encoder", 908, 323, "", "action:data_encoder")
button("value_down", 873, 437, "", "action:value_down")
button("value_up", 957, 437, "", "action:value_up")
text("value_label", 915, 471, "–      Value      +", 12, "centre")

knob("arp_gate", 1060, 158, "Gate", "arp_gate")
button("key_latch", 1060, 328, "Key Latch", "arp_key_latch", style="toggle")
button("arp_on", 1060, 437, "On", "arp_on", style="toggle")
knob("glide_time", 1192, 338, "Time", "glide_time")
button("glide_on", 1192, 437, "On", "glide_on", style="toggle")

LFO_POS = {  # measured per LFO: type LED x / y0, type button, fade cap, range LED x / y0, range button, rate cap
    1: dict(tx=1193, ty=119, tb=(1216, 222), fade=(1336, 159), rx=1420, ry=140, rb=(1443, 222), rate=(1565, 160)),
    2: dict(tx=1693, ty=120, tb=(1716, 223), fade=(1844, 160), rx=1916, ry=142, rb=(1940, 224), rate=(2072, 160)),
}
for n, p in LFO_POS.items():
    for i, sym in enumerate(("~tri", "~saw", "~square", "S+H")):
        led(f"lfo{n}_wave_{i}", p["tx"], p["ty"] + 21 * i, sym, f"lfo{n}_wave", i)
    button(f"lfo{n}_type", *p["tb"], "Type", f"cycle:lfo{n}_wave")
    knob(f"lfo{n}_fade", *p["fade"], "Fade Time", f"lfo{n}_fade_time")
    for i, nm in enumerate(("Low", "High", "Sync")):
        led(f"lfo{n}_range_{i}", p["rx"], p["ry"] + 21 * i, nm, f"lfo{n}_range", i)
    button(f"lfo{n}_range_btn", *p["rb"], "Range", f"cycle:lfo{n}_range")
    knob(f"lfo{n}_rate", *p["rate"], "Rate", f"lfo{n}_rate@lfo{n}_range=0|lfo{n}_rate@lfo{n}_range=1|lfo{n}_sync_rate@lfo{n}_range=2")
    bracket(f"lfo{n}_range_rate", p["rb"][0] + 30, p["rate"][0], p["rb"][1] + 21, "")

for i, (nm, x) in enumerate((("Attack", 1308), ("Decay", 1398), ("Sustain", 1488), ("Release", 1576))):
    fader(f"ampenv_{nm.lower()}", x, 302, 468, nm, f"ampenv_{nm.lower()}")
led("modenv_sel_1", 1668, 377, "1", "ui:modenv_select", 0)
led("modenv_sel_2", 1668, 398, "2", "ui:modenv_select", 1)
button("modenv_select", 1691, 438, "Select", "cycle:ui:modenv_select")
for i, (nm, x) in enumerate((("Attack", 1788), ("Decay", 1876), ("Sustain", 1965), ("Release", 2053))):
    fader(f"modenv_{nm.lower()}", x, 302, 468, nm, f"modenv1_{nm.lower()}@ui:modenv_select=0|modenv2_{nm.lower()}@ui:modenv_select=1")

# Oscillators
for n, dy in ((1, 0), (2, 173.5), (3, 347)):
    led(f"osc{n}_range_16", 113, 560 + dy, "16'", f"osc{n}_range", 63)
    led(f"osc{n}_range_8", 113, 581 + dy, "8'", f"osc{n}_range", 64)
    led(f"osc{n}_range_4", 156, 560 + dy, "4'", f"osc{n}_range", 65)
    led(f"osc{n}_range_2", 156, 581 + dy, "2'", f"osc{n}_range", 66)
    button(f"osc{n}_range_btn", 135, 621 + dy, "Range", f"cycle:osc{n}_range")
    knob(f"osc{n}_coarse", 225.6, 577 + dy, "Coarse", f"osc{n}_coarse")
    knob(f"osc{n}_fine", 349.5, 577 + dy, "Fine", f"osc{n}_fine")
    for i, (sym, x, y) in enumerate((("~sine", 447, 540), ("~tri", 447, 561), ("~saw", 447, 582), ("~pulse", 489, 540), ("more", 489, 561))):
        led(f"osc{n}_wave_{i}", x, y + dy, sym, f"osc{n}_wave", i)
    button(f"osc{n}_wave_btn", 468, 622 + dy, "Wave", f"cycle:osc{n}_wave")
    knob(f"osc{n}_me2", 567.5, 576 + dy, "Mod Env 2 Depth", f"osc{n}_modenv2_to_pitch")
    knob(f"osc{n}_lfo2", 691, 577 + dy, "LFO 2 Depth", f"osc{n}_lfo2_to_pitch")
    bracket(f"osc{n}_pitch", 575, 697, 651 + dy, "Pitch")
    knob(f"osc{n}_shape", 818, 577 + dy, "Shape Amount",
         f"osc{n}_manual_shape@osc{n}_shape_source=0|osc{n}_modenv1_to_shape@osc{n}_shape_source=1|osc{n}_lfo1_to_shape@osc{n}_shape_source=2")
    for i, (nm, y) in enumerate((("Manual", 540), ("Mod Env 1", 561), ("LFO 1", 582))):
        led(f"osc{n}_src_{i}", 907, y + dy, nm, f"osc{n}_shape_source", i)
    button(f"osc{n}_source_btn", 928, 622 + dy, "", f"cycle:osc{n}_shape_source")
    bracket(f"osc{n}_source", 820, 895, 651 + dy, "")
    text(f"osc{n}_source_label", 929, 651 + dy, "Source", 13, "centre")
    arrow(f"osc{n}_more", 985, 517 + dy, f"menu:Oscillator {n}")
text("oxford", 75, 1067, "OXFORD OSCILLATORS", 20, bold=True)

# Mixer / VCA
knob("mix_osc1", 1063, 577, "Osc 1", "mix_osc1")
knob("mix_osc2", 1063, 753, "Osc 2", "mix_osc2")
knob("mix_osc3", 1062, 930, "Osc 3", "mix_osc3")
knob("vca_gain", 1189, 577, "VCA Gain", "vca_gain")
knob("ring", 1189, 754, "Ring 1•2", "ring_1_2")
knob("noise", 1190, 930, "Noise", "noise_level")
arrow("vca_more", 1235, 517, "menu:Voice")
frame("vca_frame", 1118, 507, 1248, 665, "u")

# Filter
knob("flt_lfo1", 1326, 577, "LFO 1 Depth", "lfo1_to_filter")
knob("flt_osc3", 1451, 577, "Osc 3 Filter Mod", "osc3_to_filter")
knob("flt_track", 1576, 577, "Key Tracking", "filter_key_track")
knob("flt_env", 1326, 755, "Env Depth", "ampenv_to_filter@filter_env_select=0|modenv1_to_filter@filter_env_select=1")
knob("flt_res", 1451, 754, "Resonance", "filter_resonance")
frame("flt_env_bracket", 1272, 812, 1282, 917, "left")
knob("flt_od", 1575, 754, "Overdrive", "filter_overdrive")
led("flt_src_amp", 1297, 832, "Amp Env", "filter_env_select", 0)
led("flt_src_mod", 1297, 853, "Mod Env 1", "filter_env_select", 1)
button("flt_source_btn", 1320, 893, "", "cycle:filter_env_select")
text("flt_source_label", 1320, 921, "Source", 12, "centre")
led("flt_slope_12", 1297, 942, "12 dB", "filter_slope", 0)
led("flt_slope_24", 1297, 962, "24 dB", "filter_slope", 1)
button("flt_slope_btn", 1320, 1001, "Slope", "cycle:filter_slope")
knob("flt_freq", 1450, 931, "Frequency", "filter_frequency", big=True)
led("flt_lp", 1541, 919, "LP", "filter_shape", 0)
led("flt_bp", 1541, 940, "BP", "filter_shape", 1)
led("flt_hp", 1541, 961, "HP", "filter_shape", 2)
button("flt_shape_btn", 1564, 1001, "Shape", "cycle:filter_shape")
arrow("flt_more", 1615, 517, "menu:Filter")

# Effects
knob("dist_level", 1705, 577, "Level", "distortion_level")
knob("chorus_rate", 1845, 578, "Rate", "chorus_rate")
for i in range(3):
    led(f"chorus_type_{i}", 1918, 544 + 21 * i, str(i + 1), "chorus_type", i)
    led(f"reverb_type_{i}", 1918, 886 + 21 * i, str(i + 1), "reverb_type", i)
button("chorus_type_btn", 1940, 626, "Type", "cycle:chorus_type")
knob("chorus_level", 2073, 578, "Level", "chorus_level")
knob("delay_fb", 1706, 755, "Feedback", "delay_feedback")
knob("delay_time", 1844, 753, "Time", "delay_time@delay_sync=0|delay_sync_time@delay_sync=1")
button("delay_sync", 1940, 754, "Sync", "delay_sync", style="toggle")
bracket("delay_time_sync", 1872, 1957, 810, "")
knob("delay_level", 2075, 755, "Level", "delay_level")
button("fx_bypass", 1691, 927, "Bypass", "fx_bypass", style="toggle")
knob("reverb_time", 1844, 930, "Time", "reverb_time")
button("reverb_type_btn", 1940, 968, "Type", "cycle:reverb_type")
knob("reverb_level", 2075, 931, "Level", "reverb_level")
text("eight_voice", 2125, 1070, "EIGHT-VOICE POLYPHONIC SYNTHESISER", 20, "right", bold=True)

# Section titles: (text, x, y) placed just below their rule.
for t, x, y in [("MASTER", 75, 92), ("PATCH", 213, 92), ("MENU", 386, 92), ("ARP", 1011, 92), ("LFO 1", 1127, 92),
                ("LFO 2", 1636, 92), ("ANIMATE", 75, 283), ("GLIDE", 1127, 283), ("AMP ENVELOPE", 1264, 283),
                ("MOD ENVELOPES", 1636, 284), ("OSCILLATOR 1", 72, 518), ("OSCILLATOR 2", 72, 690), ("OSCILLATOR 3", 72, 864),
                ("MIXER", 1011, 518), ("FILTER", 1264, 518), ("DISTORTION", 1636, 519), ("CHORUS", 1761, 519),
                ("DELAY", 1636, 692), ("EFFECTS", 1636, 865), ("REVERB", 1761, 865)]:
    text("title_" + t.lower().replace(" ", "_"), x, y, t, 15, bold=True)

# Every bound parameter must exist in the generated parameter table.
import re  # noqa: E402
known_ids = set(re.findall(r'\{ "([a-z0-9_]+)", ', (ROOT / "Source/peak/core/ParamTable.cpp").read_text()))
for e in E:
    for part in e.get("bind", "").replace("cycle:", "").split("|"):
        for pid in re.split(r"[@=]", part)[::2]:
            if pid and ":" not in pid and not pid.isdigit() and pid not in ("voices",) and pid not in known_ids:
                raise SystemExit(f"{e['id']}: unknown parameter '{pid}'")

# ------------------------------------------------------------ measurement
img =np.asarray(Image.open(pg.PHOTO).convert("RGB")).astype(int)
_, lens, *_ = pg.load()
lum = img.mean(2)
sat = img.max(2) - img.min(2)


def undistort(x, y):
    return pg.model(lens, np.array([[x, y]], float))[0]


def find_caps():
    lab, _ = ndimage.label((lum > 185) & (sat < 40))
    caps = []
    for i, (ys, xs) in enumerate(ndimage.find_objects(lab)):
        w, h = xs.stop - xs.start, ys.stop - ys.start
        if 25 <= w <= 120 and 25 <= h <= 120 and 0.7 < w / h < 1.4 and (lab[ys, xs] == i + 1).sum() > 0.45 * w * h:
            cy, cx = ndimage.center_of_mass(lab == i + 1)
            caps.append((cx, cy, (w + h) / 4))
    return np.array(caps)


def fit_parallax(caps):
    """Fits cap = ring + k (ring - axis) from caps whose printed dot ring is found."""
    dl, _ = ndimage.label((lum > 95) & (sat < 70))
    dots = np.array([(xs.start + ndimage.center_of_mass(dl[ys, xs] == i + 1)[1], ys.start + ndimage.center_of_mass(dl[ys, xs] == i + 1)[0])
                     for i, (ys, xs) in enumerate(ndimage.find_objects(dl)) if xs.stop - xs.start <= 7 and ys.stop - ys.start <= 7])
    rng = np.random.default_rng(1)
    pairs = []
    for cx, cy, cr in caps:
        if not 20 < cr < 24:
            continue
        near = dots[np.hypot(dots[:, 0] - cx, dots[:, 1] - cy) < cr * 4.2]
        best = None
        for _ in range(2000 if len(near) >= 5 else 0):
            a, b, c = near[rng.choice(len(near), 3, replace=False)]
            d = 2 * (a[0] * (b[1] - c[1]) + b[0] * (c[1] - a[1]) + c[0] * (a[1] - b[1]))
            if abs(d) < 1e-6:
                continue
            ux = ((a @ a) * (b[1] - c[1]) + (b @ b) * (c[1] - a[1]) + (c @ c) * (a[1] - b[1])) / d
            uy = ((a @ a) * (c[0] - b[0]) + (b @ b) * (a[0] - c[0]) + (c @ c) * (b[0] - a[0])) / d
            r = np.hypot(a[0] - ux, a[1] - uy)
            if not 47.6 < r < 48.8:
                continue
            inl = near[abs(np.hypot(near[:, 0] - ux, near[:, 1] - uy) - r) < 1.8]
            if best is None or len(inl) > len(best):
                best = inl
        if best is not None and len(best) >= 9:
            A = np.c_[2 * best, np.ones(len(best))]
            sol, *_ = np.linalg.lstsq(A, (best ** 2).sum(1), rcond=None)
            pairs.append((sol[0], sol[1], cx, cy))
    p = np.array(pairs)

    def res(v):
        k, ax, ay = v
        return np.r_[p[:, 2] - (p[:, 0] + k * (p[:, 0] - ax)), p[:, 3] - (p[:, 1] + k * (p[:, 1] - ay))]

    # Some caps pick up a false ring from label text: fit, drop knobs off by > 4 px, refit.
    for _ in range(3):
        sol = optimize.least_squares(res, [0.02, 1100, 600], loss="soft_l1", f_scale=2.0)
        err = np.hypot(*sol.fun.reshape(2, -1))
        p = p[err < 4.0] if (err >= 4.0).any() else p
    sol = optimize.least_squares(res, sol.x)
    return sol.x, len(p), np.sqrt((sol.fun ** 2).mean())


caps = find_caps()
(k, ax, ay), nfit, rms = fit_parallax(caps)
print(f"parallax: k={k:.4f} axis=({ax:.0f},{ay:.0f}) from {nfit} knobs, RMS {rms:.2f}px")
BUTTON_K = k * 0.2  # buttons stand ~1/5 as tall as knob caps

out = []
for e in E:
    e = dict(e)
    if e["type"] == "knob":
        d = np.hypot(caps[:, 0] - e["x"], caps[:, 1] - e["y"])
        i = int(d.argmin())
        if d[i] < 25:
            e["x"], e["y"] = caps[i, 0], caps[i, 1]
        # invert cap = ring + k (ring - axis)
        e["x"], e["y"] = (e["x"] + k * ax) / (1 + k), (e["y"] + k * ay) / (1 + k)
    elif e["type"] == "button":
        e["x"], e["y"] = (e["x"] + BUTTON_K * ax) / (1 + BUTTON_K), (e["y"] + BUTTON_K * ay) / (1 + BUTTON_K)
    x, y = undistort(e["x"], e["y"])
    e["x"], e["y"] = round(float(x), 1), round(float(y), 1)
    if "x1" in e:
        x1, y1 = undistort(e["x1"], e.get("y1", e["y"]))
        e["x1"] = round(float(x1), 1)
        if "y1" in e:
            e["y1"] = round(float(y1), 1)
    elif "y1" in e:
        e["y1"] = round(float(undistort(e["x"], e["y1"])[1]), 1)
    out.append(e)

rules = []
for s in pg.find_rule_segments(img):
    a = undistort(*s[0]); b = undistort(*s[-1])
    rules.append([round(float(a[0]), 1), round(float((a[1] + b[1]) / 2), 1), round(float(b[0]), 1)])

c0 = undistort(58, 12); c1 = undistort(2137, 1100)
layout = dict(source="docs/reference/peak-front-panel.png (lens- and parallax-corrected; tools/measure_panel.py)",
              parallax=dict(k=round(k, 5), axis=[round(ax, 1), round(ay, 1)], knobsFitted=nfit, rmsPx=round(rms, 2)),
              panel=[round(float(c0[0]), 1), round(float(c0[1]), 1), round(float(c1[0]), 1), round(float(c1[1]), 1)],
              rules=rules, elements=out)
(ROOT / "docs/panel-layout.json").write_text(json.dumps(layout, indent=1))


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def fl(v):
    return f"{float(v):.1f}f"


rows = []
for e in out:
    rows.append("    {{ PanelElement::{t}, {id}, {x}, {y}, {x1}, {y1}, {w}, {h}, {label}, {bind}, {val}, {flag}, {style} }},".format(
        t=e["type"], id=cstr(e["id"]), x=fl(e["x"]), y=fl(e["y"]), x1=fl(e.get("x1", 0)), y1=fl(e.get("y1", 0)),
        w=fl(e.get("w", e.get("size", 0))), h=fl(e.get("h", 0)), label=cstr(e.get("label", "")), bind=cstr(e.get("bind", "")),
        val=e.get("value", 0), flag="true" if e.get("big") or e.get("bold") else "false",
        style=cstr(e.get("style", e.get("align", "")))))
rule_rows = [f"    {{ {fl(a)}, {fl(y)}, {fl(b)} }}," for a, y, b in rules]
(ROOT / "Source/ui/PanelLayout.cpp").write_text(f"""// GENERATED by tools/measure_panel.py from the reference photo. Do not edit.
#include "PanelLayout.h"

const PanelLayout& PanelLayout::get()
{{
    static const PanelLayout layout {{
        {{ {", ".join(fl(v) for v in layout["panel"])} }},
        {{
{chr(10).join(rule_rows)}
        }},
        {{
{chr(10).join(rows)}
        }}
    }};
    return layout;
}}
""")
print(f"{len(out)} elements, {len(rules)} rules -> docs/panel-layout.json, Source/ui/PanelLayout.cpp")
