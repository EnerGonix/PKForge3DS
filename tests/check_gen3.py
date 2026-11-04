#!/usr/bin/env python3
"""Independent check of a Gen 3 save written by PKForge 3DS: section checksums of the newest slot,
and every boxed Pokémon's PK3 checksum (a mismatch is what the games show as a Bad Egg).
Usage: check_gen3.py <save.sav>"""
import struct, sys

LENGTHS = [0xF2C, 0xF80, 0xF80, 0xF80, 0xF08, 0xF80, 0xF80, 0xF80, 0xF80, 0xF80, 0xF80, 0xF80, 0xF80, 0x7D0]
ORDERS = ['GAEM', 'GAME', 'GEAM', 'GEMA', 'GMAE', 'GMEA', 'AGEM', 'AGME', 'AEGM', 'AEMG', 'AMGE', 'AMEG',
          'EGAM', 'EGMA', 'EAGM', 'EAMG', 'EMGA', 'EMAG', 'MGAE', 'MGEA', 'MAGE', 'MAEG', 'MEGA', 'MEAG']

def section_sum(data, length):
    total = sum(struct.unpack_from('<%dI' % (length // 4), data, 0)) & 0xFFFFFFFF
    return ((total >> 16) + (total & 0xFFFF)) & 0xFFFF

d = open(sys.argv[1], 'rb').read()
slots = []
for base in (0, 0xE000):
    index = struct.unpack_from('<I', d, base + 0xFFC)[0]
    slots.append((index, base))
index, base = max(slots)
print(f'newest slot at 0x{base:X}, save index {index}')
sections, bad = {}, 0
for i in range(14):
    off = base + i * 0x1000
    sid, chk, sig = struct.unpack_from('<HHI', d, off + 0xFF4)
    ok = sig == 0x08012025 and section_sum(d[off:off + LENGTHS[sid]], LENGTHS[sid]) == chk
    bad += not ok
    sections[sid] = d[off:off + LENGTHS[sid]]
print('section checksums:', 'all valid' if bad == 0 else f'{bad} BAD')

pc = b''.join(sections[i] for i in range(5, 14))
mons, badmons = 0, 0
for box in range(14):
    for slot in range(30):
        pk = pc[4 + (box * 30 + slot) * 80:4 + (box * 30 + slot + 1) * 80]
        pid, otid = struct.unpack_from('<II', pk, 0)
        if pid == 0 and otid == 0:
            continue
        key = pid ^ otid
        raw = bytes(b for i in range(12) for b in struct.pack('<I', struct.unpack_from('<I', pk, 32 + i * 4)[0] ^ key))
        chk = sum(struct.unpack('<24H', raw)) & 0xFFFF
        stored = struct.unpack_from('<H', pk, 28)[0]
        order = ORDERS[pid % 24]
        sub = {c: raw[i * 12:(i + 1) * 12] for i, c in enumerate(order)}
        species = struct.unpack_from('<H', sub['G'], 0)[0]
        ivs = struct.unpack_from('<I', sub['M'], 4)[0]
        iv = [(ivs >> (5 * k)) & 31 for k in range(6)]
        mons += 1
        badmons += chk != stored
        print(f'box {box + 1} slot {slot + 1}: species {species} IVs {iv} checksum {"ok" if chk == stored else "BAD"}')
print(f'{mons} boxed Pokémon, {badmons} with a bad checksum')
