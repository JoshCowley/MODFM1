#!/usr/bin/env python3
"""Write ui_defs.h: the page's module, AMP and LFO pages (web/src/1-data.js) as C tables for the firmware's menus.

The definitions are evaluated with Node (the same JavaScript the page runs), then written as:
  UI_OPT[]           every option name, once
  ui_param_t arrays  one per module / AMP / LFO: label, type, range, options, default, modulatable
  UI_MODS[]          the Lite firmware's modules: engine id, name, blurb, kind, pages, outputs (matrix sources)
Run by fm1.toml's gen step before each build (needs node). The Lite module set: no Elements, Warps, Marbles or Clouds.
"""
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = HERE / "../../../web/src/1-data.js"
OUT = HERE / "ui_defs.h"
LITE = ["PLAITS", "BRAIDS", "RINGS", "FILTER", "PEAKS", "TIDES", "GRIDS", "DELAY", "REVERB", "CHORUS"]
MID = {"PLAITS": 1, "BRAIDS": 2, "RINGS": 10, "FILTER": 12, "PEAKS": 20, "TIDES": 21, "GRIDS": 23,
       "DELAY": 31, "REVERB": 32, "CHORUS": 33}
KIND = {"src": 0, "shp": 1, "mod": 2, "fx": 3, "amp": 4, "lfo": 5}
TYPE = {"num": 0, "bip": 1, "list": 2, "int": 3}


def load():
    js = SRC.read_text().split("\n")
    end = next(i for i, l in enumerate(js) if l.startswith("const LFOS ="))
    body = "\n".join(js[:end + 1])
    prog = ("const matchMedia = () => ({ matches: false });\n" + body +
            "\nfor (const m of Object.values(MODULES)) if (m.kind === 'fx') m.pages.flat().forEach((p) => { p.mod = true; });" +
            "\nprocess.stdout.write(JSON.stringify({ MODULES, AMP, LFOS }));")
    r = subprocess.run(["node", "-e", prog], capture_output=True, text=True)
    if r.returncode:
        sys.exit("make_ui: node: " + r.stderr)
    return json.loads(r.stdout)


def cstr(s):
    s = s.replace(" \u00b7 ", " / ")
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    d = load()
    opts, opt_ix = [], {}

    def opt_block(names):
        key = tuple(names)
        if key not in opt_ix:
            opt_ix[key] = len(opts)
            opts.extend(names)
        return opt_ix[key]

    out = ["/* Written by make_ui.py from web/src/1-data.js: the page's module, AMP and LFO pages. Don't edit. */",
           "typedef struct { const char *l; uint8_t t, mod; int8_t lo, hi; uint16_t opt; float def; } ui_param_t;",
           "enum { UT_NUM, UT_BIP, UT_LIST, UT_INT };",
           "typedef struct { const char *name, *blurb; uint8_t id, kind, npages, nparams; const uint8_t *pages;"
           " const ui_param_t *p; uint8_t nouts; const char *outs[2]; } ui_mod_t;"]
    tables = []

    def params(tag, pages):
        flat = [p for pg in pages for p in pg]
        rows = []
        for p in flat:
            t = TYPE[p["t"]]
            lo = hi = 0
            o = 0
            if t == 2:
                o = opt_block(p["opts"])
                lo, hi = 0, len(p["opts"]) - 1
            elif t == 3:
                lo, hi = p["min"], p["max"]
            elif t == 1:
                lo, hi = -1, 1
            else:
                lo, hi = 0, 1
            mod = 0 if p.get("mod") is False else 1
            rows.append(f"    {{{cstr(p['l'])}, {t}, {mod}, {lo}, {hi}, {o}, {float(p['def'])!r}f}},")
        tables.append(f"static const ui_param_t UP_{tag}[] = {{\n" + "\n".join(rows) + "\n};")
        tables.append(f"static const uint8_t UG_{tag}[] = {{{', '.join(str(len(pg)) for pg in pages)}}};")
        return len(pages), len(flat)

    mods = []
    for name in LITE:
        m = d["MODULES"][name]
        npg, npar = params(name, m["pages"])
        outs = m.get("outs", [])
        o = ", ".join(cstr(x) for x in outs) or "0"
        mods.append(f"    {{{cstr(name)}, {cstr(m['blurb'])}, {MID[name]}, {KIND[m['kind']]}, {npg}, {npar}, UG_{name},"
                    f" UP_{name}, {len(outs)}, {{{o}}}}},")
    a_pg, a_n = params("AMP", d["AMP"]["pages"])
    l_pg, l_n = params("LFO", d["LFOS"]["pages"])
    out.append("static const char *const UI_OPT[] = {\n" +
               "\n".join("    " + ", ".join(cstr(x) for x in opts[i:i + 8]) + "," for i in range(0, len(opts), 8)) + "\n};")
    out += tables
    out.append(f"#define UI_NMODS {len(mods)}")
    out.append("static const ui_mod_t UI_MODS[UI_NMODS] = {\n" + "\n".join(mods) + "\n};")
    out.append(f"static const ui_mod_t UI_AMP = {{\"AMP\", \"ENVELOPES\", 0, 4, {a_pg}, {a_n}, UG_AMP, UP_AMP, 0, {{0}}}};")
    out.append(f"static const ui_mod_t UI_LFO = {{\"LFO\", \"THREE LFOS\", 0, 5, {l_pg}, {l_n}, UG_LFO, UP_LFO, 0, {{0}}}};")
    OUT.write_text("\n".join(out) + "\n")
    print(f"ui: {len(mods)} modules, {len(opts)} option names -> {OUT.name}")


if __name__ == "__main__":
    main()
