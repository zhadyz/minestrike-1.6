#!/bin/bash
# Publish MineStrike 1.6 to GitHub as a fresh public repository (a single commit, no local history).
# Local history and files stay as they are; the derived game files stay in your game copies.
set -euo pipefail
cd "$(dirname "$0")/.."
REPO=zhadyz/minestrike-1.6

# 1. rebuild the GitHub Pages copy and the README pictures from the report (picks up the footer line)
python tools/docs/build_pages.py
python tools/docs/render_readme.py

# 2. keep files derived from Valve's, the SDHLT binaries and the internal brief out of the repository
#    (tools/setup/gen_gamedata.py makes the derived files from each user's own install)
git rm -q --ignore-unmatch gamedata/cstrike/delta.lst gamedata/cstrike/listenserver.cfg \
	gamedata/cstrike/maps/de_dust2_mc.bsp gamedata/cstrike/maps/de_dust2_mc.nav gamedata/cstrike/overviews/de_dust2_mc.bmp
git rm -rq --cached --ignore-unmatch tools/sdhlt docs/VOXELIZER_BRIEF.md
grep -qx '/tools/sdhlt/' .gitignore || echo '/tools/sdhlt/' >> .gitignore
grep -qx '/docs/VOXELIZER_BRIEF.md' .gitignore || echo '/docs/VOXELIZER_BRIEF.md' >> .gitignore

# 3. licence (GPL-3.0, as ReGameDLL_CS requires for the server library)
gh api licenses/gpl-3.0 --jq .body > LICENSE

# 4. nothing key-shaped may go out (prints only file names)
if git ls-files -co --exclude-standard -z | xargs -0 grep -lIE 'sk_(live|test)_|rk_live_|whsec_|GOCSPX-|AKIA[0-9A-Z]{12}|xox[baprs]-|ghp_[A-Za-z0-9]{20}|github_pat_'; then
	echo "key-shaped strings in the files above: not publishing"
	exit 1
fi
git add -A
git commit -qm "Prepare the public release: setup-generated game files, GPL-3.0 licence" || true

# 5. a fresh one-commit history for the public repository
BRANCH=$(git rev-parse --abbrev-ref HEAD)
git checkout -q --orphan public-release
git commit -qm "MineStrike 1.6: Minecraft built into Counter-Strike 1.6"
gh repo create "$REPO" --public --description "Minecraft built into Counter-Strike 1.6: dig the real de_dust2, redstone, elytra, crossbows"
gh auth setup-git
git remote remove github 2>/dev/null || true
git remote add github "https://github.com/$REPO.git"
git push github public-release:main
git checkout -q "$BRANCH"
git branch -D public-release

# 6. GitHub Pages from docs/ (the full report with playable videos)
gh api -X POST "repos/$REPO/pages" -f "source[branch]=main" -f "source[path]=/docs" > /dev/null &&
	echo "Pages: https://zhadyz.github.io/minestrike-1.6/ (live in a minute or two)"
echo "Published: https://github.com/$REPO"
