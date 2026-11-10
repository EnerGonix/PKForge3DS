"""Writes the title version (what FBI / 3dsDL show) into a CIA's TMD and ticket.
makerom 0.19 drops the minor number (-ver 1040 / -minor 1 both come out as 1.0.0), so cia.sh patches it here.
Both fields sit in the signed headers; CIAs made by makerom carry no real signature anyway (CFW sig patches).
Usage: cia_version.py <file.cia> <major.minor.micro>"""
import struct
import sys

path, ver = sys.argv[1], sys.argv[2]
major, minor, micro = (int(x) for x in (ver.split('.') + ['0', '0'])[:3])
value = (major & 63) << 10 | (minor & 63) << 4 | (micro & 15)

SIG_SIZE = {0x10003: 0x240, 0x10004: 0x140, 0x10005: 0x80}


def align(x):
    return (x + 63) & ~63


with open(path, 'r+b') as f:
    head = f.read(0x20)
    header_size, _, _, cert_size, ticket_size, tmd_size = struct.unpack_from('<IHHIII', head, 0)
    ticket = align(header_size) + align(cert_size)
    tmd = ticket + align(ticket_size)
    for start, field in ((ticket, 0xA6), (tmd, 0x9C)):
        f.seek(start)
        sig = struct.unpack('>I', f.read(4))[0]
        f.seek(start + SIG_SIZE[sig] + field)
        f.write(struct.pack('>H', value))
print(f'title version {major}.{minor}.{micro} ({value})')
