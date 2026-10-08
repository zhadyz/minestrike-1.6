"""Render the report page (docs/index.html) into README images, section by section, in light and dark.

GitHub READMEs drop custom CSS and web fonts, so the README shows the page as pictures: one image per
section, a <picture> per image so GitHub's dark theme gets the dark rendering. Videos appear as their
posters. Headless Chromium (Playwright), no window.

usage: python tools/docs/build_pages.py && python tools/docs/render_readme.py
writes docs/readme/<nn>-<name>-{light,dark}.{png,jpg} and README.md
"""
import glob
import os
import re
import subprocess
import tempfile

from PIL import Image
from playwright.sync_api import sync_playwright

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PAGE = os.path.join(ROOT, 'docs', 'index.html')
OUT = os.path.join(ROOT, 'docs', 'readme')
WIDTH = 1000  # CSS px; the README column on github.com is about this wide
SCALE = 2     # sharp text on high-DPI screens


def chromium():
    base = os.environ.get('PLAYWRIGHT_BROWSERS_PATH', r'Z:\dev\cache\ms-playwright')
    shells = sorted(glob.glob(os.path.join(base, 'chromium_headless_shell-*', '*', 'chrome-headless-shell.exe')))
    return shells[-1] if shells else None


ANIM_WIDTH = 1200  # animated sections are scaled to this width (file size)
ANIM_FPS = 10
ANIM_SECONDS = 10


def animate(still_path, video_path, box, out_path):
    """Play the clip inside its slot: frames of the video pasted over the still at box (device px)."""
    base = Image.open(still_path).convert('RGB')
    x, y, w, h = [int(round(v)) for v in box]
    with tempfile.TemporaryDirectory(dir=OUT) as tmp:
        subprocess.run(['ffmpeg', '-loglevel', 'error', '-t', str(ANIM_SECONDS), '-i', video_path,
                        '-vf', f'fps={ANIM_FPS},scale={w}:{h}:force_original_aspect_ratio=increase,crop={w}:{h}',
                        os.path.join(tmp, 'f%04d.png')], check=True)
        frames = []
        scale = ANIM_WIDTH / base.width
        size = (ANIM_WIDTH, int(round(base.height * scale)))
        for f in sorted(glob.glob(os.path.join(tmp, 'f*.png'))):
            im = base.copy()
            im.paste(Image.open(f).convert('RGB'), (x, y))
            frames.append(im.resize(size, Image.LANCZOS))
    frames[0].save(out_path, save_all=True, append_images=frames[1:], duration=int(1000 / ANIM_FPS), loop=0,
                   quality=72, method=4)
    return out_path


def main():
    os.makedirs(OUT, exist_ok=True)
    for f in glob.glob(os.path.join(OUT, '*')):
        os.remove(f)
    url = 'file:///' + PAGE.replace('\\', '/')
    parts = []  # (index, name, alt, ext)
    with sync_playwright() as p:
        exe = chromium()
        browser = p.chromium.launch(headless=True, executable_path=exe) if exe else p.chromium.launch(headless=True)
        for scheme in ('light', 'dark'):
            page = browser.new_page(viewport={'width': WIDTH, 'height': 900}, device_scale_factor=SCALE, color_scheme=scheme)
            page.goto(url)
            page.wait_for_load_state('networkidle')
            page.evaluate('document.fonts.ready')
            # the README image already is the page; no autoplay/controls chrome needed in stills
            page.add_style_tag(content='video{pointer-events:none}')
            blocks = page.query_selector_all('.wrap > header, .wrap > section, .wrap > footer')
            for i, el in enumerate(blocks):
                name = el.get_attribute('id') or ('hero' if i == 0 else 'facts' if i == 1 else 'footer' if el.evaluate('e => e.tagName') == 'FOOTER' else f'part{i}')
                has_media = el.query_selector('img, video') is not None
                ext = 'jpg' if has_media else 'png'
                box = el.bounding_box()
                pad_top = 24 if i == 0 else 0
                clip = {'x': 0, 'y': max(0, box['y'] - pad_top), 'width': WIDTH, 'height': box['height'] + pad_top + (28 if name == 'footer' else 8)}
                path = os.path.join(OUT, f'{i:02d}-{name}-{scheme}.{ext}')
                kw = {'quality': 88, 'type': 'jpeg'} if ext == 'jpg' else {'type': 'png'}
                page.screenshot(path=path, clip=clip, full_page=True, **kw)
                # a section with a playing clip becomes an animated picture with the clip in its slot
                vid = el.query_selector('video[autoplay]')
                if vid:
                    vb = vid.bounding_box()
                    box = ((vb['x'] - clip['x']) * SCALE, (vb['y'] - clip['y']) * SCALE, vb['width'] * SCALE, vb['height'] * SCALE)
                    src = vid.get_attribute('src').replace('report/', '')
                    webp = path.rsplit('.', 1)[0] + '.webp'
                    animate(path, os.path.join(ROOT, 'docs', 'report', src), box, webp)
                    os.remove(path)
                    ext = 'webp'

                if scheme == 'light':
                    heading = el.query_selector('h1, h2')
                    title = heading.inner_text().strip() if heading else name.title()
                    lead = el.query_selector('p') or el.query_selector('.fact') or el.query_selector('li') or el.query_selector('figcaption')
                    text = re.sub(r'\s+', ' ', lead.inner_text()).strip() if lead else ''
                    alt = (f'{title}. {text}' if heading else text)[:300].replace('"', "'")
                    parts.append((i, name, alt, ext))
            page.close()
        browser.close()

    lines = [
        '<!-- The sections below are pictures of docs/report/index.html (the full page with playable videos),',
        '     rendered by tools/docs/render_readme.py so the README keeps its look on GitHub. -->',
        '',
    ]
    for i, name, alt, ext in parts:
        light = f'docs/readme/{i:02d}-{name}-light.{ext}'
        dark = f'docs/readme/{i:02d}-{name}-dark.{ext}'
        img = f'<img alt="{alt}" src="{light}" width="100%">'
        pic = f'<picture><source media="(prefers-color-scheme: dark)" srcset="{dark}">{img}</picture>'
        if name in ('hero', 'flight', 'clips'):
            pic = f'<a href="docs/report/media/">{pic}</a>'
        lines.append(pic)
        lines.append('')
    lines += open(os.path.join(ROOT, 'docs', 'readme_tail.md'), encoding='utf-8').read().splitlines()
    open(os.path.join(ROOT, 'README.md'), 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
    print(f'{len(parts)} sections -> docs/readme, README.md')


if __name__ == '__main__':
    main()
