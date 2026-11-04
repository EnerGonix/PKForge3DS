#!/usr/bin/env python3
"""Converts the Android PKForge's Poképark map for the 3DS. Usage: park.py <PKForge source tree> <project>

From Resources/Pokepark/park (ground.png, objects.png: 1008x1376, AlchemyBats' tiles) it writes
romfs/gfx/park<N>.t3x, the map cut into 256x256 RGB565 tiles (the 3DS GPU stops at 1024), and
romfs/gfx/parkmini.t3x, one pixel per 8x8 map cell for the bottom screen. From ParkMap.World.cs it writes
romfs/park/grid.txt, the walkability grid ('.' land, '~' water, '^' lava, '=' air, '#' blocked)."""
import os
import re
import struct
import subprocess
import sys
import zlib

TILE = 256


def read_png(path):
    data = open(path, 'rb').read()
    pos, idat = 8, b''
    while pos < len(data):
        n, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b'IHDR':
            w, h, depth, color = struct.unpack('>IIBB', body[:10])
            assert depth == 8 and color in (2, 6), path
        elif kind == b'IDAT':
            idat += body
    raw = zlib.decompress(idat)
    bpp = 4 if color == 6 else 3
    stride = w * bpp
    out = bytearray(w * h * 4)
    prev = bytearray(stride)
    i = 0
    for y in range(h):
        kind, line = raw[i], bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        if kind == 1:
            for x in range(bpp, stride):
                line[x] = (line[x] + line[x - bpp]) & 255
        elif kind == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 255
        elif kind == 3:
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                line[x] = (line[x] + (a + prev[x]) // 2) & 255
        elif kind == 4:
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                b = prev[x]
                c = prev[x - bpp] if x >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        for x in range(w):
            o = (y * w + x) * 4
            out[o:o + 3] = line[x * bpp:x * bpp + 3]
            out[o + 3] = line[x * bpp + 3] if bpp == 4 else 255
        prev = line
    return w, h, out


def write_png(path, w, h, rgb):
    raw = b''.join(b'\x00' + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF)

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def main():
    src, root = sys.argv[1], sys.argv[2]
    art = os.path.join(src, 'src', 'PKForge.App', 'Resources', 'Pokepark', 'park')
    w, h, ground = read_png(os.path.join(art, 'ground.png'))
    w2, h2, objects = read_png(os.path.join(art, 'objects.png'))
    assert (w, h) == (w2, h2)
    rgb = bytearray(w * h * 3)
    for i in range(w * h):
        a = objects[i * 4 + 3]
        for c in range(3):
            rgb[i * 3 + c] = (objects[i * 4 + c] * a + ground[i * 4 + c] * (255 - a)) // 255

    work = os.path.join(root, 'build', 'park')
    gfx = os.path.join(root, 'romfs', 'gfx')
    os.makedirs(work, exist_ok=True)
    os.makedirs(gfx, exist_ok=True)
    cols, rows = (w + TILE - 1) // TILE, (h + TILE - 1) // TILE
    jobs = []
    for ty in range(rows):
        for tx in range(cols):
            tile = bytearray(TILE * TILE * 3)
            for y in range(TILE):
                sy = ty * TILE + y
                if sy >= h:
                    break
                x0 = tx * TILE
                n = min(TILE, w - x0)
                tile[y * TILE * 3:(y * TILE + n) * 3] = rgb[(sy * w + x0) * 3:(sy * w + x0 + n) * 3]
            index = ty * cols + tx
            png = os.path.join(work, f'park{index}.png')
            write_png(png, TILE, TILE, tile)
            jobs.append([png, os.path.join(gfx, f'park{index}.t3x')])
    # The minimap: one pixel per 8x8 cell, averaged.
    mw, mh = w // 8, h // 8
    mini = bytearray(mw * mh * 3)
    for y in range(mh):
        for x in range(mw):
            for c in range(3):
                s = 0
                for dy in range(8):
                    row = ((y * 8 + dy) * w + x * 8) * 3 + c
                    s += sum(rgb[row + dx * 3] for dx in range(8))
                mini[(y * mw + x) * 3 + c] = s // 64
    png = os.path.join(work, 'parkmini.png')
    write_png(png, mw, mh, mini)
    jobs.append([png, os.path.join(gfx, 'parkmini.t3x')])
    for png, t3x in jobs:
        subprocess.run(['tex3ds', png, '-f', 'rgb565', '-z', 'auto', '-o', t3x], check=True)

    world = open(os.path.join(src, 'src', 'PKForge.Domain', 'ParkMap.World.cs'), encoding='utf-8').read()
    grid = [s for s in re.findall(r'"([.~^=#]+)"', world) if len(s) == w // 8]
    assert len(grid) == h // 8, (len(grid), h // 8)
    os.makedirs(os.path.join(root, 'romfs', 'park'), exist_ok=True)
    with open(os.path.join(root, 'romfs', 'park', 'grid.txt'), 'w', newline='\n') as f:
        f.write(f'{w} {h} {cols} {rows}\n' + '\n'.join(grid) + '\n')
    print(f'{cols * rows} tiles, minimap {mw}x{mh}, grid {len(grid[0])}x{len(grid)}')


if __name__ == '__main__':
    main()
