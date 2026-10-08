import struct, sys
def exports(path):
    d = open(path,'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    assert d[pe:pe+4] == b'PE\0\0'
    machine, nsec = struct.unpack_from('<HH', d, pe+4)
    optsz = struct.unpack_from('<H', d, pe+20)[0]
    opt = pe+24
    magic = struct.unpack_from('<H', d, opt)[0]
    ddir = opt + (96 if magic == 0x10b else 112)
    exp_rva, exp_sz = struct.unpack_from('<II', d, ddir)
    secs = []
    so = opt + optsz
    for i in range(nsec):
        name, vsz, va, rsz, rptr = struct.unpack_from('<8sIIII', d, so + i*40)
        secs.append((va, max(vsz, rsz), rptr))
    def r2o(rva):
        for va, sz, rp in secs:
            if va <= rva < va+sz: return rva - va + rp
        raise ValueError(hex(rva))
    out = {'machine': hex(machine), 'exports': []}
    if not exp_rva: return out
    e = r2o(exp_rva)
    nfun, nnames, afun, anames, aord = struct.unpack_from('<IIIII', d, e+20)
    for i in range(nnames):
        nrva = struct.unpack_from('<I', d, r2o(anames)+4*i)[0]
        no = r2o(nrva); name = d[no:d.index(b'\0', no)].decode()
        out['exports'].append(name)
    return out
for p in sys.argv[1:]:
    r = exports(p); print(p, r['machine'], len(r['exports'])); print('  ' + ' '.join(r['exports']))
