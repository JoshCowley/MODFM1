"""FUNC, the firmware's own MOD module (13 Nov 2026: a function generator after Make Noise's Maths; engine/rack.cc
run_func). Not on the browser page, so defined here: make_ui.py writes its page, make_presets.py its ranges."""
SOURCES = ["-", "M1", "M2", "M3", "M4", "VELO", "KEY TRK", "RANDOM", "AMP ENV", "A OUT 1", "A OUT 2", "B OUT 1",
           "B OUT 2", "LFO 1", "LFO 2", "LFO 3", "MOD ENV", "SEQ 1", "SEQ 2", "SEQ 3", "SEQ 4"]
TRIGS = ["OFF", "NOTE", "GATE", "1/16", "1/8", "1/4", "BAR", "OTHER EOC", "SOURCE"]
OUTS = ["CH 1", "CH 4", "SUM", "INV", "OR", "EOR", "EOC"]


def _n(l, d): return {"l": l, "t": "num", "def": d}
def _b(l, d): return {"l": l, "t": "bip", "def": d}
def _L(l, o, d, mod=False): return {"l": l, "t": "list", "opts": o, "def": d, **({} if mod else {"mod": False})}


FUNC = {"kind": "mod", "blurb": "FUNCTIONS + MIXER", "outs": ["OUT 1", "OUT 2"], "pages": [
    [_n("RISE 1", .3), _n("FALL 1", .55), _b("CURVE 1", 0), _L("CYCLE 1", ["OFF", "ON"], 0)],
    [_n("RISE 4", .55), _n("FALL 4", .55), _b("CURVE 4", 0), _L("CYCLE 4", ["OFF", "ON"], 1)],
    [_L("TRIG 1", TRIGS, 1), _L("TRIG 4", TRIGS, 0), _L("TRIG SRC", SOURCES, 13), _L("HOLD", ["AD", "AR"], 0)],
    [_L("CH 2 SRC", SOURCES, 0), _b("CH 2 AMT", 0), _L("CH 3 SRC", SOURCES, 0), _b("CH 3 AMT", 0)],
    [_b("LVL 1", 1), _b("LVL 2", 0), _b("LVL 3", 0), _b("LVL 4", 0)],
    [_L("OUT 1", OUTS, 0), _L("OUT 2", OUTS, 1)]]}
FUNC_ID = 24


def func_meta():
    """param -> (lo, hi, span), as the page's set_meta calls give the other modules"""
    out, i = {}, 0
    for p in [p for pg in FUNC["pages"] for p in pg]:
        out[i] = (0, len(p["opts"]) - 1, len(p["opts"]) - 1) if p["t"] == "list" else (-1, 1, 2) if p["t"] == "bip" else (0, 1, 1)
        i += 1
    return out
