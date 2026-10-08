import struct, sys
def load(path):
    d = open(path,'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    nsec = struct.unpack_from('<H', d, pe+6)[0]
    optsz = struct.unpack_from('<H', d, pe+20)[0]
    opt = pe+24
    base = struct.unpack_from('<I', d, opt+28)[0]
    secs = []
    for i in range(nsec):
        name, vsz, va, rsz, rptr = struct.unpack_from('<8sIIII', d, opt+optsz+i*40)
        secs.append((name.rstrip(b'\0').decode(), va, vsz, rptr, rsz))
    return d, base, secs
def off2va(path, off):
    d, base, secs = load(path)
    for n, va, vsz, rp, rsz in secs:
        if rp <= off < rp+rsz: return base + va + (off-rp)
if __name__ == '__main__':
    d, base, secs = load(sys.argv[1])
    print('base', hex(base))
    for s in secs: print(s[0], hex(s[1]), hex(s[2]), hex(s[3]), hex(s[4]))
    for o in sys.argv[2:]:
        print(o, '->', hex(off2va(sys.argv[1], int(o,16))))
