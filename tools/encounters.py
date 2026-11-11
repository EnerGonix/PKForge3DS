"""Packs PKHeX's wild encounter tables (Generations 1 to 7) for PKForge 3DS's encounter browser.

PKHeX (GPLv3, https://github.com/kwsch/PKHeX) keeps them as "BinLinker" archives (2-byte id, u16 count,
count + 1 u32 offsets) of encounter areas, one archive per game, in
PKHeX.Core/Resources/legality/wild/GenN/encounter_<game>.pkl, and the met location names in
PKHeX.Core/Resources/text/locations/genN/text_<games>_00000_en.txt (line = location id).

Writes romfs/enc/<version>.bin (version = the save's game version, as PKSM-Core numbers it) and
romfs/enc/loc<gen>.txt. After a u16 count, 12-byte records:
    u16 species, u8 form, u8 min level, u8 max level, u8 type, u16 location, u8 time (Gold/Silver/Crystal),
    u8 slot number (Gen 1-4: the game's slot, which the RNG methods need), u8 ground tile (Gen 4: the
    lowest tile the area allows, which PK4 stores as its encounter type), u8 area rate (Gen 3-4: the Rock Smash
    proc and the Coronet Feebas sentinel 255 need it).

Usage: encounters.py <PKHeX checkout> <out romfs dir>"""
import os
import shutil
import struct
import sys

PKHEX, OUT = sys.argv[1], sys.argv[2]
WILD = os.path.join(PKHEX, 'PKHeX.Core', 'Resources', 'legality', 'wild')
TEXT = os.path.join(PKHEX, 'PKHeX.Core', 'Resources', 'text', 'locations')

# version (PKHeX GameVersion = PKSM-Core's) -> (generation, files)
GAMES = {
    35: (1, ['Gen1/encounter_red.pkl']), 36: (1, ['Gen1/encounter_blue.pkl']),
    37: (1, ['Gen1/encounter_blue_jp.pkl']), 38: (1, ['Gen1/encounter_yellow.pkl']),
    39: (2, ['Gen2/encounter_gold.pkl']), 40: (2, ['Gen2/encounter_silver.pkl']), 41: (2, ['Gen2/encounter_crystal.pkl']),
    1: (3, ['Gen3/encounter_s.pkl', 'Gen3/encounter_rse_swarm.pkl']),
    2: (3, ['Gen3/encounter_r.pkl', 'Gen3/encounter_rse_swarm.pkl']),
    3: (3, ['Gen3/encounter_e.pkl', 'Gen3/encounter_rse_swarm.pkl']),
    4: (3, ['Gen3/encounter_fr.pkl']), 5: (3, ['Gen3/encounter_lg.pkl']),
    10: (4, ['Gen4/encounter_d.pkl']), 11: (4, ['Gen4/encounter_p.pkl']), 12: (4, ['Gen4/encounter_pt.pkl']),
    7: (4, ['Gen4/encounter_hg.pkl']), 8: (4, ['Gen4/encounter_ss.pkl']),
    20: (5, ['Gen5/encounter_w.pkl']), 21: (5, ['Gen5/encounter_b.pkl']),
    22: (5, ['Gen5/encounter_w2.pkl']), 23: (5, ['Gen5/encounter_b2.pkl']),
    24: (6, ['Gen6/encounter_x.pkl']), 25: (6, ['Gen6/encounter_y.pkl']),
    26: (6, ['Gen6/encounter_as.pkl']), 27: (6, ['Gen6/encounter_or.pkl']),
    30: (7, ['Gen7/encounter_sn.pkl']), 31: (7, ['Gen7/encounter_mn.pkl']),
    32: (7, ['Gen7/encounter_us.pkl']), 33: (7, ['Gen7/encounter_um.pkl']),
}
NAMES = {1: 'gen2/text_gsc_00000_en.txt', 2: 'gen2/text_gsc_00000_en.txt', 3: 'gen3/text_rsefrlg_00000_en.txt',
         4: 'gen4/text_hgss_00000_en.txt', 5: 'gen5/text_bw2_00000_en.txt', 6: 'gen6/text_xy_00000_en.txt',
         7: 'gen7/text_sm_00000_en.txt'}


def entries(path):
    d = open(path, 'rb').read()
    count = struct.unpack_from('<H', d, 2)[0]
    offs = struct.unpack_from('<%dI' % (count + 1), d, 4)
    return [d[offs[i]:offs[i + 1]] for i in range(count)]


def areas(gen, area, swarm=False):
    """Yield (species, form, min, max, type, location, time, slot, tile, rate) for one area."""
    if gen in (1, 2):
        loc, time, typ = area[0], area[1], area[2]
        data = area[4:]
        if gen == 2 and typ > 1:  # rates first (one per slot), then the slots
            count = len(data) // 5
            data = data[count:]
        for i in range(0, len(data) - 3, 4):
            sp, slot, lo, hi = data[i:i + 4]
            if sp:
                yield sp, 0, lo, hi, typ, loc, time if gen == 2 else 0, slot, 0, area[3]
    elif gen in (3, 4):
        loc, typ = area[0], area[2]
        if swarm:
            typ = 6  # SwarmGrass50
        tile = 0
        if gen == 4:
            allowed = struct.unpack_from('<H', area, 4)[0]
            tile = next((b for b in range(16) if allowed >> b & 1), 0)
        data = area[6:] if gen == 4 else area[4:]
        for i in range(0, len(data) - 9, 10):
            sp, form, slot, lo, hi = struct.unpack_from('<HBBBB', data, i)
            if sp:
                yield sp, form, lo, hi, typ, loc, 0, slot, tile, area[3]
    else:
        loc, typ = struct.unpack_from('<HB', area, 0)
        for i in range(4, len(area) - 3, 4):
            raw, lo, hi = struct.unpack_from('<HBB', area, i)
            sp, form = raw & 0x7FF, raw >> 11
            if sp:
                yield sp, form, lo, hi, typ, loc, 0, 0, 0, 0


def main():
    enc = os.path.join(OUT, 'enc')
    os.makedirs(enc, exist_ok=True)
    for gen, name in NAMES.items():
        shutil.copyfile(os.path.join(TEXT, name), os.path.join(enc, 'loc%d.txt' % gen))
    total = 0
    for version, (gen, files) in GAMES.items():
        rows = set()
        for f in files:
            for area in entries(os.path.join(WILD, f)):
                if len(area) < 4:
                    continue
                for row in areas(gen, area, swarm='swarm' in f):
                    rows.add(row)
        rows = sorted(rows, key=lambda r: (r[0], r[5], r[4], r[2]))
        with open(os.path.join(enc, '%d.bin' % version), 'wb') as out:
            out.write(struct.pack('<H', len(rows)))
            for sp, form, lo, hi, typ, loc, time, slot, tile, rate in rows:
                out.write(struct.pack('<HBBBBHBBBB', sp, form, lo, hi, typ, loc, time, slot, tile, rate))
        total += len(rows)
        print(version, gen, len(rows))
    print('records', total)


if __name__ == '__main__':
    main()
