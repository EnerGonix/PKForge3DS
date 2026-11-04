#!/usr/bin/env python3
"""Converts the app's autotest BMP shots to PNG: bmp2png.py <shots dir> <out dir>"""
import os, struct, sys, zlib

def convert(src, dst):
    data = open(src, 'rb').read()
    off, w, h = struct.unpack_from('<I', data, 10)[0], *struct.unpack_from('<ii', data, 18)
    row = (w * 3 + 3) & ~3
    lines = bytearray()
    for y in range(h):
        sy = h - 1 - y
        lines.append(0)
        p = off + sy * row
        for x in range(w):
            b, g, r = data[p + x * 3:p + x * 3 + 3]
            lines += bytes((r, g, b))
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF)
    with open(dst, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(bytes(lines), 6)) + chunk(b'IEND', b''))

for name in sorted(os.listdir(sys.argv[1])):
    if name.endswith('.bmp'):
        convert(os.path.join(sys.argv[1], name), os.path.join(sys.argv[2], name[:-4] + '.png'))
print('ok')
