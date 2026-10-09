#!/usr/bin/env python3
r"""Bake the editor's shell art from the generated device photo.

    python3 Tools/make_editor_shell.py Assets/DeviceShell-fullbleed.png \
        Assets/DeviceShell-editor.png Assets/ShellGlow.png

Two changes, both made in the artwork so nothing has to be pasted over it at run time:

1. The footer plate keeps its molded outline, rim and engraved bay dividers, but its four
   placeholder pills are painted out. The editor draws MOTION, RANDOM, LOCKS and the
   UNDO / REDO / BYPASS keys into those bays, so the slanted plate end and the speaker
   grill beside it stay exactly as moulded.
2. A 70 px band of circuit board is added below the plate for the INFO key. The side rails
   are extended where they run straight (row 1262) and the board is extended through a
   component-free row (1276) with board texture from the strip above, so the screw boss,
   the grill and the bottom corners are never cut or stretched.

The optional third output is the glow mask: where light from inside shows through the
translucent shell (rails and edges most, the board a little, never the LCDs or plates). The
editor tints it with the live sound, so the shell glows with what SHIFTY is doing.

Needs Python 3 with numpy and Pillow. The output is deterministic.
"""
import sys

import numpy as np
from PIL import Image

BAND = 70                    # rows added for the INFO key (editor height = shell + BAND)
WALL_SPLIT, BOARD_SPLIT, CORNER_SPLIT = 1262, 1276, 1270
BOARD_LEFT, BOARD_RIGHT = 99, 1016   # columns between the side rails' inner edges
CORNER_FADE = (962, 986)             # right of this the board splits above the corner's glow
WELLS = [(192, 1135, 315, 1197), (369, 1135, 496, 1198), (552, 1135, 675, 1197), (726, 1135, 851, 1198)]
WELL_PAD = 7


def coons_fill(block):
    """Fill a block's interior from its one-pixel border (bilinearly blended Coons patch)."""
    h, w, _ = block.shape
    top, bottom, left, right = block[0], block[-1], block[:, 0], block[:, -1]
    u = np.linspace(0.0, 1.0, w)[None, :, None]
    v = np.linspace(0.0, 1.0, h)[:, None, None]
    lofted_v = (1 - v) * top[None] + v * bottom[None]
    lofted_u = (1 - u) * left[:, None] + u * right[:, None]
    corners = ((1 - u) * (1 - v) * block[0, 0] + u * (1 - v) * block[0, -1]
               + (1 - u) * v * block[-1, 0] + u * v * block[-1, -1])
    return lofted_v + lofted_u - corners


def smooth_run(line, sigma=5.0):
    """Gaussian-smooth an (n, 3) run of colours along n, holding its ends."""
    radius = int(3 * sigma)
    k = np.exp(-0.5 * (np.arange(-radius, radius + 1) / sigma) ** 2)
    k /= k.sum()
    padded = np.pad(line, ((radius, radius), (0, 0)), mode="edge")
    return np.stack([np.convolve(padded[:, c], k, mode="valid") for c in range(3)], axis=1)


def paint_out_wells(img, rng):
    for x0, y0, x1, y1 in WELLS:
        ys = slice(y0 - WELL_PAD, y1 + WELL_PAD + 1)
        xs = slice(x0 - WELL_PAD, x1 + WELL_PAD + 1)
        block = img[ys, xs].copy()
        # Average the border over three pixels and smooth it along its length, so the patch picks
        # up the plate's shading but not its grain (grain would streak right across the patch).
        for side in (0, -1):
            block[side, :] = smooth_run(img[ys, xs][[0, 1, 2] if side == 0 else [-3, -2, -1]].mean(axis=0))
            block[:, side] = smooth_run(img[ys, xs][:, [0, 1, 2] if side == 0 else [-3, -2, -1]].mean(axis=1))
        fill = coons_fill(block)
        fill += rng.normal(0.0, 1.5, fill.shape)
        img[ys, xs][1:-1, 1:-1] = fill[1:-1, 1:-1]


