"""The HOME Menu 3D banner: PKForge's logo (tools/ctr/title-logo.png) as a solid object with relief.

The logo's dark body becomes a thick slab and its cyan lines (the handheld's edge, screen and button) stand
out of it as ridges, on the front and the back, so it has real depth from every angle and in stereoscopic
3D. Every surface takes its colour from the logo (one texture, UVs from the logo's pixels). No
transparency, no animation: the HOME Menu itself turns the model. Writes a glTF scene
COMMON > world > logo for pycgfx (https://github.com/skyfloogle/pycgfx), which makes the CGFX:

    python make_banner3d.py <out dir>
    python <pycgfx>/main.py <out dir>/banner3d.gltf tools/ctr/banner.cgfx

Units follow pycgfx's banner-camera.gltf: the camera sits at (0, 1, 44.8) looking down -Z with a 30 degree
vertical field of view, so about 24 units tall are visible around y = 1.
Needs Pillow."""
import json
import os
import struct
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)

logo = Image.open(os.path.join(HERE, 'title-logo.png')).convert('RGBA')
W, H = logo.size                  # 200 x 238
CELL = 3                          # cell size, in logo pixels
HEIGHT = 16.0                     # model height in banner units
BODY = 3.2                        # the slab's thickness
RIDGE = 1.1                       # how far the cyan lines stand out of each face
CENTER_Y = 1.0                    # the camera's height

scale = HEIGHT / H
cols, rows = (W + CELL - 1) // CELL, (H + CELL - 1) // CELL
px = logo.load()


def classify(cx, cy):
    """0 = empty, 1 = body, 2 = ridge (cyan line)."""
    n = solid = cyan = 0
    for y in range(cy * CELL, min(H, cy * CELL + CELL)):
        for x in range(cx * CELL, min(W, cx * CELL + CELL)):
            r, g, b, a = px[x, y]
            n += 1
            if a >= 128:
                solid += 1
                if b >= 150 and g >= 90:
                    cyan += 1
    if solid * 2 < n:
        return 0
    return 2 if cyan * 5 >= solid * 2 else 1


kind = [[classify(cx, cy) for cx in range(cols)] for cy in range(rows)]


def half(cx, cy):
    """Half the thickness at a cell (0 outside the logo)."""
    if cx < 0 or cy < 0 or cx >= cols or cy >= rows or kind[cy][cx] == 0:
        return 0.0
    return BODY / 2 + (RIDGE if kind[cy][cx] == 2 else 0)


def pos(x, y, z):
    """Logo pixel coordinates (y down) to banner units (y up), centred."""
    return ((x - W / 2) * scale, (H / 2 - y) * scale + CENTER_Y, z)


mesh = {'p': [], 'n': [], 'uv': [], 'i': []}


def quad(corners, normal, uvs):
    base = len(mesh['p'])
    mesh['p'] += corners
    mesh['n'] += [normal] * 4
    mesh['uv'] += uvs
    mesh['i'] += [base, base + 1, base + 2, base, base + 2, base + 3]


# Front and back faces: runs of cells with the same height in each row.
for cy in range(rows):
    cx = 0
    while cx < cols:
        h = half(cx, cy)
        if h == 0:
            cx += 1
            continue
        start = cx
        while cx < cols and half(cx, cy) == h:
            cx += 1
        x0, x1 = start * CELL, min(W, cx * CELL)
        y0, y1 = cy * CELL, min(H, cy * CELL + CELL)
        u0, u1, v0, v1 = x0 / W, x1 / W, y0 / H, y1 / H
        quad([pos(x0, y1, h), pos(x1, y1, h), pos(x1, y0, h), pos(x0, y0, h)], (0, 0, 1),
             [(u0, v1), (u1, v1), (u1, v0), (u0, v0)])
        # The back reads right from behind (mirrored UVs).
        quad([pos(x1, y1, -h), pos(x0, y1, -h), pos(x0, y0, -h), pos(x1, y0, -h)], (0, 0, -1),
             [(1 - u1, v1), (1 - u0, v1), (1 - u0, v0), (1 - u1, v0)])


def walls(ha, hb):
    """The z ranges of wall between a cell of half-thickness ha and its neighbour hb (< ha)."""
    if hb == 0:
        return [(-ha, ha)]
    return [(hb, ha), (-ha, -hb)]


