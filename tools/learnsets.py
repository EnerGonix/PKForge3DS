#!/usr/bin/env python3
"""Packs PokeAPI's learnsets (BSD-3, data/v2/csv in github.com/PokeAPI/pokeapi) into romfs/learnsets.bin
for PKForge's Legalize. Usage: learnsets.py <folder with the PokeAPI csv files> <out file>

Needs pokemon_moves.csv, pokemon.csv, pokemon_species.csv and version_groups.csv. Every form of a
species is merged into the species (so no form-only move is ever called illegal); the level-up list
is the default form's.

Layout, little-endian:
  "LRN1", u16 groups, u16 species count (index 0 unused)
  u16 evolvesFrom[species]                      the species it evolves from, 0 when none
  per group: u8 PokeAPI version group id, u8 generation, u32 offset of its table
  table: u32 offset[species]                    0 when the species has no learnset in that group
  record: u16 n, u16 move[n] (sorted)           every move it learns there, any way
          u16 m, (u16 move, u8 level)[m]        its level-up moves, by level
"""
import csv
import os
import struct
import sys
from collections import defaultdict


def rows(folder, name):
    with open(os.path.join(folder, name + '.csv'), newline='', encoding='utf-8') as f:
        yield from csv.DictReader(f)


def main():
    src, out = sys.argv[1], sys.argv[2]
    groups = {int(r['id']): int(r['generation_id']) for r in rows(src, 'version_groups')}
    species_of, default = {}, {}
    for r in rows(src, 'pokemon'):
        species_of[int(r['id'])] = int(r['species_id'])
        default[int(r['id'])] = r['is_default'] == '1'
    evolves, hatch = {}, {}
    for r in rows(src, 'pokemon_species'):
        evolves[int(r['id'])] = int(r['evolves_from_species_id'] or 0)
        hatch[int(r['id'])] = int(r['hatch_counter'] or 0)
    count = max(evolves) + 1
    # Egg cycles by species, one byte each, beside the learnsets (romfs/hatch.bin).
    with open(os.path.join(os.path.dirname(out) or '.', 'hatch.bin'), 'wb') as f:
        f.write(bytes(min(hatch.get(i, 0), 255) for i in range(count)))

    learns = defaultdict(lambda: defaultdict(set))  # group -> species -> moves
    levels = defaultdict(lambda: defaultdict(set))  # group -> species -> (level, move), default form
    for r in rows(src, 'pokemon_moves'):
        pid = int(r['pokemon_id'])
        if pid not in species_of:
            continue
        g, s, m = int(r['version_group_id']), species_of[pid], int(r['move_id'])
        learns[g][s].add(m)
        if r['pokemon_move_method_id'] == '1' and default[pid]:
            levels[g][s].add((int(r['level'] or 0), m))

    order = sorted(learns)
    head = b'LRN1' + struct.pack('<HH', len(order), count)
    head += struct.pack('<%dH' % count, *[evolves.get(i, 0) for i in range(count)])
    dir_size = len(order) * 6
    body = bytearray()
    entries = []
    base = len(head) + dir_size
    for g in order:
        table_at = base + len(body)
        offsets = [0] * count
        records = bytearray()
        rec_base = table_at + count * 4
        for s in sorted(learns[g]):
            if s >= count:
                continue
            offsets[s] = rec_base + len(records)
            moves = sorted(learns[g][s])
            ups = sorted(levels[g][s])
            records += struct.pack('<H%dH' % len(moves), len(moves), *moves)
            records += struct.pack('<H', len(ups))
            for lvl, m in ups:
                records += struct.pack('<HB', m, min(lvl, 255))
        body += struct.pack('<%dI' % count, *offsets) + records
        entries.append(struct.pack('<BBI', g, groups[g], table_at))
    data = head + b''.join(entries) + bytes(body)
    os.makedirs(os.path.dirname(out) or '.', exist_ok=True)
    with open(out, 'wb') as f:
        f.write(data)
    print(f'{len(order)} version groups, {count - 1} species, {len(data) / 1048576:.1f} MB')


if __name__ == '__main__':
    main()
