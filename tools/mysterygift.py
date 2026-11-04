#!/usr/bin/env python3
"""Packs PKHeX's Mystery Gift database (PKHeX.Core/Resources/legality/mgdb/*.pkl, every real
distribution) into romfs/mg for PKForge's Mystery Gift screen. Usage: mysterygift.py <pkl folder> <romfs>

Each .pkl is its cards back to back; romfs/mg/<name>.bin keeps them so. pgf.pkl carries one more byte
per card at the end (the version and language it was for), which is dropped here."""
import os
import sys

# name: card size (PKSM-Core's WCX lengths)
CARDS = {'wc4': 856, 'pgf': 204, 'wc6': 264, 'wc6full': 784, 'wc7': 264, 'wc7full': 784, 'wb7full': 784,
         'wc8': 0x2D0, 'wc9': 0x2C8, 'wa9': 0x2C8}


def main():
    src, romfs = sys.argv[1], sys.argv[2]
    out = os.path.join(romfs, 'mg')
    os.makedirs(out, exist_ok=True)
    total = 0
    for name, size in CARDS.items():
        data = open(os.path.join(src, name + '.pkl'), 'rb').read()
        if name == 'pgf':
            count = len(data) // (size + 1)
            data = data[:count * size]
        assert len(data) % size == 0, (name, len(data), size)
        open(os.path.join(out, name + '.bin'), 'wb').write(data)
        total += len(data) // size
        print(f'{name}: {len(data) // size} cards')
    print(f'{total} cards')


if __name__ == '__main__':
    main()
