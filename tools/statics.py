"""Packs PKHeX's static and gift encounters (Generations 3 to 7) for PKForge 3DS's Catch in the wild.

PKHeX (GPLv3) keeps them as C# lists in PKHeX.Core/Legality/Encounters/Data/GenN/Encounters*.cs:
    Gen 3:    new(species, level, VERSION) { Prop = value, ... }, // comment
    Gen 4-7:  new(VERSION) { Species = n, Level = n, Prop = value, ... }, // comment
Only the EncounterStaticN[] lists are read, and only entries PKForge can make exactly as PKHeX does: entries
with fixed IVs, trainer data, egg locations, roaming, chain-shiny, events or other special rules are skipped.

Writes romfs/enc/s<version>.bin: u16 count, then 32-byte records:
    u16 species, u8 form, u8 level, u16 location, u8 ball, u8 ability (0 any of two, 1 first, 2 second,
    3 hidden), u8 shiny (0 random, 1 never), u8 flawless IVs, u8 gender (255 any), u8 nature (255 any),
    u8 ground tile (Gen 4), u8 flags (1 gift: Poké Ball fixed), u16 moves[4], u16 relearn[4], u16 reserved
Usage: statics.py <PKHeX checkout> <out romfs dir>"""
import os
import re
import struct
import sys

PKHEX, OUT = sys.argv[1], sys.argv[2]
DATA = os.path.join(PKHEX, 'PKHeX.Core', 'Legality', 'Encounters', 'Data')

VERSIONS = {
    'S': [1], 'R': [2], 'E': [3], 'FR': [4], 'LG': [5], 'RS': [1, 2], 'RSE': [1, 2, 3], 'FRLG': [4, 5],
    'D': [10], 'P': [11], 'Pt': [12], 'DP': [10, 11], 'DPPt': [10, 11, 12], 'HG': [7], 'SS': [8], 'HGSS': [7, 8],
    'W': [20], 'B': [21], 'BW': [20, 21], 'W2': [22], 'B2': [23], 'B2W2': [22, 23],
    'X': [24], 'Y': [25], 'XY': [24, 25], 'AS': [26], 'OR': [27], 'ORAS': [26, 27],
    'SN': [30], 'MN': [31], 'SM': [30, 31], 'US': [32], 'UM': [33], 'USUM': [32, 33],
}
BALLS = ['None', 'Master', 'Ultra', 'Great', 'Poke', 'Safari', 'Net', 'Dive', 'Nest', 'Repeat', 'Timer', 'Luxury',
         'Premier', 'Dusk', 'Heal', 'Quick', 'Cherish', 'Fast', 'Level', 'Lure', 'Heavy', 'Love', 'Friend', 'Moon',
         'Sport', 'Dream', 'Beast']
NATURES = ['Hardy', 'Lonely', 'Brave', 'Adamant', 'Naughty', 'Bold', 'Docile', 'Relaxed', 'Impish', 'Lax', 'Timid',
           'Hasty', 'Serious', 'Jolly', 'Naive', 'Modest', 'Mild', 'Quiet', 'Bashful', 'Rash', 'Calm', 'Gentle',
           'Sassy', 'Careful', 'Quirky']
TILES = {'None': 0, 'Sand': 1, 'Grass': 2, 'Puddle': 3, 'Rock': 4, 'Cave': 5, 'Snow': 6, 'Water': 7, 'Ice': 8,
         'Building': 9, 'Marsh': 10, 'Bridge': 11, 'Max_DP': 12}
ABILITY = {'Any12': 0, 'Any12H': 0, 'OnlyFirst': 1, 'OnlySecond': 2, 'OnlyHidden': 3}
SUPPORTED = {'Species', 'Level', 'Location', 'FixedBall', 'Moves', 'Relearn', 'Form', 'Ability', 'Shiny',
             'FlawlessIVCount', 'Gender', 'Nature', 'GroundTile'}


