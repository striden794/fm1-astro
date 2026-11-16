#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
# Copyright (C) 2026 Tomorrow's Man (SAMSARA)
"""Make the SAMSARA site for GitHub Pages (https://tomorrowsman.github.io/fm1-samsara/), from AMB-1's:

  index.html                  what SAMSARA is, and the ways in: listen, manual, source (and, with a release
                              package, install and download)
  img/                        screenshots (docs/img)
  emu/                        SAMSARA in the browser (app/build/emu, from ./build.sh --web)
  words/                      the screen reader's words files (app/web/words, from app/words.tsv)
  manual/index.html, img/     the manual (fm1-base's tools/manual_html.py, from docs/MANUAL.md)
 with a release package only:
  install/index.html          fm1-base's web installer (base/web/installer.html, with fm1pkg.js, fm1ota.js and
                              the package's metadata inlined; Chrome or Edge, Web MIDI)
  firmware/samsara-VERSION.fwsc   the package the installer writes; also the download

  tools/make_pages.py OUT_DIR                                          the player's site, no package
  tools/make_pages.py OUT_DIR app/build/samsara-1.0.fwsc 1.0           with the installer and the download

A package must be a release build (./build.sh --release X.Y): its identity, FM-1_108XXYYZZ, is what
the installer checks the download against and what the FM-1 reports after the install."""
import hashlib
import html
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
BASE = SRC / "base"
sys.path.insert(0, str(BASE / "tools"))
from make_site import MBOOT_VERSION, product_of, read_conf, strip_module  # noqa: E402  (fm1-base's: the package format)

NAME = "SAMSARA"

