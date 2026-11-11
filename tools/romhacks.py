"""Packs the ROM hack tables the Android PKForge carries (src/PKForge.Engine/{RadicalRed,Unbound,GsChronicles}/Data,
GPLv3) into romfs/hacks/<hack>.txt for PKForge 3DS: every hack id bridged to the national id PKForge uses,
by name, as the original's tables do at run time.

Lines (tab-separated):  S id national form   (species; form 0 unless a regional suffix names one)
                        M id national         (moves)       I id national   (items)
                        A id a1 a2 hidden     (Unbound: the species' abilities, national ids)
Usage: romhacks.py <PKForge-3.1.1 checkout> <PKForge3DS dir>"""
import json
import os
import re
import sys
import unicodedata

SRC, ROOT = sys.argv[1], sys.argv[2]
ENGINE = os.path.join(SRC, 'src', 'PKForge.Engine')
I18N = os.path.join(ROOT, 'romfs', 'i18n', 'eng')


def norm(name):
    name = unicodedata.normalize('NFKD', name).encode('ascii', 'ignore').decode()
    name = name.replace('♀', 'f').replace('♂', 'm')
    return re.sub(r'[^a-z0-9]', '', name.lower())


def national(file):
    out = {}
    for i, line in enumerate(open(os.path.join(I18N, file), encoding='utf-8-sig').read().split('\n')):
        line = line.strip()
        if line:
            out.setdefault(norm(line.replace('♀', 'F').replace('♂', 'M')), i)
            out.setdefault(norm(line), i)
    return out


SPECIES, MOVES, ITEMS, ABILITIES = (national(f) for f in ('species.txt', 'moves.txt', 'items.txt', 'abilities.txt'))
SPECIES.update({'nidoranf': 29, 'nidoranm': 32})
ITEMS.setdefault('pokeball', 4)


def table(path, sep):
    out = {}
    for line in open(path, encoding='utf-8-sig').read().split('\n'):
        if not line.strip() or line.startswith('#'):
            continue
        parts = line.split(sep, 1)
        if len(parts) == 2 and parts[0].strip().isdigit():
            out[int(parts[0])] = parts[1].strip()
    return out


REGIONAL = {'alola': 1, 'alolan': 1, 'galar': 1, 'galarian': 1, 'hisui': 1, 'hisuian': 1, 'paldea': 1}


def species_row(name):
    base, form = name, 0
    n = norm(name)
    if n in SPECIES:
        return SPECIES[n], 0
    while '-' in base:
        suffix = base[base.rfind('-') + 1:]
        base = base[:base.rfind('-')]
        if norm(base) in SPECIES:
            return SPECIES[norm(base)], REGIONAL.get(norm(suffix), 0)
    return 0, 0


def write(hack, species, moves, items, abilities=None, move_fallback=None):
    lines, unmatched = [], {'S': 0, 'M': 0, 'I': 0}
    for i, name in sorted(species.items()):
        nat, form = species_row(name)
        unmatched['S'] += nat == 0
        lines.append('S\t%d\t%d\t%d' % (i, nat, form))
    for i, name in sorted(moves.items()):
        nat = MOVES.get(norm(name), 0) or (move_fallback(i) if move_fallback else 0)
        unmatched['M'] += nat == 0
        lines.append('M\t%d\t%d' % (i, nat))
    for i, name in sorted(items.items()):
        first = name.split(' / ')[0]
        nat = ITEMS.get(norm(first), 0)
        unmatched['I'] += nat == 0 and not name.startswith('-') and name != '.'
        lines.append('I\t%d\t%d' % (i, nat))
    for i, a in sorted((abilities or {}).items()):
        lines.append('A\t%d\t%d\t%d\t%d' % (i, *a))
    os.makedirs(os.path.join(ROOT, 'romfs', 'hacks'), exist_ok=True)
    open(os.path.join(ROOT, 'romfs', 'hacks', hack + '.txt'), 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
    print(hack, len(species), 'species', len(moves), 'moves', len(items), 'items; unmatched', unmatched)


# Unbound: its own species, moves, items and per-species abilities.
ub = os.path.join(ENGINE, 'Unbound', 'Data')
ub_species = table(os.path.join(ub, 'pokemon.txt'), ':')
ub_moves = table(os.path.join(ub, 'moves.txt'), ':')
ub_items = table(os.path.join(ub, 'items.txt'), ':')
ub_ability_names = table(os.path.join(ub, 'abilities.txt'), ':')
meta = json.load(open(os.path.join(ub, 'species_abilities_meta.json'), encoding='utf-8'))
ub_abilities = {}
for sid, a in meta.items():
    conv = [ABILITIES.get(norm(ub_ability_names.get(a.get(k, 0) or 0, '')), 0)
            for k in ('ability_1_id', 'ability_2_id', 'hidden_ability_id')]
    ub_abilities[int(sid)] = conv
ub_species.pop(0, None)
write('unbound', ub_species, ub_moves, ub_items, ub_abilities)

# Radical Red: own species and items; moves are the retail ids through 354, then CFRU's = Unbound's.
rr = os.path.join(ENGINE, 'RadicalRed', 'Data')
rr_moves = {i: n for i, n in ub_moves.items() if i <= 766}
write('radicalred', table(os.path.join(rr, 'species.txt'), '\t'), rr_moves, table(os.path.join(rr, 'items.txt'), '\t'),
      move_fallback=lambda i: i if i <= 354 else 0)

# GS Chronicles: its own tables.
gs = os.path.join(ENGINE, 'GsChronicles', 'Data')
write('gschronicles', table(os.path.join(gs, 'species.txt'), '\t'), table(os.path.join(gs, 'moves.txt'), '\t'),
      table(os.path.join(gs, 'items.txt'), '\t'))
