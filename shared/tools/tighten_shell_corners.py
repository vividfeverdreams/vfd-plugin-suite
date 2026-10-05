#!/usr/bin/env python3
"""Tighten the rounded corners of a plugin shell PNG so the device can fill its editor window.

The shell artwork is a photographed-looking device with large rounded corners floating on black. Inside a
DAW window that leaves black wedges at every corner. This tool keeps the artwork's size and coordinates
(so no control positions move) and radially remaps each corner zone: the rim's real bevel, highlights and
resin are stretched outward from the corner's centre to fill a much smaller corner radius. Content near
the corner centre (screws, board parts) is left untouched.

Usage:
  tighten_shell_corners.py input.png output.png --edges L T R B --radius TL TR BR BL --new-radius R
  [--inner 0.42] [--matte] [--background r,g,b]

Edges are the shell's straight-edge pixel positions (inclusive) and radii are the source corner radii, all
in source pixels. --matte cuts opaque artwork out of its baked backdrop first; --background flattens the
result onto an opaque colour (for RGB shells).
"""
import argparse
import numpy as np
from PIL import Image


def sdf_rounded_rect(px, py, left, top, right, bottom, radius):
    """Signed distance, positive inside, to a rounded rectangle."""
    cx, cy = (left + right) * 0.5, (top + bottom) * 0.5
    hx, hy = (right - left) * 0.5 - radius, (bottom - top) * 0.5 - radius
    qx, qy = np.abs(px - cx) - hx, np.abs(py - cy) - hy
    outside = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0)
    return radius - outside


def bilinear(image, x, y):
    h, w = image.shape[:2]
    x = np.clip(x, 0, w - 1.001)
    y = np.clip(y, 0, h - 1.001)
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = (x - x0)[..., None], (y - y0)[..., None]
    a, b = image[y0, x0], image[y0, x0 + 1]
    c, d = image[y0 + 1, x0], image[y0 + 1, x0 + 1]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def tighten(src, edges, radii, new_radius, inner):
    left, top, right, bottom = [float(v) for v in edges]
    # Work in premultiplied alpha so stretched edges never pick up black fringes.
    rgba = src.astype(np.float64) / 255.0
    rgba[..., :3] *= rgba[..., 3:4]
    out = rgba.copy()
    corners = [  # (radius, centre x, centre y, x sign, y sign)
        (radii[0], left + radii[0], top + radii[0], -1, -1),
        (radii[1], right - radii[1], top + radii[1], 1, -1),
        (radii[2], right - radii[2], bottom - radii[2], 1, 1),
        (radii[3], left + radii[3], bottom - radii[3], -1, 1),
    ]
    for radius, cx, cy, sx, sy in corners:
        x0, x1 = (int(np.floor(cx - radius)), int(np.ceil(cx))) if sx < 0 else (int(np.floor(cx)), int(np.ceil(cx + radius)))
        y0, y1 = (int(np.floor(cy - radius)), int(np.ceil(cy))) if sy < 0 else (int(np.floor(cy)), int(np.ceil(cy + radius)))
        ys, xs = np.mgrid[y0:y1 + 1, x0:x1 + 1].astype(np.float64)
        vx, vy = xs - cx, ys - cy
        rho = np.hypot(vx, vy)
        ux, uy = vx / np.maximum(rho, 1e-9), vy / np.maximum(rho, 1e-9)
        # Distance from the corner centre to the new (tighter) outline along each ray.
        lo, hi = np.full_like(rho, radius * 0.5), np.full_like(rho, radius * 1.5)
        for _ in range(28):
            mid = (lo + hi) * 0.5
            inside = sdf_rounded_rect(cx + ux * mid, cy + uy * mid, left, top, right, bottom, new_radius) > 0
            lo, hi = np.where(inside, mid, lo), np.where(inside, hi, mid)
        rho_new = (lo + hi) * 0.5
        rho_inner = radius * inner
        stretch = np.where(rho > rho_inner,
                           rho_inner + (rho - rho_inner) * (radius - rho_inner) / np.maximum(rho_new - rho_inner, 1e-6),
                           rho)
        sample = bilinear(rgba, cx + ux * stretch, cy + uy * stretch)
        zone = (rho > rho_inner)
        # Only rewrite the corner quadrant; outside the tighter outline is transparent.
        quadrant = ((vx * sx) >= 0) & ((vy * sy) >= 0)
        inside_new = sdf_rounded_rect(xs, ys, left, top, right, bottom, new_radius)
        coverage = np.clip(inside_new + 0.5, 0, 1)[..., None]
        region = out[y0:y1 + 1, x0:x1 + 1]
        mask = (zone & quadrant)[..., None]
        region[:] = np.where(mask, sample * coverage, region)
    alpha = out[..., 3:4]
    out[..., :3] = np.where(alpha > 1e-6, out[..., :3] / np.maximum(alpha, 1e-6), 0)
    return np.clip(out * 255.0 + 0.5, 0, 255).astype(np.uint8)


def outline_matte(shape, edges, radii):
    """Coverage of the device outline (per-corner radii), for opaque RGB artwork with a baked backdrop."""
    h, w = shape
    left, top, right, bottom = [float(v) for v in edges]
    # Same convention as tighten(): integer pixel coordinates, straight edges at the given pixels.
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float64)
    cx, cy = (left + right) * 0.5, (top + bottom) * 0.5
    radius = np.where(ys < cy, np.where(xs < cx, radii[0], radii[1]), np.where(xs < cx, radii[3], radii[2]))
    return np.clip(sdf_rounded_rect(xs, ys, left, top, right, bottom, radius) + 0.5, 0, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input")
    parser.add_argument("output")
    parser.add_argument("--edges", nargs=4, type=float, required=True, metavar=("L", "T", "R", "B"))
    parser.add_argument("--radius", nargs=4, type=float, required=True, metavar=("TL", "TR", "BR", "BL"))
    parser.add_argument("--new-radius", type=float, default=14)
    parser.add_argument("--inner", type=float, default=0.42, help="fraction of each corner radius left untouched")
    parser.add_argument("--background", default=None, help="r,g,b to flatten onto")
    parser.add_argument("--matte", action="store_true",
                        help="derive alpha from the outline (opaque artwork whose backdrop is baked in)")
    args = parser.parse_args()
    src = np.asarray(Image.open(args.input).convert("RGBA")).copy()
    if args.matte:
        src[..., 3] = np.round(outline_matte(src.shape[:2], args.edges, args.radius) * 255).astype(np.uint8)
    result = Image.fromarray(tighten(src, args.edges, args.radius, args.new_radius, args.inner), "RGBA")
    if args.background:
        colour = tuple(int(v) for v in args.background.split(",")) + (255,)
        flat = Image.new("RGBA", result.size, colour)
        flat.alpha_composite(result)
        result = flat.convert("RGB")
    result.save(args.output, optimize=True)


if __name__ == "__main__":
    main()
