#!/usr/bin/env python3
"""Write ui_defs.h: the page's module, AMP and LFO pages (web/src/1-data.js) as C tables for the firmware's menus.

The definitions are evaluated with Node (the same JavaScript the page runs), then written as:
  UI_OPT[]           every option name, once
  ui_param_t arrays  one per module / AMP / LFO: label, type, range, options, default, modulatable
  UI_MODS[]          the Lite firmware's modules: engine id, name, blurb, kind, pages, outputs (matrix sources)
  UI_GLO, UI_SEL, UI_ARP the GLOBAL (its first five pages, then SYSTEM with none), SEL (CHORD) and ARP pages
  TUNE_*, SCALE_*    the tunings (cents per degree) and the scales (semitones per degree)
Run by fm1.toml's gen step before each build (needs node). The Lite module set: no Elements, Warps, Marbles or Clouds.
"""
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = HERE / "../../../web/src/1-data.js"
TOOLS = HERE / "../../../web/src/5c-tools.js"
GLO_PAGES = 5                     # SETTINGS PLAYING TUNING MIDI MASTER (the page's STEP ADV, HARDWARE, FIRMWARE: not here)
OUT = HERE / "ui_defs.h"
LITE = ["PLAITS", "BRAIDS", "RINGS", "FILTER", "PEAKS", "TIDES", "GRIDS", "DELAY", "REVERB", "CHORUS"]
MID = {"PLAITS": 1, "BRAIDS": 2, "RINGS": 10, "FILTER": 12, "PEAKS": 20, "TIDES": 21, "GRIDS": 23,
       "DELAY": 31, "REVERB": 32, "CHORUS": 33}
KIND = {"src": 0, "shp": 1, "mod": 2, "fx": 3, "amp": 4, "lfo": 5}
TYPE = {"num": 0, "bip": 1, "list": 2, "int": 3}


def load():
    js = SRC.read_text().split("\n")
    end = next(i for i, l in enumerate(js) if l.startswith("const C_SCALE ="))
    body = "\n".join(js[:end + 1])
    arp = "\n".join(l for l in TOOLS.read_text().split("\n") if l.startswith(("const ARP_MODES", "const ARP_RATES", "const ARPDEF")))
    prog = ("const matchMedia = () => ({ matches: false });\n" + body + "\n" + arp +
            "\nfor (const m of Object.values(MODULES)) if (m.kind === 'fx') m.pages.flat().forEach((p) => { p.mod = true; });" +
            "\nprocess.stdout.write(JSON.stringify({ MODULES, AMP, LFOS, GLOBAL, CHORD, ARPDEF, TUNINGS, SCALES }));")
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

    out = ["/* Written by make_ui.py from web/src (1-data.js, 5c-tools.js): the page's modules, AMP, LFO, GLOBAL, SEL and ARP pages, tunings, scales; the firmware's OSC page. Don't edit. */",
           "#pragma once",
           "typedef struct { const char *l; uint8_t t, mod; int16_t lo, hi; uint16_t opt; float def; } ui_param_t;",
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
    # OSC: the firmware's own page (not on the browser page): a plain oscillator per voice mixed with the source,
    # on AMP's spare parameters 24-30 (engine/rack.cc osc_mix)
    num = lambda l, d, mod=True: {"l": l, "t": "num", "def": d, **({} if mod else {"mod": False})}
    osc = [[{"l": "WAVE", "t": "list", "opts": ["SINE", "TRI", "SAW", "SQUARE", "PULSE", "NOISE"], "def": 0, "mod": False},
            {"l": "OCTAVE", "t": "int", "min": -3, "max": 2, "def": 0, "mod": False},
            {"l": "SEMI", "t": "int", "min": -12, "max": 12, "def": 0, "mod": False},
            {"l": "FINE", "t": "bip", "def": 0}],
           [num("PW", 0.5), num("OSC LVL", 0.0), num("MAIN LVL", 1.0)]]
    o_pg, o_n = params("OSC", osc)
    g_pg, g_n = params("GLO", d["GLOBAL"]["pages"][:GLO_PAGES] + [[]])
    s_pg, s_n = params("SEL", d["CHORD"]["pages"])
    r_pg, r_n = params("ARP", d["ARPDEF"]["pages"])
    tune = d["TUNINGS"]
    tables.append("static const float TUNE_CENTS[] = {" + ", ".join(f"{c:.2f}f" for t in tune for c in t[1]) + "};")
    at = 0
    tat = []
    for t in tune:
        tat.append(at)
        at += len(t[1])
    tables.append(f"static const uint16_t TUNE_AT[] = {{{', '.join(map(str, tat))}}};")
    tables.append(f"static const uint8_t TUNE_N[] = {{{', '.join(str(len(t[1])) for t in tune)}}};")
    sc = list(d["SCALES"].values())
    tables.append("static const uint8_t SCALE_DEG[][8] = {" + ", ".join("{" + ", ".join(map(str, x)) + "}" for x in sc) + "};")
    tables.append(f"static const uint8_t SCALE_N[] = {{{', '.join(str(len(x)) for x in sc)}}};")
    tables.append(f"#define NTUNE {len(tune)}\n#define NSCALE {len(sc)}")
    out.append("static const char *const UI_OPT[] = {\n" +
               "\n".join("    " + ", ".join(cstr(x) for x in opts[i:i + 8]) + "," for i in range(0, len(opts), 8)) + "\n};")
    out += tables
    out.append(f"#define UI_NMODS {len(mods)}")
    out.append("static const ui_mod_t UI_MODS[UI_NMODS] = {\n" + "\n".join(mods) + "\n};")
    out.append(f"static const ui_mod_t UI_AMP = {{\"AMP\", \"ENVELOPES\", 0, 4, {a_pg}, {a_n}, UG_AMP, UP_AMP, 0, {{0}}}};")
    out.append(f"static const ui_mod_t UI_LFO = {{\"LFO\", \"THREE LFOS\", 0, 5, {l_pg}, {l_n}, UG_LFO, UP_LFO, 0, {{0}}}};")
    out.append(f"static const ui_mod_t UI_OSC = {{\"OSC\", \"OSC + MIXER\", 0, 0, {o_pg}, {o_n}, UG_OSC, UP_OSC, 0, {{0}}}};")
    out.append(f"static const ui_mod_t UI_GLO = {{\"GLOBAL\", \"SETTINGS\", 0, 6, {g_pg}, {g_n}, UG_GLO, UP_GLO, 0, {{0}}}};")
    out.append(f"static const ui_mod_t UI_SEL = {{\"SCALE + CHORD\", \"KEYS TO SCALE\", 0, 7, {s_pg}, {s_n}, UG_SEL, UP_SEL, 0, {{0}}}};")
    out.append(f"static const ui_mod_t UI_ARP = {{\"ARP\", \"ARPEGGIATOR\", 0, 8, {r_pg}, {r_n}, UG_ARP, UP_ARP, 0, {{0}}}};")
    OUT.write_text("\n".join(out) + "\n")
    print(f"ui: {len(mods)} modules, {len(opts)} option names -> {OUT.name}")


if __name__ == "__main__":
    main()
