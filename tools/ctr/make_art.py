#!/usr/bin/env python3
"""HOME menu art for PKForge: its logo (tools/ctr/icon-flat.png) raised into a tile with depth for the
48x48 icon and the 256x128 banner, and the banner sound: tools/ctr/banner-sound.wav when present,
else an original chiptune jingle (no game music). Usage: make_art.py <project dir>
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

RATE = 32000


def note(freq, length, volume=0.22, duty=0.5, slide=0.0):
    out = []
    n = int(RATE * length)
    phase = 0.0
    for i in range(n):
        f = freq * (1 + slide * i / n)
        phase += f / RATE
        wave_ = 1.0 if (phase % 1.0) < duty else -1.0
        env = min(1.0, i / 120) * (1 - i / n) ** 0.6
        out.append(wave_ * volume * env)
    return out


def rest(length):
    return [0.0] * int(RATE * length)


def click(volume=0.35):
    # The ball wobbling: a short noisy tick with a falling pitch.
    out, seed = [], 12345
    for i in range(int(RATE * 0.045)):
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
        noise = (seed >> 16) / 32768.0 - 1
        tone = math.sin(2 * math.pi * (900 - i * 6) * i / RATE)
        out.append((noise * 0.5 + tone * 0.5) * volume * (1 - i / (RATE * 0.045)) ** 2)
    return out


def mix(*tracks):
    n = max(len(t) for t in tracks)
    return [sum(t[i] for t in tracks if i < len(t)) for i in range(n)]


def jingle():
    hz = lambda semis: 440.0 * 2 ** (semis / 12)  # semitones from A4
    lead = []
    # An upbeat rising call (G major arpeggio, then a turn).
    for semis, length in [(-2, 0.09), (2, 0.09), (5, 0.09), (10, 0.18), (9, 0.09), (10, 0.27)]:
        lead += note(hz(semis), length, duty=0.25)
    lead += rest(0.12)
    # Three wobbles of the ball...
    for _ in range(3):
        lead += click() + rest(0.17)
    # ...and caught: a bright little fanfare with a sparkle on top.
    fanfare = []
    for semis, length in [(5, 0.08), (9, 0.08), (12, 0.08), (17, 0.34)]:
        fanfare += note(hz(semis), length, volume=0.2, duty=0.125)
    sparkle = rest(0.24) + note(hz(29), 0.05, 0.08, 0.5) + note(hz(33), 0.05, 0.07, 0.5) + note(hz(36), 0.09, 0.06, 0.5)
    bass = note(hz(-14), 0.24, 0.16, 0.5) + note(hz(-7), 0.34, 0.16, 0.5)
    lead += mix(fanfare, sparkle, bass)
    return lead


def write_wav(path, samples):
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b''.join(struct.pack('<h', int(max(-1, min(1, s)) * 30000)) for s in samples))


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