LANDING = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>SAMSARA · An ambient piece for the FM-1</title>
<meta name="description" content="__TAGLINE__. A fork of AMB-1 for the M-VAVE FM-1; listen in the browser.">
<meta name="theme-color" content="#0c1a4a">
<style>
/* the firmware's own colours (app/ui.c): the lapis blue and the gold of Tibetan painting */
:root { --lapis: #0c1a4a; --panel: #162a68; --line: #324a8a; --dim: #a9b7dc; --gold: #f4da92; --acc: #eab43c;
        --sky: #a8c8f2; color-scheme: dark; }
body { background: var(--lapis); color: var(--gold); margin: 0; padding-inline: 16px; padding-block: 48px 64px;
       font: 400 17px/1.6 "Helvetica Neue", Helvetica, Arial, sans-serif; }
main { max-width: 42rem; margin: 0 auto; }
h1 { font-size: 3rem; font-weight: 300; line-height: 1; letter-spacing: .12em; margin: 0 0 .3rem; color: var(--acc); }
.sub { text-transform: uppercase; letter-spacing: .08em; font-size: .9rem; margin: 0 0 .5rem; }
.for { font-style: italic; color: var(--dim); margin: 0 0 1.2rem; padding-bottom: .8rem; border-bottom: 1px solid var(--line); }
h2 { font-size: 1.1rem; font-weight: 400; text-transform: uppercase; letter-spacing: .06em; margin: 2.4rem 0 .6rem;
     padding-top: .6rem; border-top: 1px solid var(--line); color: var(--acc); }
.lede { font-size: 1.15rem; margin: 0 0 1.6rem; max-width: 36rem; }
.shots { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 12px; margin: 0 0 1.8rem; }
.shots img { width: 100%; height: auto; image-rendering: pixelated; border: 1px solid var(--line); }
.ways { display: grid; gap: 10px; margin: 0 0 1rem; }
.ways a { display: block; text-decoration: none; color: var(--gold); background: var(--panel); border: 1px solid var(--line); padding: 12px 16px; }
.ways a:first-child { border-color: var(--acc); }
.ways a:hover, .ways a:focus-visible { background: var(--line); }
.ways a:focus-visible { outline: 3px solid var(--acc); outline-offset: 2px; }
a:focus-visible { outline: 2px solid var(--acc); outline-offset: 2px; }
.ways strong { font-weight: 400; font-size: 1.15rem; text-transform: uppercase; letter-spacing: .05em; display: block; color: var(--acc); }
.ways span { color: var(--dim); font-size: .95rem; }
.ways em.v { font-style: normal; text-transform: none; letter-spacing: 0; }
ul.garden { padding-left: 1.2rem; margin: 0 0 1rem; }
ul.garden li { margin: 0 0 .4rem; }
ul.garden li::marker { color: var(--acc); }
p.small { color: var(--dim); font-size: .9rem; }
a { color: var(--gold); text-decoration-color: var(--acc); text-decoration-thickness: 2px; text-underline-offset: 3px; }
code { font-size: .9em; }
</style>
</head>
<body>
<main>
<h1>SAMSARA</h1>
<p class="sub">An ambient piece for the M-VAVE FM-1</p>
<p class="for">For Brett.</p>
<p class="lede">SAMSARA turns the FM-1 into a slow ambient piece that plays itself and changes over hours, in the
spirit of Brian Eno's <i>Reflection</i>. Tone, harmony and density drift; every so often the whole piece ebbs
nearly to silence and returns. It never repeats and never stands still, unless you turn Drift fully down, which
holds the moment you are in.</p>
<div class="shots">
  <img src="img/screen-tide.png" width="240" height="240" alt="SAMSARA's screen in lapis blue and gold: the wheel with the scale and the chord, the bell rings around it">
  <img src="img/screen-maranasati.png" width="240" height="240" alt="SAMSARA's screen: the piece Maranasati">
  <img src="img/screen-hold.png" width="240" height="240" alt="SAMSARA's screen: Drift fully down, the moment held">
</div>
<nav class="ways" aria-label="Get SAMSARA">
  <a href="emu/"><strong>Listen in the browser</strong><span>No FM-1 needed: it runs the same code as the instrument. Press the page once to start the sound, then leave it playing, or turn the knobs with a mouse, a touch screen or the keyboard.</span></a>
__INSTALL__  <a href="manual/"><strong>Manual</strong><span>Get started in a few minutes, then read about everything else.</span></a>
__DOWNLOAD__  <a href="__REPO__"><strong>Source</strong><span>On GitHub under MPL-2.0. A fork of AMB-1 by Charles Vestal, built on fm1-base and felucca-bsp, which grew out of Felucca by Hügelton Instruments.</span></a>
</nav>

<h2>The piece</h2>
<ul class="garden">
  <li>A breathing <b>pad</b>, soft <b>bells</b>, a gliding <b>bass</b> and a rare low <b>ground</b> come and go like weather, through a tape echo and a wandering plate.</li>
  <li>Four knobs: <b>Mood</b> (dark to bright), <b>Density</b>, <b>Drift</b> (the pace of all change; fully down, it holds) and <b>Space</b> (near to far).</li>
  <li>Four pieces: <b>Samsara</b>, <b>Maranasati</b>, <b>Anatta</b> and <b>Nirvana</b>. The keys are the scale: each key adds its note or takes it out.</li>
</ul>
<p>SAMSARA is a fork of <a href="https://github.com/charlesvestal/fm1-amb">AMB-1</a> by Charles Vestal, by
Tomorrow's Man. The sounds, the pages and the whole platform are AMB-1's; SAMSARA's own part is the conductor over
them: the weather, the tide, the wandering harmony and tone, and the wheel on the screen.</p>
<p class="small">Installing a firmware on an FM-1 is at your own risk. To go back to the official firmware, use
M-VAVE's own updater, M-UPGRADE, from <a href="https://www.m-vave.com/download">m-vave.com/download</a>.
M-VAVE and FM-1 are trademarks of their owners. SAMSARA is not affiliated with M-VAVE or Brian Eno.</p>
</main>
</body>
</html>
"""

WAY_INSTALL = """  <a href="install/"><strong>Install <em class="v">v__VERSION__</em></strong><span>Connect your FM-1 by USB and install from Chrome or Edge. There's nothing to install on your computer.</span></a>
"""
WAY_DOWNLOAD = """  <a href="firmware/__PKG__"><strong>Download <em class="v">__PKG__</em></strong><span>For the command-line installer: <code>python3 base/tools/fm1_install.py __PKG__</code></span></a>
"""


def installer(pkg, version, out, conf, repo):
    """install/ and firmware/: fm1-base's installer page around a release package; the package's file name"""
    raw = pkg.read_bytes()
    product = product_of(raw)
    prefix = conf.get("id_prefix", "FM-1_108")
    if not re.fullmatch(re.escape(prefix) + r"\d{6}", product):
        raise SystemExit(f"{pkg}: identity {product!r}: make a release build (./build.sh --release X.Y)")
    if b"FELUCCA-LOADER-1" not in raw:
        raise SystemExit(f"{pkg}: no update loader in it")
    name = f"samsara-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    for d in ("install", "firmware"):
        (out / d).mkdir(parents=True)
    shutil.copy(pkg, out / "firmware" / name)
    page = (BASE / "web" / "installer.html").read_text(encoding="utf-8")
    lib = strip_module((BASE / "web" / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((BASE / "web" / "fm1ota.js").read_text(encoding="utf-8"))
    # m-bootloader (MIT), which the installer offers to install after SAMSARA, with its licence (as fm1-base's make_site)
    boot = BASE / "web" / "m-bootloader" / f"fm1-uboot-{MBOOT_VERSION}.fwsc"
    boot_name = f"m-bootloader-{MBOOT_VERSION}.fwsc"
    shutil.copy(boot, out / "firmware" / boot_name)
    shutil.copy(BASE / "web" / "m-bootloader" / "LICENSE", out / "firmware" / "m-bootloader-LICENSE.txt")
    meta = json.dumps({"version": version, "product": product, "pkg": "../firmware/" + name,
                       "backup": bool(conf.get("backup")),
                       "boot": {"pkg": "../firmware/" + boot_name, "version": MBOOT_VERSION,
                                "sha256": hashlib.sha256(boot.read_bytes()).hexdigest()}})
    for mark in ("/*LIB*/", "/*META*/", "/*APP*/"):
        if page.count(mark) != 1:
            raise SystemExit(f"base/web/installer.html must contain {mark} once")
    for key, val in (("NAME", NAME), ("REPO", repo), ("REPO_SHORT", re.sub(r"^https?://", "", repo)),
                     ("LICENSE", conf.get("licence", "MPL-2.0")), ("CREDITS", " " + conf.get("credits", ""))):
        page = page.replace("{{" + key + "}}", val)
    # the firmware's own steps around an install (fm1.toml install_app; base/web/installer.html APP): none here
    app_js = (SRC / "app" / conf["install_app"]).read_text(encoding="utf-8") if conf.get("install_app") else "null"
    (out / "install" / "index.html").write_text(page.replace("/*LIB*/", lib).replace("/*META*/", meta)
                                                .replace("/*APP*/", app_js), encoding="utf-8")
    return name, product, len(raw)


def main(out, pkg=None, version=None):
    out = Path(out)
    conf = read_conf(SRC / "app" / "fm1.toml")
    repo = conf["repo"]
    if out.exists():
        shutil.rmtree(out)
    (out / "manual").mkdir(parents=True)
    install = download = ""
    said = "no package"
    if pkg:
        name, product, size = installer(Path(pkg), version, out, conf, repo)
        install = WAY_INSTALL.replace("__VERSION__", html.escape(version))
        download = WAY_DOWNLOAD.replace("__PKG__", name)
        said = f"install/ ({product}), firmware/{name} ({size} B)"
    # the manual (a complete page; its images beside it)
    subprocess.run([sys.executable, str(BASE / "tools" / "manual_html.py"), str(SRC / "docs" / "MANUAL.md"),
                    str(out / "manual" / "index.html"), "--name", NAME, "--repo", repo,
                    "--tagline", conf.get("tagline", "")], check=True, capture_output=True)
    shutil.copytree(SRC / "docs" / "img", out / "manual" / "img")
    # the landing page and its screenshots
    shutil.copytree(SRC / "docs" / "img", out / "img")
    (out / "index.html").write_text(LANDING.replace("__INSTALL__", install).replace("__DOWNLOAD__", download)
                                    .replace("__TAGLINE__", html.escape(conf.get("tagline", ""), quote=True))
                                    .replace("__REPO__", repo), encoding="utf-8")
    emu = SRC / "app" / "build" / "emu"
    if not (emu / "app.wasm").exists():
        raise SystemExit("no app/build/emu/app.wasm: run ./build.sh --web (needs Emscripten)")
    shutil.copytree(emu, out / "emu", ignore=shutil.ignore_patterns("o2", "web", "*.o"))   # (not the build's leftovers)
    # the screen reader's words files (fm1-reader): every one ever made, for the devices that point at them
    shutil.copytree(SRC / "app" / "web" / "words", out / "words")
    (out / ".nojekyll").write_text("")
    print(f"site: {out}: index.html, manual/, emu/, words/, {said}")


if __name__ == "__main__":
    if len(sys.argv) not in (2, 4):
        sys.exit(__doc__)
    main(*sys.argv[1:])
