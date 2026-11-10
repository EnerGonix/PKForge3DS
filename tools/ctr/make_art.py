#!/usr/bin/env python3
"""HOME menu art for PKForge: its logo (tools/ctr/icon-flat.png) raised into a tile with depth for the
48x48 icon and the 256x128 banner, and the banner sound: tools/ctr/banner-sound.wav when present,
else an original sound of a ball opening (no game audio). Usage: make_art.py <project dir>
Writes icon.png (used for the SMDH) and build/cia/banner.png + banner.wav (used by cia.sh)."""
import shutil
import math
import os
import struct
import sys
import wave
import zlib

# ---------------------------------------------------------------- PNG out


def write_png(path, w, h, pixels):
    raw = b''.join(b'\x00' + bytes(v for px in row for v in px) for row in pixels)

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF)

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


# ---------------------------------------------------------------- the logo, given depth


def read_png(path):
    data = open(path, 'rb').read()
    pos, idat = 8, b''
    while pos < len(data):
        n, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b'IHDR':
            w, h, depth, color = struct.unpack('>IIBB', body[:10])
            assert depth == 8 and color in (2, 6), 'the logo must be 8-bit RGB or RGBA'
        elif kind == b'IDAT':
            idat += body
    raw = zlib.decompress(idat)
    bpp = 4 if color == 6 else 3
    stride, rows, prev, i = w * bpp, [], bytearray(w * bpp), 0
    for _ in range(h):
        kind, line = raw[i], bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if kind == 1:
                line[x] = (line[x] + a) & 255
            elif kind == 2:
                line[x] = (line[x] + b) & 255
            elif kind == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append([tuple(v / 255 for v in line[x * bpp:x * bpp + bpp]) + ((1.0,) if bpp == 3 else ()) for x in range(w)])
        prev = line
    return w, h, rows


def sample(img, u, v):
    """Bilinear sample of the logo at (u, v) in [0, 1); transparent outside."""
    w, h, rows = img
    x, y = u * w - 0.5, v * h - 0.5
    x0, y0 = math.floor(x), math.floor(y)
    fx, fy = x - x0, y - y0
    acc = [0.0] * 4
    for dy, wy in ((0, 1 - fy), (1, fy)):
        for dx, wx in ((0, 1 - fx), (1, fx)):
            xx, yy = x0 + dx, y0 + dy
            px = rows[yy][xx] if 0 <= xx < w and 0 <= yy < h else (0, 0, 0, 0)
            k = wx * wy
            acc[3] += px[3] * k
            for c in range(3):
                acc[c] += px[c] * px[3] * k
    a = acc[3]
    return (acc[0] / a, acc[1] / a, acc[2] / a, a) if a > 1e-6 else (0, 0, 0, 0)


def render_logo(img, w, h, left, top, size, depth, background, ss=3):
    """The logo as a raised tile: its face on top, an extruded side going down-right, a soft shadow."""
    def alpha(x, y):
        return sample(img, (x - left) / size, (y - top) / size)[3]

    rows = []
    for py in range(h):
        row = []
        for px in range(w):
            acc = [0.0, 0.0, 0.0]
            for sy in range(ss):
                for sx in range(ss):
                    x, y = px + (sx + 0.5) / ss, py + (sy + 0.5) / ss
                    c = background(x, y)
                    # Shadow: the logo's outline pushed further down-right, blurred by a few taps.
                    sh = sum(alpha(x - depth * 1.8 + ox, y - depth * 2.2 + oy) for ox in (-2, 0, 2) for oy in (-2, 0, 2)) / 9
                    c = tuple(v * (1 - 0.55 * sh) for v in c)
                    # The side: the face's colours darkened, nearest layer first.
                    for layer in range(int(depth * 2), 0, -1):
                        d = layer / 2
                        s = sample(img, (x - d - left) / size, (y - d - top) / size)
                        if s[3] > 0.5:
                            dark = 0.35 + 0.25 * (1 - d / depth)
                            c = tuple(v * dark for v in s[:3])
                            break
                    f = sample(img, (x - left) / size, (y - top) / size)
                    if f[3] > 0:
                        # Bevel: lighter where the face's edge looks up-left, darker down-right.
                        gx = alpha(x + 1, y) - alpha(x - 1, y)
                        gy = alpha(x, y + 1) - alpha(x, y - 1)
                        light = 1 + 0.45 * (gx + gy) * -1 * 0.5 + 0.10
                        face = tuple(min(1, v * light) for v in f[:3])
                        c = tuple(fv * f[3] + cv * (1 - f[3]) for fv, cv in zip(face, c))
                    for i in range(3):
                        acc[i] += c[i]
            n = ss * ss
            row.append(tuple(int(max(0, min(1, v / n)) * 255 + 0.5) for v in acc) + (255,))
        rows.append(row)
    return rows


