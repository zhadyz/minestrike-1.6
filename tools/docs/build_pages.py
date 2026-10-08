"""Build docs/index.html (GitHub Pages) from the report page docs/report/index.html.

The report is a body fragment (title, font links, style, markup) so it can also be published as a
Claude artifact. GitHub Pages needs a full document, so this wraps it and points media at report/.
usage: python tools/docs/build_pages.py
"""
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, 'docs', 'report', 'index.html')
DST = os.path.join(ROOT, 'docs', 'index.html')

src = open(SRC, encoding='utf-8').read()
head_end = src.index('</style>') + len('</style>')
head, body = src[:head_end], src[head_end:].strip()
body = re.sub(r'\b(src|poster|href)="((?:img|media)/)', r'\1="report/\2', body)

page = f'''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="description" content="MineStrike 1.6: Minecraft built into Counter-Strike 1.6. The real de_dust2 you can dig, a survival inventory, hunger and elytra flight.">
<meta property="og:title" content="MineStrike 1.6">
<meta property="og:description" content="Minecraft built into Counter-Strike 1.6.">
<meta property="og:image" content="readme/social.png">
{head}
</head>
<body>
{body}
</body>
</html>
'''
open(DST, 'w', encoding='utf-8', newline='\n').write(page)
print('wrote', os.path.relpath(DST, ROOT))
