#!/usr/bin/env python3
"""Compare pictures of the original game with pictures of the port.

  compare.py pair A.png B.png [--out side-by-side.png]
  compare.py walk DIR_A DIR_B --out DIR [--worst 12]

`pair` measures two pictures and can write them side by side with their difference. `walk` takes two folders of
pictures in reading order (as `orewin walk` and the port's test host write them), pairs them up even when one side
has a picture more or less here and there, and reports the pairs that differ most.

The scene (above --split) and the text box (below it) are measured apart: Windows draws the letters with another
font than Apple's systems do, so the text box never matches to the pixel, while the scene should.

Needs Pillow and NumPy.
"""
import argparse
import csv
from pathlib import Path

import numpy as np
from PIL import Image

SIZE = (800, 600)


def load(path):
    image = Image.open(path).convert("RGB")
    if image.size != SIZE:
        image = image.resize(SIZE, Image.BILINEAR)
    return np.asarray(image, dtype=np.int16)


def difference(a, b):
    """The largest difference of a colour channel, per pixel (0..255)."""
    return np.abs(a - b).max(axis=2)


def measure(diff):
    return {"mean": float(diff.mean()), "over16": float((diff > 16).mean()), "over48": float((diff > 48).mean())}


def boxes(diff, cell=16, level=24, limit=6):
    """Rectangles (x0, y0, x1, y1) around the areas that differ, largest first."""
    h, w = diff.shape
    grid = (diff[:h - h % cell, :w - w % cell].reshape(h // cell, cell, w // cell, cell) > level).mean(axis=(1, 3)) > 0.05
    seen = np.zeros_like(grid)
    found = []
    for y, x in zip(*np.nonzero(grid)):
        if seen[y, x]:
            continue
        stack, cells = [(y, x)], []
        seen[y, x] = True
        while stack:
            cy, cx = stack.pop()
            cells.append((cy, cx))
            for ny, nx in ((cy - 1, cx), (cy + 1, cx), (cy, cx - 1), (cy, cx + 1)):
                if 0 <= ny < grid.shape[0] and 0 <= nx < grid.shape[1] and grid[ny, nx] and not seen[ny, nx]:
                    seen[ny, nx] = True
                    stack.append((ny, nx))
        ys, xs = [c[0] for c in cells], [c[1] for c in cells]
        found.append((len(cells), (min(xs) * cell, min(ys) * cell, (max(xs) + 1) * cell, (max(ys) + 1) * cell)))
    return [box for _, box in sorted(found, reverse=True)[:limit]]


def side_by_side(a, b, diff):
    sheet = Image.new("RGB", (SIZE[0] * 3 + 16, SIZE[1]), (40, 40, 40))
    sheet.paste(Image.fromarray(a.astype(np.uint8)), (0, 0))
    sheet.paste(Image.fromarray(b.astype(np.uint8)), (SIZE[0] + 8, 0))
    heat = np.clip(diff * 4, 0, 255).astype(np.uint8)
    sheet.paste(Image.merge("RGB", [Image.fromarray(heat), Image.fromarray(heat // 3), Image.fromarray(heat // 3)]),
                (SIZE[0] * 2 + 16, 0))
    return sheet


def compare(a, b, split):
    diff = difference(a, b)
    return diff, {"all": measure(diff), "scene": measure(diff[:split]), "text": measure(diff[split:]),
                  "boxes": boxes(diff)}


def describe(result):
    parts = []
    for name in ("scene", "text"):
        m = result[name]
        parts.append(f"{name}: mean {m['mean']:.2f}, {m['over16'] * 100:.2f}% over 16, {m['over48'] * 100:.2f}% over 48")
    if result["boxes"]:
        parts.append("areas " + " ".join("(%d,%d)-(%d,%d)" % box for box in result["boxes"]))
    return "; ".join(parts)


def pair(args):
    a, b = load(args.a), load(args.b)
    diff, result = compare(a, b, args.split)
    print(describe(result))
    if args.out:
        side_by_side(a, b, diff).save(args.out)


def signature(picture, split):
    """A small grey picture of the scene: enough to tell scenes apart, blind to letter shapes."""
    grey = Image.fromarray(picture[:split].astype(np.uint8)).convert("L").resize((64, 36), Image.BILINEAR)
    return np.asarray(grey, dtype=np.float32) / 255


def align(sa, sb, gap=0.12):
    """Pairs the two sequences so that like stands by like; None marks a picture without partner."""
    n, m = len(sa), len(sb)
    cost = np.zeros((n + 1, m + 1), dtype=np.float32)
    cost[:, 0] = np.arange(n + 1) * gap
    cost[0, :] = np.arange(m + 1) * gap
    step = np.zeros((n + 1, m + 1), dtype=np.uint8)
    step[1:, 0], step[0, 1:] = 1, 2
    for i in range(1, n + 1):
        apart = np.abs(sb - sa[i - 1]).mean(axis=(1, 2))
        for j in range(1, m + 1):
            choices = (cost[i - 1, j - 1] + apart[j - 1], cost[i - 1, j] + gap, cost[i, j - 1] + gap)
            step[i, j] = int(np.argmin(choices))
            cost[i, j] = choices[step[i, j]]
    pairs, i, j = [], n, m
    while i or j:
        s = step[i, j]
        if s == 0:
            pairs.append((i - 1, j - 1))
            i, j = i - 1, j - 1
        elif s == 1:
            pairs.append((i - 1, None))
            i -= 1
        else:
            pairs.append((None, j - 1))
            j -= 1
    return pairs[::-1]


def walk(args):
    files_a, files_b = sorted(args.a.glob(args.glob)), sorted(args.b.glob(args.glob))
    if not files_a or not files_b:
        raise SystemExit("no pictures found")
    pictures_a, pictures_b = [load(f) for f in files_a], [load(f) for f in files_b]
    sa = np.stack([signature(p, args.split) for p in pictures_a])
    sb = np.stack([signature(p, args.split) for p in pictures_b])
    args.out.mkdir(parents=True, exist_ok=True)
    rows = []
    for ia, ib in align(sa, sb):
        row = {"a": files_a[ia].name if ia is not None else "", "b": files_b[ib].name if ib is not None else ""}
        if ia is not None and ib is not None:
            diff, result = compare(pictures_a[ia], pictures_b[ib], args.split)
            row.update(scene_mean=round(result["scene"]["mean"], 3), scene_over16=round(result["scene"]["over16"], 5),
                       text_mean=round(result["text"]["mean"], 3), text_over16=round(result["text"]["over16"], 5),
                       areas=" ".join("%d,%d,%d,%d" % box for box in result["boxes"]))
            row["_pair"] = (ia, ib, diff)
        rows.append(row)
    fields = ["a", "b", "scene_mean", "scene_over16", "text_mean", "text_over16", "areas"]
    with open(args.out / "pairs.csv", "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)
    paired = [r for r in rows if "_pair" in r]
    alone_a = sum(1 for r in rows if not r["b"])
    alone_b = sum(1 for r in rows if not r["a"])
    print(f"{len(files_a)} and {len(files_b)} pictures: {len(paired)} pairs, {alone_a} only in A, {alone_b} only in B")
    if paired:
        scene = np.array([r["scene_over16"] for r in paired])
        print(f"scene differing over 16: median {np.median(scene) * 100:.2f}%, worst {scene.max() * 100:.2f}%; "
              f"{int((scene < 0.005).sum())} pairs under 0.5%")
    worst = sorted(paired, key=lambda r: r["scene_over16"], reverse=True)[:args.worst]
    for rank, r in enumerate(worst):
        ia, ib, diff = r["_pair"]
        name = f"worst_{rank:02d}_{Path(r['a']).stem}_{Path(r['b']).stem}.png"
        side_by_side(pictures_a[ia], pictures_b[ib], diff).resize((1208, 300), Image.BILINEAR).save(args.out / name)
        print(f"{name}: scene {r['scene_over16'] * 100:.2f}% over 16, text {r['text_over16'] * 100:.2f}%")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--split", type=int, default=440, help="the row where the text box begins (default 440)")
    commands = parser.add_subparsers(dest="command", required=True)
    p = commands.add_parser("pair")
    p.add_argument("a", type=Path)
    p.add_argument("b", type=Path)
    p.add_argument("--out", type=Path)
    p.set_defaults(run=pair)
    w = commands.add_parser("walk")
    w.add_argument("a", type=Path)
    w.add_argument("b", type=Path)
    w.add_argument("--out", type=Path, required=True)
    w.add_argument("--glob", default="*.png")
    w.add_argument("--worst", type=int, default=12)
    w.set_defaults(run=walk)
    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