# Walls across rows (top/bottom edges): runs along x with the same pair of heights.
for cy in range(rows):
    for dy, normal in ((-1, (0, 1, 0)), (1, (0, -1, 0))):
        cx = 0
        while cx < cols:
            ha, hb = half(cx, cy), half(cx, cy + dy)
            if ha <= hb:
                cx += 1
                continue
            start = cx
            while cx < cols and (half(cx, cy), half(cx, cy + dy)) == (ha, hb):
                cx += 1
            x0, x1 = start * CELL, min(W, cx * CELL)
            y = cy * CELL if dy < 0 else min(H, cy * CELL + CELL)
            v = (cy * CELL + CELL / 2) / H
            for z0, z1 in walls(ha, hb):
                a, b = (x0, x1) if dy < 0 else (x1, x0)
                quad([pos(a, y, z1), pos(b, y, z1), pos(b, y, z0), pos(a, y, z0)], normal,
                     [(a / W, v), (b / W, v), (b / W, v), (a / W, v)])

# Walls across columns (left/right edges): runs along y.
for cx in range(cols):
    for dx, normal in ((-1, (-1, 0, 0)), (1, (1, 0, 0))):
        cy = 0
        while cy < rows:
            ha, hb = half(cx, cy), half(cx + dx, cy)
            if ha <= hb:
                cy += 1
                continue
            start = cy
            while cy < rows and (half(cx, cy), half(cx + dx, cy)) == (ha, hb):
                cy += 1
            y0, y1 = start * CELL, min(H, cy * CELL)
            x = cx * CELL if dx < 0 else min(W, cx * CELL + CELL)
            u = (cx * CELL + CELL / 2) / W
            for z0, z1 in walls(ha, hb):
                a, b = (y1, y0) if dx < 0 else (y0, y1)  # counter-clockwise seen from outside
                quad([pos(x, a, z1), pos(x, b, z1), pos(x, b, z0), pos(x, a, z0)], normal,
                     [(u, a / H), (u, b / H), (u, b / H), (u, a / H)])

# The texture: the logo on its own dark colour (only filled cells are drawn, so no alpha), 128 x 128.
flat = Image.new('RGBA', logo.size, (0x14, 0x12, 0x1D, 255))
flat.alpha_composite(logo)
flat.convert('RGB').resize((128, 128), Image.LANCZOS).save(os.path.join(OUT, 'logo.png'))

# ---- glTF
blob = bytearray()
views, accessors = [], []


def add(data, fmt, count, kind_, target, minmax=None):
    while len(blob) % 4:
        blob.append(0)
    offset = len(blob)
    blob.extend(data)
    views.append({'buffer': 0, 'byteOffset': offset, 'byteLength': len(data), 'target': target})
    acc = {'bufferView': len(views) - 1, 'componentType': fmt, 'count': count, 'type': kind_}
    if minmax:
        acc['min'], acc['max'] = minmax
    accessors.append(acc)
    return len(accessors) - 1


p = mesh['p']
assert len(p) < 65536
mins = [min(v[i] for v in p) for i in range(3)]
maxs = [max(v[i] for v in p) for i in range(3)]
prim = {
    'attributes': {
        'POSITION': add(b''.join(struct.pack('<3f', *v) for v in p), 5126, len(p), 'VEC3', 34962, (mins, maxs)),
        'NORMAL': add(b''.join(struct.pack('<3f', *v) for v in mesh['n']), 5126, len(p), 'VEC3', 34962),
        'TEXCOORD_0': add(b''.join(struct.pack('<2f', *v) for v in mesh['uv']), 5126, len(p), 'VEC2', 34962),
    },
    'indices': add(b''.join(struct.pack('<H', i) for i in mesh['i']), 5123, len(mesh['i']), 'SCALAR', 34963),
    'material': 0,
}
gltf = {
    'asset': {'version': '2.0', 'generator': 'PKForge3DS tools/ctr/make_banner3d.py'},
    'scene': 0,
    'scenes': [{'nodes': [0]}],
    'nodes': [
        {'name': 'COMMON', 'children': [1]},
        {'name': 'world', 'children': [2]},
        {'name': 'logo', 'mesh': 0},
    ],
    'meshes': [{'name': 'logo', 'primitives': [prim]}],
    'materials': [
        {'name': 'logo', 'pbrMetallicRoughness': {'baseColorTexture': {'index': 0}, 'metallicFactor': 0, 'roughnessFactor': 1}},
    ],
    'textures': [{'source': 0, 'sampler': 0}],
    'samplers': [{'magFilter': 9729, 'minFilter': 9729, 'wrapS': 33071, 'wrapT': 33071}],
    'images': [{'uri': 'logo.png'}],
    'buffers': [{'uri': 'banner3d.bin', 'byteLength': len(blob)}],
    'bufferViews': views,
    'accessors': accessors,
}
open(os.path.join(OUT, 'banner3d.bin'), 'wb').write(blob)
json.dump(gltf, open(os.path.join(OUT, 'banner3d.gltf'), 'w'), indent=1)
ridges = sum(row.count(2) for row in kind)
print(f'{cols}x{rows} cells ({ridges} ridge), {len(mesh["i"]) // 3} triangles, {len(p)} vertices, {len(blob)} bytes')