def navy_glow(w, h, gx, gy, spread):
    """PKForge's navy with a cyan glow behind the ball and a faint grid."""
    def bg(x, y):
        d = math.hypot((x - gx) / spread, (y - gy) / spread)
        glow = max(0.0, 1 - d) ** 2
        base = (0.05 + 0.02 * y / h, 0.09 + 0.03 * y / h, 0.20 + 0.06 * y / h)
        grid = 0.04 if (int(x) % 16 == 0 or int(y) % 16 == 0) else 0.0
        return (base[0] + glow * 0.05 + grid * 0.4, base[1] + glow * 0.45 + grid, base[2] + glow * 0.75 + grid * 1.6)
    return bg


# ---------------------------------------------------------------- the jingle (original)
# A ball opening (original, not the games' sound): a button click, the latch popping, air rushing out and
# a burst of sparkling light settling on a soft chord, with a light echo. Stereo, 44.1 kHz, 16-bit, under 3 seconds: what the HOME Menu plays for a banner.

RATE = 44100
LENGTH = 2.2


def hz(semis):
    return 440.0 * 2 ** (semis / 12)  # semitones from A4


def strike(left, right, at, freq, volume, decay, partials, pan=0.0):
    """One struck note: decaying sine partials (ratio, level); pan -1 left .. 1 right."""
    start = int(at * RATE)
    n = min(len(left) - start, int(decay * 6 * RATE))
    gl, gr = volume * (1 - pan) / 2 ** 0.5, volume * (1 + pan) / 2 ** 0.5
    for i in range(n):
        t = i / RATE
        env = min(1.0, i / 90) * math.exp(-t / decay)
        v = 0.0
        for ratio, level in partials:
            # Higher partials die away faster, like a real bell.
            v += level * math.sin(2 * math.pi * freq * ratio * t) * math.exp(-t * ratio / (decay * 6))
        left[start + i] += v * env * gl
        right[start + i] += v * env * gr


BELL = [(1, 1.0), (2.0, 0.35), (3.0, 0.12), (4.2, 0.05)]


class Noise:
    """Repeatable white noise (a fixed seed: the same sound on every build)."""

    def __init__(self, seed):
        self.seed = seed

    def __call__(self):
        self.seed = (self.seed * 1103515245 + 12345) & 0x7FFFFFFF
        return (self.seed >> 15) / 32768.0 - 1


def add(left, right, at, samples, pan=0.0):
    start = int(at * RATE)
    gl, gr = (1 - pan) / 2 ** 0.5, (1 + pan) / 2 ** 0.5
    for i, v in enumerate(samples):
        if start + i < len(left):
            left[start + i] += v * gl
            right[start + i] += v * gr


def click(volume, bright, length=0.018, seed=7):
    """A short mechanical tick: a noise burst through a fast decay, with a small pitched knock."""
    noise, out, lp = Noise(seed), [], 0.0
    for i in range(int(length * RATE)):
        t = i / RATE
        lp += (noise() - lp) * bright
        knock = math.sin(2 * math.pi * 1900 * t) * 0.6
        out.append((lp * 1.6 + knock) * volume * math.exp(-t / (length / 4)))
    return out


