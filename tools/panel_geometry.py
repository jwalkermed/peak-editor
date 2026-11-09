#!/usr/bin/env python3
"""Lens/perspective correction for docs/reference/peak-front-panel.png.

The real panel is flat and rectangular: its printed blue section rules are
straight, rules in one row share a y, and section edges in one column share an
x. This fits a radial lens model plus rotation so that holds, and exposes
undistort(x, y) -> rectified photo pixels. Needs Pillow, NumPy, SciPy.
"""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage, optimize

PHOTO = Path(__file__).resolve().parent.parent / "docs/reference/peak-front-panel.png"


def find_rule_segments(img):
    r, g, b = (img[..., i].astype(int) for i in range(3))
    blue = (b > 170) & (b - r > 70) & (g > 110) & (g < b)
    lab, _ = ndimage.label(blue)
    segs = []
    for i, (ys, xs) in enumerate(ndimage.find_objects(lab)):
        if xs.stop - xs.start > 80 and ys.stop - ys.start < 12:
            mask = lab[ys, xs] == i + 1
            pts = []
            w = xs.stop - xs.start
            for j in sorted(set(range(0, w, 4)) | {w - 1}):
                rows = np.nonzero(mask[:, j])[0]
                if len(rows):
                    pts.append((xs.start + j, ys.start + rows.mean()))
            segs.append(np.array(pts))
    return segs


def model(params, pts):
    cx, cy, k1, k2, theta = params
    d = pts - (cx, cy)
    r2 = (d ** 2).sum(1, keepdims=True) / 1000.0 ** 2
    u = d * (1 + k1 * r2 + k2 * r2 ** 2)
    c, s = np.cos(theta), np.sin(theta)
    return np.stack([c * u[:, 0] - s * u[:, 1], s * u[:, 0] + c * u[:, 1]], 1) + (cx, cy)


def fit(segs, width, height):
    rows = {}
    for s in segs:
        rows.setdefault(round(s[:, 1].mean() / 30), []).append(s)
    ends = []  # (x, y) of segment ends, grouped by column
    for s in segs:
        ends += [tuple(s[0]), tuple(s[-1])]
    ends = np.array(ends)
    cols = {}
    for e in ends:
        cols.setdefault(round(e[0] / 12), []).append(e)
    cols = [np.array(v) for v in cols.values() if len(v) >= 3]
    rowpts = [np.vstack(v) for v in rows.values()]

    def residuals(p):
        res = []
        for pts in rowpts:
            y = model(p, pts)[:, 1]
            res.append(y - y.mean())
        for pts in cols:
            x = model(p, pts)[:, 0]
            res.append((x - x.mean()) * 3)  # few points per column; weight them up
        return np.concatenate(res)

    p0 = [width / 2, height / 2, 0.0, 0.0, 0.0]
    before = residuals(p0)
    sol = optimize.least_squares(residuals, p0, x_scale=[100, 100, 1e-3, 1e-3, 1e-3])
    return sol.x, before, residuals(sol.x), len(rowpts), len(cols)


def load():
    img = np.asarray(Image.open(PHOTO).convert("RGB"))
    segs = find_rule_segments(img)
    params, before, after, nrows, ncols = fit(segs, img.shape[1], img.shape[0])
    return img, params, before, after, nrows, ncols


if __name__ == "__main__":
    img, p, before, after, nrows, ncols = load()
    print(f"{nrows} rule rows, {ncols} column groups")
    print(f"centre ({p[0]:.0f}, {p[1]:.0f})  k1 {p[2]:+.5f}  k2 {p[3]:+.5f}  rotation {np.degrees(p[4]):+.3f} deg")
    print(f"residual RMS before {np.sqrt((before ** 2).mean()):.2f}px, max {abs(before).max():.2f}px")
    print(f"residual RMS after  {np.sqrt((after ** 2).mean()):.2f}px, max {abs(after).max():.2f}px")
    if len(sys.argv) > 1:
        json.dump(dict(centre=[p[0], p[1]], k1=p[2], k2=p[3], rotation=p[4]), open(sys.argv[1], "w"), indent=1)
