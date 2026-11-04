#!/usr/bin/env python3
"""Lists the species an egg can hatch into, for the egg factory: the first stage of each evolution line
(babies included) that can breed. From PokeAPI's pokemon_species.csv (BSD-3): no pre-evolution, not
legendary or mythical; the rest of the Undiscovered egg group (Unown, Ditto, the Ultra Beasts, the Paradox
Pokémon, Gimmighoul) is left out by number, since that csv has no egg groups.
Usage: eggspecies.py <folder with pokemon_species.csv> <out file>   (one national number per line)"""
import csv
import os
import sys

NO_EGGS = {132, 201, 999} | set(range(793, 800)) | set(range(803, 807)) | set(range(984, 996)) | \
    {1005, 1006, 1009, 1010} | set(range(1020, 1024))


def main():
    src, out = sys.argv[1], sys.argv[2]
    ids = []
    with open(os.path.join(src, 'pokemon_species.csv'), newline='', encoding='utf-8') as f:
        for row in csv.DictReader(f):
            n = int(row['id'])
            if n > 1025 or row['evolves_from_species_id'] or n in NO_EGGS:
                continue
            if row['is_legendary'] == '1' or row['is_mythical'] == '1':
                continue
            ids.append(n)
    ids.sort()
    os.makedirs(os.path.dirname(out) or '.', exist_ok=True)
    with open(out, 'w', newline='\n') as f:
        f.write('\n'.join(map(str, ids)) + '\n')
    print(len(ids), 'species')


if __name__ == '__main__':
    main()