def pop(volume):
    """The latch giving way: a quick low thump falling in pitch."""
    out, phase = [], 0.0
    for i in range(int(0.12 * RATE)):
        t = i / RATE
        f = 70 + 260 * math.exp(-t / 0.018)
        phase += f / RATE
        out.append(math.sin(2 * math.pi * phase) * volume * math.exp(-t / 0.04) * min(1.0, i / 30))
    return out


def whoosh(volume, length=0.55):
    """Air rushing out: noise through a low-pass opening up, swelling and fading, with a rising tone."""
    noise, out, lp, phase = Noise(99), [], 0.0, 0.0
    n = int(length * RATE)
    for i in range(n):
        x = i / n
        cutoff = 0.02 + 0.35 * x ** 1.5
        lp += (noise() - lp) * cutoff
        phase += (500 + 1500 * x ** 2) / RATE
        env = math.sin(math.pi * min(1.0, x * 1.15)) ** 1.5
        out.append((lp * 1.4 + 0.18 * math.sin(2 * math.pi * phase)) * volume * env)
    return out


def jingle():
    n = int(LENGTH * RATE)
    left, right = [0.0] * n, [0.0] * n
    # The button pressed, then the latch: two clicks and a pop.
    add(left, right, 0.00, click(0.55, 0.55, seed=11), -0.1)
    add(left, right, 0.07, click(0.45, 0.35, seed=23), 0.1)
    add(left, right, 0.10, pop(0.7))
    # The ball opening: air rushing out, left to right.
    add(left, right, 0.11, whoosh(0.5), -0.3)
    add(left, right, 0.16, whoosh(0.35), 0.3)
    # The light coming out: a rising burst of sparkles all around, then a soft glowing chord.
    sparkle = Noise(4242)
    for k in range(18):
        at = 0.32 + k * 0.045 + (sparkle() + 1) * 0.012
        semis = 22 + int((sparkle() + 1) * 9) + k // 3
        strike(left, right, at, hz(semis), 0.09, 0.22, [(1, 1.0), (2.0, 0.2)], sparkle() * 0.8)
    for semis, pan in ((10, -0.4), (14, 0.0), (17, 0.4), (22, 0.0)):
        strike(left, right, 0.40, hz(semis), 0.11, 0.8, BELL, pan)
    # A light echo, crossing sides.
    delay = int(0.15 * RATE)
    for i in range(delay, n):
        left[i] += right[i - delay] * 0.25
        right[i] += left[i - delay] * 0.25
    fade = int(0.5 * RATE)
    for i in range(n - fade, n):
        k = (n - i) / fade
        left[i] *= k
        right[i] *= k
    peak = max(max(abs(x) for x in left), max(abs(x) for x in right))
    g = 0.8 / peak
    return [x * g for x in left], [x * g for x in right]


def write_wav(path, channels):
    left, right = channels
    with wave.open(path, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b''.join(struct.pack('<hh', int(max(-1, min(1, a)) * 32767), int(max(-1, min(1, b)) * 32767))
                               for a, b in zip(left, right)))


def main():
    root = sys.argv[1]
    out = os.path.join(root, 'build', 'cia')
    os.makedirs(out, exist_ok=True)
    logo = read_png(os.path.join(root, 'tools', 'ctr', 'icon-flat.png'))
    icon = render_logo(logo, 48, 48, 2, 1, 42, 2.5, navy_glow(48, 48, 20, 18, 34), ss=4)
    write_png(os.path.join(root, 'icon.png'), 48, 48, icon)
    banner = render_logo(logo, 256, 128, 78, 8, 100, 7, navy_glow(256, 128, 120, 52, 150), ss=2)
    write_png(os.path.join(out, 'banner.png'), 256, 128, banner)
    sound = os.path.join(root, 'tools', 'ctr', 'banner-sound.wav')
    if os.path.exists(sound):
        shutil.copyfile(sound, os.path.join(out, 'banner.wav'))
    else:
        write_wav(os.path.join(out, 'banner.wav'), jingle())


if __name__ == '__main__':
    main()
