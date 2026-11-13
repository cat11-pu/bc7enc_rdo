#!/usr/bin/env python3
"""Deterministic test texture corpus generator for bc7enc regression testing.

Generates 500 PNGs across 6 categories: solid, gradient, hardedge, noise,
transparent, normalmap. Pure stdlib (zlib/struct) - no external deps.
"""
import math
import os
import random
import struct
import zlib

CATEGORIES = ["solid", "gradient", "hardedge", "noise", "transparent", "normalmap"]
COUNT_PER_CATEGORY = {"solid": 84, "gradient": 84, "hardedge": 83,
                      "noise": 83, "transparent": 83, "normalmap": 83}
SIZES = [(64, 64), (128, 128), (96, 96), (128, 64), (64, 128),
         (80, 80), (112, 96), (256, 128)]


def write_png(path, width, height, pixels, has_alpha):
    color_type = 6 if has_alpha else 2
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        row = pixels[y]
        for x in range(width):
            p = row[x]
            raw += bytes(p if has_alpha else p[:3])

    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(bytes(raw), 6))
           + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def clamp(v):
    return 0 if v < 0 else (255 if v > 255 else int(v))


def rand_color(rng, alpha=255):
    return (rng.randrange(256), rng.randrange(256), rng.randrange(256), alpha)


def make_image(w, h, fill=(0, 0, 0, 255)):
    return [[fill for _ in range(w)] for _ in range(h)]


def gen_solid(rng, w, h):
    img = make_image(w, h, rand_color(rng))
    variant = rng.randrange(4)
    if variant >= 1:  # 2-4 flat bands
        bands = rng.randrange(2, 5)
        horizontal = rng.random() < 0.5
        colors = [rand_color(rng) for _ in range(bands)]
        for y in range(h):
            for x in range(w):
                i = (y if horizontal else x) * bands // (h if horizontal else w)
                img[y][x] = colors[i]
    if variant == 3:  # constant alpha
        a = rng.randrange(256)
        for y in range(h):
            for x in range(w):
                p = img[y][x]
                img[y][x] = (p[0], p[1], p[2], a)
    return img


def gen_gradient(rng, w, h):
    c0, c1 = rand_color(rng), rand_color(rng)
    c2 = rand_color(rng) if rng.random() < 0.4 else None
    kind = rng.randrange(3)
    cx, cy = rng.random() * w, rng.random() * h
    maxd = math.hypot(max(cx, w - cx), max(cy, h - cy)) or 1.0
    angle = rng.random() * math.pi
    dx, dy = math.cos(angle), math.sin(angle)
    img = make_image(w, h)
    for y in range(h):
        for x in range(w):
            if kind == 0:    # linear
                t = (x * dx + y * dy) / (w * abs(dx) + h * abs(dy) + 1e-9)
            elif kind == 1:  # radial
                t = math.hypot(x - cx, y - cy) / maxd
            else:            # diagonal sweep
                t = (x + y) / (w + h - 2)
            t = min(1.0, max(0.0, t))
            if c2 is not None:
                if t < 0.5:
                    a, b, u = c0, c1, t * 2.0
                else:
                    a, b, u = c1, c2, t * 2.0 - 1.0
            else:
                a, b, u = c0, c1, t
            img[y][x] = tuple(clamp(a[i] + (b[i] - a[i]) * u) for i in range(3)) + (255,)
    return img