def board_band(img, split):
    """70 rows of circuit board for the middle columns, made from the strip under the plate."""
    width = BOARD_RIGHT - BOARD_LEFT
    tile_rows = img[1245:1276, 160:974].copy()          # clean rows at both ends, two rows of parts between
    period, fade = 790, 24
    # Make the strip wrap seamlessly: its last columns fade into its first.
    ramp = np.linspace(0.0, 1.0, fade)[None, :, None]
    periodic = tile_rows[:, :period].copy()
    periodic[:, :fade] = (1 - ramp) * tile_rows[:, period:period + fade] + ramp * tile_rows[:, :fade]

    def tile(shift, flip):
        cols = (np.arange(width) + shift) % period
        t = periodic[:, cols]
        return t[:, ::-1] if flip else t

    # Level every row to the board colour either side of the band, keeping the parts' detail.
    target = 0.5 * (np.median(img[split - 1, BOARD_LEFT:BOARD_RIGHT], axis=0)
                    + np.median(img[split, BOARD_LEFT:BOARD_RIGHT], axis=0))

    def levelled(t):
        base = np.median(t, axis=1, keepdims=True)
        # Pale plastic clips would read as obvious copies: sink them into the board.
        pale = t[..., 2:3] > 78
        t = np.where(pale, base + 0.25 * (t - base), t)
        return t - base + target

    tiles = [(0, levelled(tile(331, False))), (19, levelled(tile(97, True))), (39, levelled(tile(563, False)))]
    band = np.zeros((BAND, width, 3))
    weight = np.zeros((BAND, 1, 1))
    for top, t in tiles:
        rows = t.shape[0]
        w = np.ones((rows, 1, 1))
        overlap = 12
        if top > 0:
            w[:overlap, 0, 0] = np.linspace(0.0, 1.0, overlap + 2)[1:-1]
        if top + rows < BAND:
            w[-overlap:, 0, 0] = np.linspace(1.0, 0.0, overlap + 2)[1:-1]
        band[top:top + rows] += w * t
        weight[top:top + rows] += w
    band /= weight

    # Fade the parts out toward the band's edges. Top and bottom meet the clean rows either side
    # of the split; beside each rail the board continues as the rail rows above it, softened so
    # nothing reads as a stretched streak.
    def smooth_ramp(n, length):
        r = np.clip(np.arange(n) / float(length), 0.0, 1.0)
        return r * r * (3 - 2 * r)

    cut = img[split - 2:split + 2, BOARD_LEFT:BOARD_RIGHT]
    edge = np.median(cut, axis=0)[None]
    side = img[WALL_SPLIT - 4:WALL_SPLIT + 5, BOARD_LEFT:BOARD_RIGHT].mean(axis=0)
    kernel = np.exp(-0.5 * (np.arange(-15, 16) / 5.0) ** 2)
    kernel /= kernel.sum()
    padded = np.pad(side, ((15, 15), (0, 0)), mode="edge")
    side = np.stack([np.convolve(padded[:, c], kernel, mode="valid") for c in range(3)], axis=1)[None]
    rows = np.minimum(smooth_ramp(BAND, 9), smooth_ramp(BAND, 9)[::-1])[:, None, None]
    cols = np.minimum(smooth_ramp(width, 48), smooth_ramp(width, 48)[::-1])[None, :, None]
    inner = cols * band + (1 - cols) * side
    return (1 - rows) * edge + rows * inner