def split_top(text):
    """Split on commas outside parentheses/brackets."""
    out, depth, cur = [], 0, ''
    for ch in text:
        if ch in '([':
            depth += 1
        elif ch in ')]':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur.strip())
            cur = ''
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def number(v):
    v = v.strip()
    if re.fullmatch(r'0x[0-9A-Fa-f]+', v):
        return int(v, 16)
    return int(v) if re.fullmatch(r'\d+', v) else None  # "001" is decimal in C#


def parse_entry(head, body, gen):
    args = split_top(head)
    props = {}
    for p in split_top(body):
        if '=' in p:
            k, v = p.split('=', 1)
            props[k.strip()] = v.strip()
    if gen == 3:
        if len(args) != 3:
            return None
        species, level, ver = number(args[0]), number(args[1]), args[2].strip()
    else:
        if len(args) != 1:
            return None
        ver = args[0].strip()
        species, level = number(props.get('Species', '')), number(props.get('Level', ''))
    if ver not in VERSIONS or species is None or level is None:
        return None
    if set(props) - SUPPORTED:
        return None  # a property PKForge does not reproduce
    shiny = props.get('Shiny', 'Shiny.Random').split('.')[-1]
    if shiny not in ('Random', 'Never'):
        return None
    if gen == 4 and species == 172:
        return None  # Pichu: the Pokéwalker method
    loc = number(props.get('Location', '0'))
    ball = props.get('FixedBall', 'Ball.Poke').split('.')[-1]
    ability = ABILITY.get(props.get('Ability', 'AbilityPermission.Any12').split('.')[-1])
    nature = props.get('Nature')
    tile = props.get('GroundTile')
    if loc is None or ball not in BALLS or ability is None:
        return None

    def moves(key):
        v = props.get(key)
        if not v:
            return [0, 0, 0, 0]
        m = [number(x) for x in re.findall(r'\d+', v)][:4]
        return m + [0] * (4 - len(m))

    rec = dict(species=species, form=number(props.get('Form', '0')) or 0, level=level, location=loc,
               ball=BALLS.index(ball), ability=ability, shiny=1 if shiny == 'Never' else 0,
               flawless=number(props.get('FlawlessIVCount', '0')) or 0,
               gender=number(props['Gender']) if 'Gender' in props else 255,
               nature=NATURES.index(nature.split('.')[-1]) if nature else 255,
               tile=TILES.get(tile.split('.')[-1], 0) if tile else 0,
               flags=1 if 'FixedBall' in props and ball == 'Poke' else 0,
               moves=moves('Moves'), relearn=moves('Relearn'))
    return VERSIONS[ver], rec


def main():
    out = {}
    skipped = kept = 0
    for gen in range(3, 8):
        folder = os.path.join(DATA, 'Gen%d' % gen)
        for name in sorted(os.listdir(folder)):
            if 'Colo' in name or 'XD' in name or 'WC' in name or 'GG' in name or 'DR' in name:
                continue
            text = open(os.path.join(folder, name), encoding='utf-8').read()
            for m in re.finditer(r'EncounterStatic%d\[\]\s+\w+\s*=[^\[]*\[(.*?)\n\s*\];' % gen, text, re.S):
                for e in re.finditer(r'new\(([^)]*)\)\s*\{([^}]*)\}', m.group(1)):
                    r = parse_entry(e.group(1), e.group(2), gen)
                    if not r:
                        skipped += 1
                        continue
                    kept += 1
                    for v in r[0]:
                        out.setdefault(v, []).append(r[1])
    enc = os.path.join(OUT, 'enc')
    os.makedirs(enc, exist_ok=True)
    for v, recs in out.items():
        with open(os.path.join(enc, 's%d.bin' % v), 'wb') as f:
            f.write(struct.pack('<H', len(recs)))
            for r in recs:
                f.write(struct.pack('<HBBHBBBBBBBB4H4HH', r['species'], r['form'], r['level'], r['location'],
                                    r['ball'], r['ability'], r['shiny'], r['flawless'], r['gender'], r['nature'],
                                    r['tile'], r['flags'], *r['moves'], *r['relearn'], 0))
        print(v, len(recs))
    print('kept', kept, 'skipped', skipped)


if __name__ == '__main__':
    main()
