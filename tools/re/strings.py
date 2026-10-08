import re, sys
pat = re.compile(sys.argv[2], re.I) if len(sys.argv) > 2 else None
d = open(sys.argv[1], 'rb').read()
for m in re.finditer(rb'[\x20-\x7e]{5,}', d):
    s = m.group().decode('ascii')
    if not pat or pat.search(s): print(hex(m.start()), s)
