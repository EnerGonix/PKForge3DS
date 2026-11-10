"""The HOME Menu 3D banner: PKForge's logo (tools/ctr/title-logo.png) made into a thick, solid object.

The front and back follow the logo's outline (cells of its alpha mask, merged in row runs) and carry the
logo as a texture; side walls in PKForge blue give it its thickness. No transparency, no animation: the
HOME Menu itself turns the model and shows it in stereoscopic 3D. Writes a glTF scene
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
CELL = 3                          # mask cell, in logo pixels
HEIGHT = 15.0                     # model height in banner units
DEPTH = 1.8                       # thickness
CENTER_Y = 1.0                    # the camera's height
SIDE = (0x27 / 255, 0x89 / 255, 0xCD / 255, 1.0)   # PKForge's blue (theme "Blue")

scale = HEIGHT / H
cols, rows = (W + CELL - 1) // CELL, (H + CELL - 1) // CELL
alpha = logo.getchannel('A').load()


def filled(cx, cy):
    if cx < 0 or cy < 0 or cx >= cols or cy >= rows:
        return False
    total = n = 0
    for y in range(cy * CELL, min(H, cy * CELL + CELL)):
        for x in range(cx * CELL, min(W, cx * CELL + CELL)):
            total += alpha[x, y]
            n += 1
    return total / n >= 128


grid = [[filled(cx, cy) for cx in range(cols)] for cy in range(rows)]


def pos(px, py, z):
    """Logo pixel coordinates (y down) to banner units (y up), centred."""
    return ((px - W / 2) * scale, (H / 2 - py) * scale + CENTER_Y, z)


front, walls = {'p': [], 'n': [], 'uv': [], 'i': []}, {'p': [], 'n': [], 'i': []}


def quad(mesh, corners, normal, uvs=None):
    base = len(mesh['p'])
    mesh['p'] += corners
    mesh['n'] += [normal] * 4
    if uvs is not None:
        mesh['uv'] += uvs
    mesh['i'] += [base, base + 1, base + 2, base, base + 2, base + 3]


zf, zb = DEPTH / 2, -DEPTH / 2
for cy in range(rows):
    cx = 0
    while cx < cols:
        if not grid[cy][cx]:
            cx += 1
            continue
        start = cx
        while cx < cols and grid[cy][cx]:
            cx += 1
        x0, x1 = start * CELL, min(W, cx * CELL)
        y0, y1 = cy * CELL, min(H, cy * CELL + CELL)
        u0, u1, v0, v1 = x0 / W, x1 / W, y0 / H, y1 / H
        # Front (+Z, counter-clockwise seen from the camera) and back (-Z, the logo reads right from behind).
        quad(front, [pos(x0, y1, zf), pos(x1, y1, zf), pos(x1, y0, zf), pos(x0, y0, zf)], (0, 0, 1),
             [(u0, v1), (u1, v1), (u1, v0), (u0, v0)])
        quad(front, [pos(x1, y1, zb), pos(x0, y1, zb), pos(x0, y0, zb), pos(x1, y0, zb)], (0, 0, -1),
             [(1 - u1, v1), (1 - u0, v1), (1 - u0, v0), (1 - u1, v0)])

# Side walls along every edge between a filled cell and an empty one, merged into runs.
for cy in range(rows):
    for side, dy in (('top', -1), ('bottom', 1)):
        cx = 0
        while cx < cols:
            if not (grid[cy][cx] and not (0 <= cy + dy < rows and grid[cy + dy][cx])):
                cx += 1
                continue
            start = cx
            while cx < cols and grid[cy][cx] and not (0 <= cy + dy < rows and grid[cy + dy][cx]):
                cx += 1
            x0, x1 = start * CELL, min(W, cx * CELL)
            y = cy * CELL if side == 'top' else min(H, cy * CELL + CELL)
            if side == 'top':
                quad(walls, [pos(x0, y, zf), pos(x1, y, zf), pos(x1, y, zb), pos(x0, y, zb)], (0, 1, 0))
            else:
                quad(walls, [pos(x1, y, zf), pos(x0, y, zf), pos(x0, y, zb), pos(x1, y, zb)], (0, -1, 0))
for cx in range(cols):
    for side, dx in (('left', -1), ('right', 1)):
        cy = 0
        while cy < rows:
            if not (grid[cy][cx] and not (0 <= cx + dx < cols and grid[cy][cx + dx])):
                cy += 1
                continue
            start = cy
            while cy < rows and grid[cy][cx] and not (0 <= cx + dx < cols and grid[cy][cx + dx]):
                cy += 1
            y0, y1 = start * CELL, min(H, cy * CELL)
            x = cx * CELL if side == 'left' else min(W, cx * CELL + CELL)
            if side == 'left':
                quad(walls, [pos(x, y0, zf), pos(x, y1, zf), pos(x, y1, zb), pos(x, y0, zb)], (-1, 0, 0))
            else:
                quad(walls, [pos(x, y1, zf), pos(x, y0, zf), pos(x, y0, zb), pos(x, y1, zb)], (1, 0, 0))

# The texture: the logo on its own dark colour (no alpha needed: only filled cells are drawn), 128 x 128.
flat = Image.new('RGBA', logo.size, (0x14, 0x12, 0x1D, 255))
flat.alpha_composite(logo)
flat.convert('RGB').resize((128, 128), Image.LANCZOS).save(os.path.join(OUT, 'logo.png'))

# ---- glTF
blob = bytearray()
views, accessors = [], []


def add(data, fmt, count, kind, target, minmax=None):
    while len(blob) % 4:
        blob.append(0)
    offset = len(blob)
    blob.extend(data)
    views.append({'buffer': 0, 'byteOffset': offset, 'byteLength': len(data), 'target': target})
    acc = {'bufferView': len(views) - 1, 'componentType': fmt, 'count': count, 'type': kind}
    if minmax:
        acc['min'], acc['max'] = minmax
    accessors.append(acc)
    return len(accessors) - 1


def primitive(mesh, material, with_uv):
    p = mesh['p']
    mins = [min(v[i] for v in p) for i in range(3)]
    maxs = [max(v[i] for v in p) for i in range(3)]
    attrs = {
        'POSITION': add(b''.join(struct.pack('<3f', *v) for v in p), 5126, len(p), 'VEC3', 34962, (mins, maxs)),
        'NORMAL': add(b''.join(struct.pack('<3f', *v) for v in mesh['n']), 5126, len(p), 'VEC3', 34962),
    }
    if with_uv:
        attrs['TEXCOORD_0'] = add(b''.join(struct.pack('<2f', *v) for v in mesh['uv']), 5126, len(p), 'VEC2', 34962)
    idx = add(b''.join(struct.pack('<H', i) for i in mesh['i']), 5123, len(mesh['i']), 'SCALAR', 34963)
    return {'attributes': attrs, 'indices': idx, 'material': material}


assert len(front['p']) < 65536 and len(walls['p']) < 65536
gltf = {
    'asset': {'version': '2.0', 'generator': 'PKForge3DS tools/ctr/make_banner3d.py'},
    'scene': 0,
    'scenes': [{'nodes': [0]}],
    'nodes': [
        {'name': 'COMMON', 'children': [1]},
        {'name': 'world', 'children': [2]},
        {'name': 'logo', 'mesh': 0},
    ],
    'meshes': [{'name': 'logo', 'primitives': [primitive(front, 0, True), primitive(walls, 1, False)]}],
    'materials': [
        {'name': 'face', 'pbrMetallicRoughness': {'baseColorTexture': {'index': 0}, 'metallicFactor': 0, 'roughnessFactor': 1}},
        {'name': 'side', 'pbrMetallicRoughness': {'baseColorFactor': list(SIDE), 'metallicFactor': 0, 'roughnessFactor': 1}},
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
print(f'{cols}x{rows} cells, {len(front["i"]) // 3} face + {len(walls["i"]) // 3} wall triangles, {len(blob)} bytes')
