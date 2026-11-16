#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
# Copyright (C) 2026 Tomorrow's Man (SAMSARA)
# Build the GitHub Pages site (tools/make_pages.py) and push it to the gh-pages branch of origin, replacing
# what was there (the branch holds only the generated site).
#   tools/publish_pages.sh              the player's site: the emulator, the manual (no package)
#   tools/publish_pages.sh 1.0          with release 1.0's installer and download (after ./build.sh --release 1.0)
# The site's commit is made with this repository's own user.name and user.email.
set -e
cd "$(dirname "$0")/.."
V="$1"
PY="${PYTHON:-python3}"
if [ -n "$V" ]; then
    PKG="app/build/samsara-$V.fwsc"
    [ -f "$PKG" ] || { echo "no $PKG: run ./build.sh --release $V first"; exit 1; }
    sh build.sh --web --release "$V"
    "$PY" tools/make_pages.py app/build/pages "$PKG" "$V"
else
    sh build.sh --web
    "$PY" tools/make_pages.py app/build/pages
fi
REMOTE="$(git remote get-url origin)"
SHA="$(git rev-parse --short HEAD)"
NAME="$(git config user.name)"
MAIL="$(git config user.email)"
cd app/build/pages
rm -rf .git
git init -q -b gh-pages
git add -A
git -c user.name="$NAME" -c user.email="$MAIL" commit -q -m "Site for SAMSARA${V:+ $V} (from $SHA)"
git push -q -f "$REMOTE" gh-pages
rm -rf .git
# ask GitHub Pages to build it now: a push alone has left the first deploy stuck on "building"
REPO="$(printf %s "$REMOTE" | sed -E 's#(git@github.com:|https://github.com/)##; s#\.git$##')"
gh api -X POST "repos/$REPO/pages/builds" >/dev/null 2>&1 || echo "(could not ask GitHub Pages to build: check the repository's Pages settings)"
echo "pushed the site${V:+ for $V} to gh-pages"
