"""List the sound banks in KART.HWL with the SPU memory each one needs.

Usage: howl_banks.py DISC.bin [--bank N ...] [--samples]

Reads KART.HWL straight from the NTSC-U disc image (raw 2352-byte sectors).
For each bank: its id, sample count and the bytes Bank_AssignSpuAddrs uploads
(the sum of its samples' sizes, game/HOWL/HOWL_Bank.c). --samples also lists
each sample's index and size. Use it to check that a set of banks loaded
together fits under the SPU ceiling (include/platform/native_spu_memory.h).
"""
import argparse
import struct
import sys

RAW = 2352
DATA = 24  # Mode 2 Form 1: sync, header and subheader before the 2048 data bytes


def read_sector(f, lba):
    f.seek(lba * RAW + DATA)
    return f.read(2048)


def find_file(f, path):
    pvd = read_sector(f, 16)
    root = pvd[156:156 + 34]
    lba, size = struct.unpack_from('<I', root, 2)[0], struct.unpack_from('<I', root, 10)[0]
    for part in path.strip('\\').split('\\'):
        found = None
        for s in range((size + 2047) // 2048):
            sec = read_sector(f, lba + s)
            off = 0
            while off < 2048 and sec[off]:
                n = sec[off]
                name = sec[off + 33:off + 33 + sec[off + 32]].decode('ascii', 'replace').split(';')[0]
                if name.upper() == part.upper():
                    found = struct.unpack_from('<I', sec, off + 2)[0], struct.unpack_from('<I', sec, off + 10)[0]
                off += n
        if found is None:
            sys.exit(f'{path} not found on the disc')
        lba, size = found
    return lba, size


def read_file(f, lba, size):
    return b''.join(read_sector(f, lba + s) for s in range((size + 2047) // 2048))[:size]


def load_banks(disc):
    with open(disc, 'rb') as f:
        hwl = read_file(f, *find_file(f, 'SOUNDS\\KART.HWL'))
    magic, version, _, _, num_spu, num_other, num_engine, num_banks, num_seq, header_size = struct.unpack_from('<10i', hwl, 0)
    off = 0x28
    spu = [struct.unpack_from('<HH', hwl, off + 4 * i) for i in range(num_spu)]
    off += 4 * num_spu + 8 * num_other + 8 * num_engine
    bank_offsets = struct.unpack_from(f'<{num_banks}H', hwl, off)
    banks = []
    for b, sector in enumerate(bank_offsets):
        base = sector * 2048
        n = struct.unpack_from('<h', hwl, base)[0]
        idx = struct.unpack_from(f'<{n}h', hwl, base + 2)
        banks.append((b, idx, sum(spu[i][1] for i in idx) * 8))
    return spu, banks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('disc')
    ap.add_argument('--bank', type=int, nargs='*')
    ap.add_argument('--samples', action='store_true')
    a = ap.parse_args()
    spu, banks = load_banks(a.disc)
    for b, idx, size in banks:
        if a.bank and b not in a.bank:
            continue
        print(f'bank {b:3d}: {len(idx):3d} samples, 0x{size:05x} bytes')
        if a.samples:
            print('   ', ' '.join(f'{i}:0x{spu[i][1] * 8:x}' for i in idx))


if __name__ == '__main__':
    main()
