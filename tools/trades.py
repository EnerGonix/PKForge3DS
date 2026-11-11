"""Packs PKHeX's in-game trades (Generations 3 to 7) for PKForge 3DS's Catch in the wild.

PKHeX (GPLv3) defines them as C# lists in PKHeX.Core/Legality/Encounters/Data/GenN/Encounters*.cs
(EncounterTrade3, EncounterTrade4PID, EncounterTrade5BW, EncounterTrade5B2W2, EncounterTrade6,
EncounterTrade7), the nickname and trainer name of each in PKHeX.Core/Resources/legality/genN/
text_<trades>_<lang>.txt (nicknames first, then the trainer names, at index + half the file).

Writes romfs/enc/t<version>.txt, one trade per line, tab-separated:
    gen species form level location pid(hex, or R for random) tid sid otGender gender(0/1/2, R) nature(0-24, R)
    ability(0 any, 1 first, 2 second, 3 hidden) ivs(6, -1 random) contest(6) moves(4) memory(4: memory,
    intensity, feeling, variable) fixedNickname(0/1) evolve(0/1) nicknames(lang=name|...) trainers(lang=name|...)
Usage: trades.py <PKHeX checkout> <out romfs dir>"""
import os
import re
import sys

PKHEX, OUT = sys.argv[1], sys.argv[2]
DATA = os.path.join(PKHEX, 'PKHeX.Core', 'Legality', 'Encounters', 'Data')
TEXT = os.path.join(PKHEX, 'PKHeX.Core', 'Resources', 'legality')

VERSIONS = {
    'S': [1], 'R': [2], 'E': [3], 'FR': [4], 'LG': [5], 'RS': [1, 2], 'RSE': [1, 2, 3], 'FRLG': [4, 5],
    'D': [10], 'P': [11], 'Pt': [12], 'DP': [10, 11], 'DPPt': [10, 11, 12], 'HG': [7], 'SS': [8], 'HGSS': [7, 8],
    'W': [20], 'B': [21], 'BW': [20, 21], 'W2': [22], 'B2': [23], 'B2W2': [22, 23],
    'X': [24], 'Y': [25], 'XY': [24, 25], 'AS': [26], 'OR': [27], 'ORAS': [26, 27],
    'SN': [30], 'MN': [31], 'SM': [30, 31], 'US': [32], 'UM': [33], 'USUM': [32, 33],
}
NATURES = ['Hardy', 'Lonely', 'Brave', 'Adamant', 'Naughty', 'Bold', 'Docile', 'Relaxed', 'Impish', 'Lax', 'Timid',
           'Hasty', 'Serious', 'Jolly', 'Naive', 'Modest', 'Mild', 'Quiet', 'Bashful', 'Rash', 'Calm', 'Gentle',
           'Sassy', 'Careful', 'Quirky']
ABILITY = {'Any12': 0, 'Any12H': 0, 'OnlyFirst': 1, 'OnlySecond': 2, 'OnlyHidden': 3}
TYPES = {'EncounterTrade3': 3, 'EncounterTrade4PID': 4, 'EncounterTrade5BW': 5, 'EncounterTrade5B2W2': 5,
         'EncounterTrade6': 6, 'EncounterTrade7': 7}
LOCATION = {3: 254, 4: 2001, 5: 30002, 6: 30001, 7: 30001}


def split_top(text):
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
    return int(v) if re.fullmatch(r'-?\d+', v) else None


def names_for(gen, base):
    """{lang: lines} for text_<base>_<lang>.txt of a generation."""
    folder = os.path.join(TEXT, 'gen%d' % gen)
    out = {}
    for f in os.listdir(folder):
        m = re.fullmatch(r'text_%s_([A-Za-z-]+)\.txt' % base, f)
        if m:
            out[m.group(1)] = open(os.path.join(folder, f), encoding='utf-8-sig').read().split('\n')
    return out


