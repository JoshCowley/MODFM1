# Joins src/page.html, the six script parts and presets.json into index.html.
#   python3 build.py [manual-url] [--standalone | --single]
# --single writes fm1-rack-single.html with rack.wasm and worklet.js embedded (opens from disk).
# --lite builds FM1 Rack Lite (lite.html, or fm1-rack-lite-single.html with --single): the FM-1 module set.
# --standalone adds the doctype, charset and viewport a normal web server needs.
# Without it the output is the fragment published as a claude.ai Artifact,
# which adds its own page skeleton.
import sys, re, os, tempfile
args = [a for a in sys.argv[1:] if not a.startswith("--")]
single = "--single" in sys.argv
lite = "--lite" in sys.argv   # FM1 Rack Lite: the FM-1 module set (no Elements; Warps, Clouds, Marbles optional)
standalone = "--standalone" in sys.argv or single
manual = args[0] if args else "#"
page = open('src/page.html').read()
parts = [open(f'src/{p}').read() for p in ['1-data.js', '2-screen.js', '3-vis.js', '4-pages.js', '5-controls.js', '5b-seq.js', '5c-tools.js', '5d-sequencer.js', '6-engine.js']]
import json
presets = json.load(open('presets.json'))
if lite: presets = [p for p in presets if 'ELEMENTS' not in p['slots'].values()]
app = "\n".join(parts).replace("/*PRESETS*/[]", json.dumps(presets))
if lite:
    app = app.replace("const LITE = /*LITE*/false;", "const LITE = /*LITE*/true;")
    page = page.replace("<title>FM1 Rack</title>", "<title>FM1 Rack Lite</title>").replace("<h1>FM1 Rack <span>", "<h1>FM1 Rack Lite <span>")
    page = page.replace("This is the browser build, running the original module code.</p>", "This is the browser build, running the original module code.</p>\n      <p><b>Lite</b> is the prototype of the FM-1 version: only modules that could fit the FM-1's flash. Elements is left out, and Warps, Clouds and Marbles are switched in or out on GLOBAL &gt; FIRMWARE (page 8), which shows the flash they need. It is still a browser preview, not firmware.</p>")
page = page.replace("/*APP*/", app).replace("/*MANUAL*/", manual)
if single:   # one file that opens by double-clicking: engine and worklet embedded
    import base64, json
    emb = '<script>window.__RACK_WASM=' + json.dumps(base64.b64encode(open('rack.wasm','rb').read()).decode()) + ';window.__RACK_WORKLET=' + json.dumps(open('worklet.js').read()).replace('</', '<\\/') + ';</script>\n'
    page = emb + page
if standalone:
    page = '<!doctype html>\n<html lang="en">\n<meta charset="utf-8">\n<meta name="viewport" content="width=device-width, initial-scale=1">\n' + page
out = ('fm1-rack-lite-single.html' if lite else 'fm1-rack-single.html') if single else ('lite.html' if lite else 'index.html')
open(out, 'w').write(page)
js = re.findall(r'<script>(.*?)</script>', page, re.S)[-1]
check = os.path.join(tempfile.gettempdir(), 'fm1-rack-app-check.js')
open(check, 'w').write(js)   # run `node --check` on this to catch syntax errors
print(len(page), out, "single file" if single else "standalone" if standalone else "artifact fragment")