def gen_hardedge(rng, w, h):
    colors = [rand_color(rng) for _ in range(rng.randrange(2, 5))]
    kind = rng.randrange(4)
    img = make_image(w, h, colors[0])
    if kind == 0:      # checkerboard
        cell = rng.choice([4, 8, 16])
        for y in range(h):
            for x in range(w):
                img[y][x] = colors[((x // cell) + (y // cell)) % len(colors)]
    elif kind == 1:    # random rectangles
        for _ in range(rng.randrange(3, 9)):
            x0, y0 = rng.randrange(w), rng.randrange(h)
            x1, y1 = rng.randrange(x0, w + 1), rng.randrange(y0, h + 1)
            c = rng.choice(colors)
            for y in range(y0, y1):
                for x in range(x0, x1):
                    img[y][x] = c
    elif kind == 2:    # stripes
        sw = rng.choice([2, 4, 8])
        horizontal = rng.random() < 0.5
        for y in range(h):
            for x in range(w):
                i = (y if horizontal else x) // sw
                img[y][x] = colors[i % len(colors)]
    else:              # circles
        for _ in range(rng.randrange(2, 6)):
            cx, cy = rng.randrange(w), rng.randrange(h)
            r = rng.randrange(4, max(5, min(w, h) // 2))
            c = rng.choice(colors)
            for y in range(max(0, cy - r), min(h, cy + r)):
                for x in range(max(0, cx - r), min(w, cx + r)):
                    if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                        img[y][x] = c
    return img


def gen_noise(rng, w, h):
    kind = rng.randrange(3)
    img = make_image(w, h)
    if kind == 0:      # per-pixel white noise
        for y in range(h):
            for x in range(w):
                img[y][x] = (rng.randrange(256), rng.randrange(256), rng.randrange(256), 255)
    elif kind == 1:    # blocky noise
        cell = rng.choice([2, 4, 8])
        grid = [[rand_color(rng) for _ in range((w // cell) + 1)] for _ in range((h // cell) + 1)]
        for y in range(h):
            for x in range(w):
                img[y][x] = grid[y // cell][x // cell]
    else:              # sparse speckle on flat background
        bg = rand_color(rng)
        for y in range(h):
            for x in range(w):
                img[y][x] = bg if rng.random() < 0.9 else rand_color(rng)
    return img


def gen_transparent(rng, w, h):
    base = gen_gradient(rng, w, h) if rng.random() < 0.5 else gen_solid(rng, w, h)
    kind = rng.randrange(3)
    img = make_image(w, h)
    cx, cy = rng.random() * w, rng.random() * h
    r = rng.randrange(max(4, min(w, h) // 4), max(5, min(w, h) // 2))
    for y in range(h):
        for x in range(w):
            p = base[y][x]
            if kind == 0:    # alpha gradient
                a = clamp(255 * (x + y) / (w + h - 2))
            elif kind == 1:  # hard cutout
                a = 0 if (x - cx) ** 2 + (y - cy) ** 2 <= r * r else 255
            else:            # random alpha noise
                a = rng.randrange(256)
            img[y][x] = (p[0], p[1], p[2], a)
    return img


def gen_normalmap(rng, w, h):
    kind = rng.randrange(3)
    bumps = [(rng.random() * w, rng.random() * h, rng.randrange(6, max(7, min(w, h) // 2)),
              rng.uniform(0.5, 2.0)) for _ in range(rng.randrange(2, 7))]
    freq = rng.uniform(0.1, 0.5)
    img = make_image(w, h)
    for y in range(h):
        for x in range(w):
            if kind == 0:    # smooth bumps
                hx = sum(a * -(x - bx) / (br * br) * math.exp(-((x - bx) ** 2 + (y - by) ** 2) / (2.0 * br * br))
                         for bx, by, br, a in bumps)
                hy = sum(a * -(y - by) / (br * br) * math.exp(-((x - bx) ** 2 + (y - by) ** 2) / (2.0 * br * br))
                         for bx, by, br, a in bumps)
                nx, ny, nz = -hx, -hy, 1.0
            elif kind == 1:  # ripples
                nx = 0.6 * math.sin(x * freq) + 0.2 * math.sin(y * freq * 0.7)
                ny = 0.6 * math.cos(y * freq) + 0.2 * math.cos(x * freq * 0.7)
                nz = 1.0
            else:            # noisy normals
                nx, ny, nz = rng.gauss(0, 0.4), rng.gauss(0, 0.4), 1.0
            l = math.sqrt(nx * nx + ny * ny + nz * nz)
            img[y][x] = (clamp(127.5 + 127.5 * nx / l),
                         clamp(127.5 + 127.5 * ny / l),
                         clamp(127.5 + 127.5 * nz / l), 255)
    return img


GENERATORS = {"solid": gen_solid, "gradient": gen_gradient, "hardedge": gen_hardedge,
              "noise": gen_noise, "transparent": gen_transparent, "normalmap": gen_normalmap}


def main(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    total = 0
    for cat in CATEGORIES:
        for i in range(COUNT_PER_CATEGORY[cat]):
            seed = (zlib.crc32(cat.encode()) & 0xFFFF) * 1000 + i
            rng = random.Random(seed)
            w, h = SIZES[(i + CATEGORIES.index(cat)) % len(SIZES)]
            img = GENERATORS[cat](rng, w, h)
            has_alpha = any(p[3] != 255 for row in img for p in row)
            name = "%s_%03d.png" % (cat, i)
            write_png(os.path.join(out_dir, name), w, h, img, has_alpha)
            total += 1
    print("generated %d textures in %s" % (total, out_dir))


if __name__ == "__main__":
    import sys
    main(sys.argv[1] if len(sys.argv) > 1 else "corpus")
