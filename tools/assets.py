#!/usr/bin/env python3
"""Builds PKForge 3DS's RomFS and icon from the PKForge source tree and PKSM-Core.

Usage: assets.py <PKForge source root> <PKForge3DS root>

Standard library only: PNGs are decoded, cut and scaled here; tex3ds and mkbcfnt
(devkitPro tools, on PATH) write the 3DS texture and font formats.
"""
import concurrent.futures
import os
import shutil
import struct
import subprocess
import sys
import zlib


# ---------------------------------------------------------------- PNG in / out

def png_read(path):
    """Returns (width, height, RGBA bytearray). Handles every non-interlaced PNG kind."""
    with open(path, 'rb') as f:
        raw = f.read()
    if raw[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'{path}: not a PNG')
    pos, idat, palette, trns = 8, bytearray(), None, None
    width = height = depth = ctype = interlace = 0
    while pos < len(raw):
        length, kind = struct.unpack('>I4s', raw[pos:pos + 8])
        body = raw[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b'IHDR':
            width, height, depth, ctype, _, _, interlace = struct.unpack('>IIBBBBB', body)
        elif kind == b'PLTE':
            palette = body
        elif kind == b'tRNS':
            trns = body
        elif kind == b'IDAT':
            idat += body
        elif kind == b'IEND':
            break
    if interlace:
        raise ValueError(f'{path}: interlaced PNGs are not supported')
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    bpp = max(1, channels * depth // 8)
    stride = (width * channels * depth + 7) // 8
    data = zlib.decompress(bytes(idat))
    rows, prev = [], bytearray(stride)
    p = 0
    for _ in range(height):
        ftype = data[p]
        line = bytearray(data[p + 1:p + 1 + stride])
        p += 1 + stride
        if ftype == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif ftype == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ftype == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif ftype == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        rows.append(line)
        prev = line

    out = bytearray(width * height * 4)
    o = 0
    for line in rows:
        if depth < 8:
            per = 8 // depth
            mask = (1 << depth) - 1
            samples = [(line[x // per] >> (8 - depth * (x % per + 1))) & mask for x in range(width)]
        elif depth == 16:
            samples = [line[i] for i in range(0, len(line), 2)]
        else:
            samples = line
        if ctype == 3:
            for x in range(width):
                i = samples[x]
                out[o:o + 3] = palette[i * 3:i * 3 + 3]
                out[o + 3] = trns[i] if trns is not None and i < len(trns) else 255
                o += 4
        elif ctype == 0:
            scale = 255 // ((1 << depth) - 1) if depth < 8 else 1
            key = struct.unpack('>H', trns[:2])[0] if trns else None
            for x in range(width):
                v = samples[x]
                out[o:o + 4] = bytes((v * scale, v * scale, v * scale, 0 if v == key else 255))
                o += 4
        elif ctype == 2:
            key = tuple(struct.unpack('>HHH', trns[:6])) if trns else None
            for x in range(width):
                r, g, b = samples[x * 3], samples[x * 3 + 1], samples[x * 3 + 2]
                out[o:o + 4] = bytes((r, g, b, 0 if key == (r, g, b) else 255))
                o += 4
        elif ctype == 4:
            for x in range(width):
                v = samples[x * 2]
                out[o:o + 4] = bytes((v, v, v, samples[x * 2 + 1]))
                o += 4
        else:
            out[o:o + width * 4] = bytes(samples[:width * 4])
            o += width * 4
    return width, height, out


def png_write(path, width, height, rgba):
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF)
    lines = bytearray()
    for y in range(height):
        lines.append(0)
        lines += rgba[y * width * 4:(y + 1) * width * 4]
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(bytes(lines), 9)))
        f.write(chunk(b'IEND', b''))


def crop(width, height, rgba, x, y, w, h):
    out = bytearray(w * h * 4)
    for row in range(h):
        sy = y + row
        if 0 <= sy < height:
            x0, x1 = max(0, x), min(width, x + w)
            if x1 > x0:
                src = (sy * width + x0) * 4
                dst = (row * w + (x0 - x)) * 4
                out[dst:dst + (x1 - x0) * 4] = rgba[src:src + (x1 - x0) * 4]
    return out


def resize(width, height, rgba, nw, nh):
    """Area-average scale with premultiplied alpha: crisp edges, no dark fringes."""
    sx, sy = width / nw, height / nh
    out = bytearray(nw * nh * 4)
    for ty in range(nh):
        fy0, fy1 = ty * sy, (ty + 1) * sy
        ys = range(int(fy0), min(height, int(fy1 + 0.999999)))
        wy = [min(fy1, y + 1) - max(fy0, y) for y in ys]
        for tx in range(nw):
            fx0, fx1 = tx * sx, (tx + 1) * sx
            xs = range(int(fx0), min(width, int(fx1 + 0.999999)))
            wx = [min(fx1, x + 1) - max(fx0, x) for x in xs]
            r = g = b = a = total = 0.0
            for y, wyy in zip(ys, wy):
                base = y * width
                for x, wxx in zip(xs, wx):
                    wgt = wyy * wxx
                    i = (base + x) * 4
                    alpha = rgba[i + 3] * wgt
                    r += rgba[i] * alpha
                    g += rgba[i + 1] * alpha
                    b += rgba[i + 2] * alpha
                    a += alpha
                    total += wgt
            o = (ty * nw + tx) * 4
            if a > 0:
                out[o:o + 4] = bytes((min(255, round(r / a)), min(255, round(g / a)), min(255, round(b / a)),
                                      min(255, round(a / total))))
    return out


def fit_size(width, height, box_w, box_h):
    scale = min(box_w / width, box_h / height)
    return max(1, round(width * scale)), max(1, round(height * scale))


def opaque_bounds(width, height, rgba):
    xs = [x for x in range(width) if any(rgba[(y * width + x) * 4 + 3] for y in range(height))]
    ys = [y for y in range(height) if any(rgba[(y * width + x) * 4 + 3] for x in range(width))]
    if not xs:
        return 0, 0, width, height
    return xs[0], ys[0], xs[-1] - xs[0] + 1, ys[-1] - ys[0] + 1


def run(*args):
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(' '.join(args) + '\n' + result.stdout + result.stderr)


# ---------------------------------------------------------------- the build

def main():
    src, root = sys.argv[1], sys.argv[2]
    app = os.path.join(src, 'src', 'PKForge.App', 'Resources')
    domain = os.path.join(src, 'src', 'PKForge.Domain', 'Resources')
    core = os.path.join(root, 'external', 'PKSM-Core')
    romfs = os.path.join(root, 'romfs')
    work = os.path.join(root, 'build', 'assets')
    for d in (romfs, work):
        os.makedirs(d, exist_ok=True)

    # PKSM-Core's strings and personal tables, read at runtime from RomFS.
    shutil.copytree(os.path.join(core, 'strings'), os.path.join(romfs, 'i18n'), dirs_exist_ok=True)
    shutil.copytree(os.path.join(core, 'personals'), os.path.join(romfs, 'personal'), dirs_exist_ok=True)

    # PKForge's tables: PKHeX form -> Showdown sprite stem and icon.
    os.makedirs(os.path.join(romfs, 'data'), exist_ok=True)
    shutil.copy(os.path.join(domain, 'showdownsprites.tsv'), os.path.join(romfs, 'data', 'showdownsprites.tsv'))

    # PKForge's dex facts (PokeAPI, BSD-3): move types and texts, ability texts, genus and a Pokédex entry.
    import gzip
    with gzip.open(os.path.join(domain, 'dexfacts.tsv.gz'), 'rt', encoding='utf-8') as f:
        facts = f.read().splitlines()
    moves, abilities, genus, flavor = [], [], {}, {}
    for line in facts:
        f = line.split('\t')
        if f[0] == 'M' and len(f) >= 7:
            moves.append('\t'.join(f[1:7]))
        elif f[0] == 'A' and len(f) >= 3:
            abilities.append(f[1] + '\t' + f[2])
        elif f[0] == 'S' and len(f) >= 3:
            genus[int(f[1])] = f[2]
        elif f[0] == 'F' and len(f) >= 4 and f[3]:
            flavor.setdefault(int(f[1]), (f[2], f[3].replace('\t', ' ')))
    data = os.path.join(romfs, 'data')
    with open(os.path.join(data, 'moves.tsv'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(moves) + '\n')
    with open(os.path.join(data, 'abilities.tsv'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(abilities) + '\n')
    with open(os.path.join(data, 'species.tsv'), 'w', encoding='utf-8', newline='\n') as f:
        for sp in sorted(set(genus) | set(flavor)):
            game, text = flavor.get(sp, ('', ''))
            f.write(f'{sp}\t{genus.get(sp, "")}\t{game}\t{text}\n')

    # NDS12, the DS system font PKForge uses everywhere, with its licence beside it.
    os.makedirs(os.path.join(romfs, 'font'), exist_ok=True)
    fonts = os.path.join(app, 'Fonts')
    run('mkbcfnt', '-s', '8', '-o', os.path.join(romfs, 'font', 'nds12.bcfnt'), os.path.join(fonts, 'NDS12.ttf'))
    licences = os.path.join(romfs, 'licenses')
    os.makedirs(licences, exist_ok=True)
    for name in ('NDS12-license.txt', 'NDS12-readme.txt'):
        shutil.copy(os.path.join(fonts, name), os.path.join(licences, name))
    shutil.copy(os.path.join(app, 'Showdown', 'ATTRIBUTION.md'), os.path.join(licences, 'Showdown-ATTRIBUTION.md'))
    shutil.copy(os.path.join(app, 'UI', 'ATTRIBUTION.md'), os.path.join(licences, 'UI-ATTRIBUTION.md'))

    jobs = []

    # Box icons: Showdown's sheet, cut into pages of 34 rows (12 x 34 icons, 480x1020 -> 512x1024).
    gfx = os.path.join(romfs, 'gfx')
    os.makedirs(gfx, exist_ok=True)
    sw, sh, sheet = png_read(os.path.join(app, 'Showdown', 'icons.png'))
    rows_per_page = 34
    page = 0
    for top in range(0, sh, rows_per_page * 30):
        h = min(rows_per_page * 30, sh - top)
        page_png = os.path.join(work, f'icons{page}.png')
        png_write(page_png, sw, h, crop(sw, sh, sheet, 0, top, sw, h))
        jobs.append(('tex3ds', page_png, '-f', 'rgba5551', '-z', 'auto', '-o', os.path.join(gfx, f'icons{page}.t3x')))
        page += 1
    with open(os.path.join(romfs, 'data', 'icons.txt'), 'w', newline='\n') as f:
        f.write(f'{page} {rows_per_page} 12 40 30\n')

    # Front sprites, normal and shiny: one small texture each, loaded on demand.
    for folder, out_name in (('front', 'front'), ('front-shiny', 'shiny')):
        out = os.path.join(romfs, out_name)
        os.makedirs(out, exist_ok=True)
        for name in sorted(os.listdir(os.path.join(app, 'Showdown', folder))):
            if name.endswith('.png'):
                jobs.append(('tex3ds', os.path.join(app, 'Showdown', folder, name), '-f', 'rgba5551', '-z', 'auto',
                             '-o', os.path.join(out, name[:-4] + '.t3x')))

    # UI atlas: PKForge's own icons, scaled to 3DS sizes.
    ui_dir = os.path.join(work, 'ui')
    os.makedirs(ui_dir, exist_ok=True)
    ui = []

    def ui_image(name, path, box=None, trim=False):
        w, h, px = png_read(path)
        if trim:
            x, y, tw, th = opaque_bounds(w, h, px)
            px, w, h = crop(w, h, px, x, y, tw, th), tw, th
        if box and (w > box[0] or h > box[1]):
            nw, nh = fit_size(w, h, *box)
            px, w, h = resize(w, h, px, nw, nh), nw, nh
        out = os.path.join(ui_dir, name + '.png')
        png_write(out, w, h, px)
        ui.append(name)

    pksm = os.path.join(app, 'UI', 'pksm')
    design = os.path.join(app, 'UI', 'design')
    ui_image('logo', os.path.join(src, 'docs', 'images', 'logo-transparent.png'), (84, 100))
    ui_image('logo_small', os.path.join(src, 'docs', 'images', 'logo-transparent.png'), (20, 24))
    for name, file, box in (
        ('bank', 'pkf_bank.png', (28, 28)), ('trainer', 'pkf_trainer.png', (28, 28)),
        ('bag', 'icon_bag.png', (28, 28)), ('storage', 'icon_storage.png', (28, 28)),
        ('events', 'icon_events.png', (32, 24)), ('settings', 'icon_settings.png', (26, 26)),
        ('party', 'icon_party.png', (28, 28)), ('editor', 'icon_editor.png', (28, 28)),
        ('search', 'icon_search.png', (20, 20)), ('folder', 'icon_folder.png', (28, 28)),
        ('credits', 'icon_credits.png', (28, 28)), ('shiny', 'icon_shiny.png', None),
        ('pointer', 'pointer_arrow.png', None),
    ):
        ui_image(name, os.path.join(pksm, file), box)
    for name, file, box in (
        ('male', 'gi_male.png', (11, 11)), ('female', 'gi_female.png', (11, 11)),
        ('egg', 'px_egg.png', (26, 26)), ('box', 'px_box.png', (22, 22)), ('ball', 'px_ball.png', (14, 14)),
        ('copy', 'px_copy.png', (18, 18)), ('trash', 'px_delete.png', (18, 18)),
        ('create', 'px_create.png', (18, 18)), ('check', 'px_check.png', (14, 14)),
        ('close', 'px_close.png', (14, 14)),
    ):
        ui_image(name, os.path.join(pksm, file), box, trim=True)
    for name in ('info', 'stats', 'moves', 'origin', 'legality'):
        ui_image('tab_' + name, os.path.join(design, f'tab_{name}.png'), (16, 16), trim=True)

    # Game cartridges for the save cards.
    game_art = os.path.join(app, 'GameArt')
    games = []
    for name in sorted(os.listdir(game_art)):
        if name.endswith('.png'):
            stem = 'game_' + name[:-4].replace('-', '_')
            ui_image(stem, os.path.join(game_art, name), (44, 44))
            games.append((name[:-4], stem))

    with open(os.path.join(ui_dir, 'ui.t3s'), 'w', newline='\n') as f:
        f.write('--atlas -f rgba8 -z auto\n')
        for name in ui:
            f.write(name + '.png\n')
    with open(os.path.join(root, 'source', 'ui_ids.hpp'), 'w', newline='\n') as f:
        f.write('// Generated by tools/assets.py: indices into romfs:/gfx/ui.t3x.\n#pragma once\n\nnamespace ui\n{\n')
        for i, name in enumerate(ui):
            f.write(f'    constexpr int {name} = {i};\n')
        f.write(f'    constexpr int count = {len(ui)};\n\n')
        f.write('    // Cartridge art by PKForge game-art file name.\n')
        f.write('    struct GameArt { const char* name; int image; };\n')
        f.write('    inline constexpr GameArt gameArt[] = {\n')
        for name, stem in games:
            f.write(f'        {{"{name}", {stem}}},\n')
        f.write('    };\n}\n')
    jobs.append(('tex3ds', '-i', os.path.join(ui_dir, 'ui.t3s'), '-o', os.path.join(gfx, 'ui.t3x')))

    # The home-menu icon.
    w, h, px = png_read(os.path.join(app, 'AppIcon', 'pkforge.png'))
    png_write(os.path.join(root, 'icon.png'), 48, 48, resize(w, h, px, 48, 48))

    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        for future in concurrent.futures.as_completed([pool.submit(run, *job) for job in jobs]):
            future.result()
    print(f'assets: {len(jobs)} textures, {page} icon pages, {len(ui)} UI images')


if __name__ == '__main__':
    main()
