# Joins src/page.html, the six script parts and presets.json into index.html.
#   python3 build.py [manual-url] [--standalone]
# --standalone adds the doctype, charset and viewport a normal web server needs.
# Without it the output is the fragment published as a claude.ai Artifact,
# which adds its own page skeleton.
import sys, re, os, tempfile
args = [a for a in sys.argv[1:] if not a.startswith("--")]
standalone = "--standalone" in sys.argv
manual = args[0] if args else "#"
page = open('src/page.html').read()
parts = [open(f'src/{p}').read() for p in ['1-data.js', '2-screen.js', '3-vis.js', '4-pages.js', '5-controls.js', '5b-seq.js', '6-engine.js']]
app = "\n".join(parts).replace("/*PRESETS*/[]", open('presets.json').read())
page = page.replace("/*APP*/", app).replace("/*MANUAL*/", manual)
if standalone:
    page = '<!doctype html>\n<html lang="en">\n<meta charset="utf-8">\n<meta name="viewport" content="width=device-width, initial-scale=1">\n' + page
open('index.html', 'w').write(page)
js = re.search(r'<script>(.*)</script>', page, re.S).group(1)
check = os.path.join(tempfile.gettempdir(), 'fm1-rack-app-check.js')
open(check, 'w').write(js)   # run `node --check` on this to catch syntax errors
print(len(page), "standalone" if standalone else "artifact fragment")
