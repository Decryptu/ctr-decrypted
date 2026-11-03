"""Minimal NDS ROM reader: NitroFS listing, LZ77 (type 0x10) and NARC unpacking."""
import struct


def u16(b, o): return struct.unpack_from('<H', b, o)[0]
def u32(b, o): return struct.unpack_from('<I', b, o)[0]


def nitrofs(rom):
    fnt_off, _, fat_off, _ = struct.unpack_from('<IIII', rom, 0x40)
    files = {}

    def walk(dir_id, path):
        off, first, _ = struct.unpack_from('<IHH', rom, fnt_off + 8 * (dir_id & 0xfff))
        p, fid = fnt_off + off, first
        while True:
            b = rom[p]; p += 1
            if b == 0:
                return
            name = rom[p:p + (b & 0x7f)].decode('latin1'); p += b & 0x7f
            if b & 0x80:
                walk(u16(rom, p), path + name + '/'); p += 2
            else:
                s, e = struct.unpack_from('<II', rom, fat_off + 8 * fid)
                files[path + name] = rom[s:e]
                fid += 1
    walk(0xf000, '')
    return files


def lz10(data):
    assert data[0] == 0x10, 'not LZ10'
    size = u32(data, 0) >> 8
    out = bytearray()
    p = 4
    while len(out) < size:
        flags = data[p]; p += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if flags & (0x80 >> bit):
                b0, b1 = data[p], data[p + 1]; p += 2
                n = (b0 >> 4) + 3
                d = ((b0 & 0xf) << 8 | b1) + 1
                for _ in range(n):
                    out.append(out[-d])
            else:
                out.append(data[p]); p += 1
    return bytes(out)


def narc(data):
    """NARC -> {path: bytes}."""
    assert data[:4] == b'NARC'
    p = u16(data, 0x0c)
    sections = {}
    for _ in range(3):
        sections[data[p:p + 4]] = (p, u32(data, p + 4))
        p += u32(data, p + 4)
    fatb, _ = sections[b'BTAF']
    fntb, _ = sections[b'BTNF']
    gmif, _ = sections[b'GMIF']
    count = u16(data, fatb + 8)
    ranges = [struct.unpack_from('<II', data, fatb + 12 + 8 * i) for i in range(count)]
    fnt = fntb + 8
    files = {}

    def walk(dir_id, path):
        off, first, _ = struct.unpack_from('<IHH', data, fnt + 8 * (dir_id & 0xfff))
        q, fid = fnt + off, first
        while True:
            b = data[q]; q += 1
            if b == 0:
                return
            name = data[q:q + (b & 0x7f)].decode('latin1'); q += b & 0x7f
            if b & 0x80:
                walk(u16(data, q), path + name + '/'); q += 2
            else:
                s, e = ranges[fid]
                files[path + name] = data[gmif + 8 + s:gmif + 8 + e]
                fid += 1
    walk(0xf000, '')
    return files


def carc(data):
    return narc(lz10(data) if data[0] == 0x10 else data)