def main():
    out = {}
    kept = skipped = 0
    for gen in range(3, 8):
        folder = os.path.join(DATA, 'Gen%d' % gen)
        for fname in sorted(os.listdir(folder)):
            if 'Colo' in fname or 'XD' in fname or 'GG' in fname:
                continue
            text = open(os.path.join(folder, fname), encoding='utf-8').read()
            const = re.search(r'const string trade\w+\s*=\s*"(\w+)"', text)
            if not const:
                continue
            names = names_for(gen, const.group(1))
            contests = {m.group(1): [int(x) for x in re.findall(r'\d+', m.group(2))]
                        for m in re.finditer(r'ReadOnlySpan<byte>\s+(\w+)\s*=>\s*\[([^\]]*)\]', text)}
            for m in re.finditer(r'(EncounterTrade\w+)\[\]\s+\w+\s*=[^\[]*\[(.*?)\n\s*\];', text, re.S):
                kind = m.group(1)
                if kind not in TYPES:
                    continue
                for e in re.finditer(r'new\(TradeNames,\s*([^)]*)\)\s*\{([^}]*)\}', m.group(2)):
                    args = split_top(e.group(1))
                    props = {}
                    for p in split_top(e.group(2)):
                        if '=' in p:
                            k, v = p.split('=', 1)
                            props[k.strip()] = v.strip()
                    idx, ver = number(args[0]), args[1].strip()
                    if ver not in VERSIONS or idx is None:
                        skipped += 1
                        continue
                    pid = 'R'
                    species, level, form = number(props.get('Species', '')), number(props.get('Level', '')), number(props.get('Form', '0')) or 0
                    memory = [0, 0, 0, 0]
                    if kind in ('EncounterTrade3', 'EncounterTrade4PID'):
                        pid, species, level = '%08X' % number(args[2]), number(args[3]), number(args[4])
                    elif kind == 'EncounterTrade5BW':
                        pid = '%08X' % number(args[2])
                    elif kind == 'EncounterTrade6':
                        memory = [number(x) for x in args[2:6]]
                    elif kind == 'EncounterTrade7':
                        memory = [1, 3, 5, 40]
                    if species is None or level is None:
                        skipped += 1
                        continue
                    tid, sid = number(props.get('TID16', '0')) or 0, number(props.get('SID16', '0')) or 0
                    if 'ID32' in props:
                        id32 = number(props['ID32'])
                        tid, sid = id32 & 0xFFFF, id32 >> 16
                    gender = props.get('Gender', 'R')
                    nature = props.get('Nature', 'Nature.Random').split('.')[-1]
                    nature = str(NATURES.index(nature)) if nature in NATURES else 'R'
                    ability = ABILITY.get(props.get('Ability', 'AbilityPermission.Any12').split('.')[-1], 0)
                    ivs = [-1] * 6
                    if props.get('IVs', 'default') != 'default':
                        ivs = [number(x) for x in split_top(props['IVs'][props['IVs'].index('(') + 1:-1])]
                    contest = [0] * 6
                    c = props.get('Contest')
                    if c is not None:
                        if number(c) is not None:
                            contest = [number(c)] * 5 + [0]
                        elif c in contests:
                            contest = contests[c]
                    moves = [0, 0, 0, 0]
                    if 'Moves' in props:
                        moves = ([number(x) for x in re.findall(r'\d+', props['Moves'])] + [0] * 4)[:4]
                    location = number(props.get('MetLocation', '0')) or LOCATION[gen]
                    fixed = 0 if props.get('IsFixedNickname', 'true') == 'false' else 1
                    if kind == 'EncounterTrade5BW' and 'IsFixedNickname' not in props:
                        fixed = 1
                    evolve = 1 if props.get('EvolveOnTrade', 'false') == 'true' else 0
                    nicks, ots = [], []
                    for lang, lines in sorted(names.items()):
                        half = len([l for l in lines if l.strip() or True]) // 2
                        if idx < len(lines) and idx + half < len(lines):
                            nicks.append('%s=%s' % (lang, lines[idx].strip()))
                            ots.append('%s=%s' % (lang, lines[idx + half].strip()))
                    row = [gen, species, form, level, location, pid, tid, sid, number(props.get('OTGender', '0')) or 0,
                           gender if gender != 'R' else 'R', nature, ability, ','.join(map(str, ivs)),
                           ','.join(map(str, contest)), ','.join(map(str, moves)), ','.join(map(str, memory)),
                           fixed, evolve, '|'.join(nicks), '|'.join(ots)]
                    kept += 1
                    for v in VERSIONS[ver]:
                        out.setdefault(v, []).append('\t'.join(map(str, row)))
    enc = os.path.join(OUT, 'enc')
    os.makedirs(enc, exist_ok=True)
    for v, rows in out.items():
        open(os.path.join(enc, 't%d.txt' % v), 'w', encoding='utf-8', newline='\n').write('\n'.join(rows) + '\n')
        print(v, len(rows))
    print('kept', kept, 'skipped', skipped)


if __name__ == '__main__':
    main()
