# Joins src/page.html, the six script parts and presets.json into index.html.
#   python3 build.py [manual-url] [--standalone | --single]
# --single writes fm1-rack-single.html with rack.wasm and worklet.js embedded (opens from disk).
# --standalone adds the doctype, charset and viewport a normal web server needs.
# Without it the output is the fragment published as a claude.ai Artifact,
# which adds its own page skeleton.
import sys, re, os, tempfile
args = [a for a in sys.argv[1:] if not a.startswith("--")]
single = "--single" in sys.argv
standalone = "--standalone" in sys.argv or single
manual = args[0] if args else "#"
page = open('src/page.html').read()
parts = [open(f'src/{p}').read() for p in ['1-data.js', '2-screen.js', '3-vis.js', '4-pages.js', '5-controls.js', '5b-seq.js', '5c-tools.js', '5d-sequencer.js', '6-engine.js']]
app = "\n".join(parts).replace("/*PRESETS*/[]", open('presets.json').read())
page = page.replace("/*APP*/", app).replace("/*MANUAL*/", manual)
if single:   # one file that opens by double-clicking: engine and worklet embedded
    import base64, json
    emb = '<script>window.__RACK_WASM=' + json.dumps(base64.b64encode(open('rack.wasm','rb').read()).decode()) + ';window.__RACK_WORKLET=' + json.dumps(open('worklet.js').read()).replace('</', '<\\/') + ';</script>\n'
    page = emb + page
if standalone:
    page = '<!doctype html>\n<html lang="en">\n<meta charset="utf-8">\n<meta name="viewport" content="width=device-width, initial-scale=1">\n' + page
out = 'fm1-rack-single.html' if single else 'index.html'
open(out, 'w').write(page)
js = re.findall(r'<script>(.*?)</script>', page, re.S)[-1]
check = os.path.join(tempfile.gettempdir(), 'fm1-rack-app-check.js')
open(check, 'w').write(js)   # run `node --check` on this to catch syntax errors
print(len(page), out, "single file" if single else "standalone" if standalone else "artifact fragment")