def main(src, dst, glow_dst=None):
    img = np.asarray(Image.open(src).convert("RGB")).astype(np.float64)
    rng = np.random.default_rng(1262)
    paint_out_wells(img, rng)

    h, w, _ = img.shape
    out = np.zeros((h + BAND, w, 3))
    for cols in (slice(0, BOARD_LEFT), slice(BOARD_RIGHT, w)):
        out[:WALL_SPLIT, cols] = img[:WALL_SPLIT, cols]
        out[WALL_SPLIT:WALL_SPLIT + BAND, cols] = img[WALL_SPLIT - 2:WALL_SPLIT + 3, cols].mean(axis=0)
        out[WALL_SPLIT + BAND:, cols] = img[WALL_SPLIT:, cols]
    mid = slice(BOARD_LEFT, BOARD_RIGHT)

    def extended(split):
        board = np.zeros((h + BAND, BOARD_RIGHT - BOARD_LEFT, 3))
        board[:split] = img[:split, mid]
        board[split:split + BAND] = board_band(img, split)
        board[split + BAND:] = img[split:, mid]
        return board

    # Most of the board splits through the clean row under the parts. Beside the right rail the
    # rail's corner glow starts at 1272, so that corner splits above it and the glow moves down
    # with the rail; the two meet in a crossfade across plain board.
    x = np.arange(BOARD_LEFT, BOARD_RIGHT)
    blend = np.clip((x - CORNER_FADE[0]) / float(CORNER_FADE[1] - CORNER_FADE[0]), 0.0, 1.0)[None, :, None]
    out[:, mid] = (1 - blend) * extended(BOARD_SPLIT) + blend * extended(CORNER_SPLIT)
    Image.fromarray(np.clip(np.rint(out), 0, 255).astype(np.uint8)).save(dst, optimize=True)
    if glow_dst:
        save_glow_mask(out, glow_dst)


# Solid parts that never light up: the two LCDs with their bezels, the page-tab slot and the
# three molded plates (the footer plate's end follows its slant). Shell coordinates.
SOLID_RECTS = [((128, 163, 995, 520), 30), ((582, 110, 925, 166), 14), ((160, 525, 962, 982), 40),
               ((128, 992, 995, 1116), 22)]
FOOTER_PLATE = [(170, 1123), (880, 1123), (902, 1147), (902, 1160), (865, 1225), (848, 1238),
                (170, 1238), (147, 1215), (147, 1146)]


def blur(a, sigma):
    """Separable Gaussian blur of a 2-D array."""
    radius = int(3 * sigma)
    k = np.exp(-0.5 * (np.arange(-radius, radius + 1) / sigma) ** 2)
    k /= k.sum()
    a = np.pad(a, radius, mode="edge")
    a = np.apply_along_axis(lambda r: np.convolve(r, k, mode="valid"), 1, a)
    return np.apply_along_axis(lambda c: np.convolve(c, k, mode="valid"), 0, a)


def save_glow_mask(shell, dst):
    """Where light shows through the translucent shell: the rails and edges most, the board a little.

    The editor fills this mask with the glow colour, so it is white with the mask as alpha."""
    from PIL import ImageDraw
    h, w, _ = shell.shape
    lum = (0.2126 * shell[..., 0] + 0.7152 * shell[..., 1] + 0.0722 * shell[..., 2]) / 255.0
    solid = Image.new("L", (w, h), 0)
    draw = ImageDraw.Draw(solid)
    for box, radius in SOLID_RECTS:
        draw.rounded_rectangle(box, radius=radius, fill=255)
    draw.polygon(FOOTER_PLATE, fill=255)
    solid = blur(np.asarray(solid, dtype=np.float64) / 255.0, 3.0)
    t = np.clip((lum - 0.30) / 0.55, 0.0, 1.0)
    body = 0.30 + 0.70 * t * t * (3 - 2 * t)
    bloom = blur(np.clip((lum - 0.72) / 0.2, 0.0, 1.0), 12.0)
    mask = np.clip(body * (1 - solid) + 0.55 * bloom * (1 - solid ** 0.5), 0.0, 1.0)
    rgba = np.zeros((h, w, 4), np.uint8)
    rgba[..., :3] = 255
    rgba[..., 3] = np.rint(mask * 255).astype(np.uint8)
    Image.fromarray(rgba, "RGBA").save(dst, optimize=True)


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    main(*sys.argv[1:])
