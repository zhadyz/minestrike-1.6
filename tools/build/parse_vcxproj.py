"""Extract the Release|Win32 source list and compiler settings from ReGameDLL.vcxproj."""
import sys
import xml.etree.ElementTree as ET

NS = '{http://schemas.microsoft.com/developer/msbuild/2003}'
proj, out = sys.argv[1], sys.argv[2]
cfg = "'$(Configuration)|$(Platform)'=='Release|Win32'"
root = ET.parse(proj).getroot()

srcs = []
for it in root.iter(NS + 'ClCompile'):
    inc = it.get('Include')
    if not inc:
        continue
    excluded = False
    for ch in it:
        tag = ch.tag.replace(NS, '')
        cond = ch.get('Condition', '')
        if tag == 'ExcludedFromBuild' and cond in (cfg, '') and (ch.text or '').strip().lower() == 'true':
            excluded = True
    if not excluded:
        srcs.append(inc.replace(chr(92), '/'))
open(out, 'w').write('\n'.join(srcs) + '\n')
print('sources:', len(srcs))

for idg in root.iter(NS + 'ItemDefinitionGroup'):
    if idg.get('Condition') == cfg:
        for el in idg.iter():
            tag = el.tag.replace(NS, '')
            if el.text and el.text.strip() and tag not in ('ClCompile', 'Link', 'ItemDefinitionGroup'):
                print(tag, '=', el.text.strip()[:300])
