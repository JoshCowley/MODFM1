#!/usr/bin/env python3
"""Builds the hosted manuals (web/manual/) from the Markdown here, with pandoc:
  FM1-Rack-Manual.md              -> web/manual/index.html   (the browser build)
  FM1-Rack-FM1-Firmware-Manual.md -> web/manual/fm1.html     (the FM-1 firmware)
  python3 manual/build_html.py"""
import re, subprocess
from pathlib import Path
HERE = Path(__file__).resolve().parent
OUT = HERE.parent / "web" / "manual"
HEAD = """<!doctype html>
<html lang="en">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title>
<style>
:root{{--bg:#f4f2ec;--fg:#1d1d1b;--dim:#6b6a65;--line:#d9d6cc;--acc:#b5532a;--code:#e8e4da}}
@media (prefers-color-scheme:dark){{:root{{--bg:#141413;--fg:#ecebe6;--dim:#9b998f;--line:#2e2d2a;--acc:#f0a46e;--code:#24231f}}}}
*{{box-sizing:border-box}}body{{margin:0;background:var(--bg);color:var(--fg);font:16px/1.6 system-ui,-apple-system,"Segoe UI",sans-serif}}
main{{max-width:860px;margin:0 auto;padding:24px 16px 80px}}
a{{color:var(--acc)}}h1{{font-size:2rem;line-height:1.2}}h2{{margin-top:2.6em;padding-top:.6em;border-top:1px solid var(--line)}}h3{{margin-top:1.8em}}
table{{border-collapse:collapse;width:100%;margin:1em 0;font-size:.92rem;display:block;overflow-x:auto}}
th,td{{border:1px solid var(--line);padding:6px 8px;text-align:left;vertical-align:top}}th{{background:var(--code)}}
img{{max-width:100%;height:auto;border-radius:6px}}code,kbd{{background:var(--code);padding:1px 5px;border-radius:4px;font-size:.9em}}
pre{{background:var(--code);padding:10px 12px;border-radius:6px;overflow-x:auto}}pre code{{padding:0}}
blockquote{{margin:1em 0;padding:.4em 1em;border-left:3px solid var(--acc);background:var(--code);border-radius:4px}}
figure{{margin:1em 0}}figcaption{{color:var(--dim);font-size:.9rem}}
img[src*="fm1-"]{{width:320px;image-rendering:pixelated}}
.top{{position:sticky;top:0;background:var(--bg);border-bottom:1px solid var(--line);padding:10px 16px;font-weight:600}}
.top a{{margin-right:1.2em}}
</style>
<div class="top"><a href="../">← Back to FM1 Rack</a><a href="index.html">Browser manual</a><a href="fm1.html">FM-1 firmware manual</a></div>
<main>
"""
for src, dst in [("FM1-Rack-Manual.md", "index.html"), ("FM1-Rack-FM1-Firmware-Manual.md", "fm1.html")]:
    md = (HERE / src).read_text()
    title = re.match(r"# (.+)", md).group(1)
    html = subprocess.run(["pandoc", "-f", "gfm", "-t", "html5", "--wrap=none"], input=md, capture_output=True, text=True, check=True).stdout
    html = html.replace('src="../web/manual/img/', 'src="img/')
    (OUT / dst).write_text(HEAD.format(title=title) + html + "</main>\n</html>\n")
    print(dst, len(html))
