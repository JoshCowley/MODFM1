#!/usr/bin/env python3
"""The factory presets (Nov 2026): written to presets.json, which build.py puts in the page and
firmware/fm1/rack/make_presets.py (through tests/record_calls.mjs) puts in the firmware.

  python3 presets_src.py            then rebuild the page and the firmware's presets as README says

Made for playing: instruments (keys, bass, leads) and pads and textures, only modules the FM-1 has, so the page
and the firmware hold the same 57. Every preset:
  - has four macros that mean something (M1 tone, M2 character, M3 space, M4 motion or shape, unless named
    otherwise below), set at the middle: a macro at 0.5 gives the designed sound, turning it moves the parameter
    either side of it (base = designed value - amount / 2);
  - plays with velocity (level, and brightness where the source has a tone control);
  - has its level set so the presets play at about the same loudness (tests/levels: measured, then LEVEL scaled).
"osc" (the firmware's OSC, AMP's parameters 24-30) is read by make_presets.py only; the page has no OSC.
"""
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
SLOTKEY = {"SRC": "SRC", "SHP": "SHP", "MOD A": "MODA", "MOD B": "MODB", "FX 1": "FX1", "FX 2": "FX2", "AMP": "AMP"}
LEVELS = HERE / "presets_levels.json"   # LEVEL per preset after measuring (written by the firmware's audition tool)


def P(name, slots, p, amp, macros=(), mods=(), lfo=None, osc=None):
    """macros: up to four (dest, amount); mods: other matrix rows [src, dest, amount(, via)]"""
    slots = {k: slots.get(k) for k in ["SRC", "SHP", "MODA", "MODB", "FX1", "FX2"]}
    p = {k: dict(v) for k, v in p.items()}
    amp = dict(amp)
    rows = []
    for i, (dst, amt) in enumerate(macros):
        sl, par = dst.split(".", 1)
        tgt = amp if sl == "AMP" else p.setdefault(SLOTKEY[sl], {})
        if par not in tgt:
            raise SystemExit(f"{name}: macro {i + 1} -> {dst} has no designed value")
        base = tgt[par] - amt / 2.0
        lo, hi = (-1.0, 1.0) if par in BIPOLAR else (0.0, 1.0)
        tgt[par] = round(min(hi, max(lo, base)), 3)
        rows.append([f"M{i + 1}", dst, amt])
    rows += [list(m) for m in mods]
    if len(rows) > 12:
        raise SystemExit(f"{name}: more than 12 matrix rows")
    out = {"name": name, "slots": slots, "p": p, "amp": amp, "matrix": rows, "macros": [0.5, 0.5, 0.5, 0.5]}
    if lfo:
        out["lfo"] = lfo
    if osc:
        out["osc"] = osc
    return out


BIPOLAR = {"FINE", "ENV>TIMB", "ENV>MRPH", "ENV>FM", "AD>TIMB", "AD>COLR", "AD>PTCH", "ENV AMT", "VEL AMT_F", "SLOPE",
           "SMOOTH", "GAIN", "WIDTH_D", "DELAY_C", "A CURVE", "D CURVE", "VEL>ATK", "KEY>TIME", "PITCH ENV"}

# a few reused pieces
PLATE = lambda mix=0.16, size=0.45: {"TYPE": "PLATE", "SIZE": size, "DAMP": 0.55, "DIFFUSE": 0.7, "MIX": mix, "LO CUT": 0.2}
HALL = lambda mix=0.28, size=0.78: {"TYPE": "HALL", "SIZE": size, "DAMP": 0.5, "DIFFUSE": 0.8, "MIX": mix, "PRE": 0.15, "LO CUT": 0.25}
SHIM = lambda mix=0.3, sh=0.35: {"TYPE": "SHIMMER", "SIZE": 0.88, "DAMP": 0.45, "DIFFUSE": 0.85, "MIX": mix, "SHIMMER": sh, "LO CUT": 0.3}
CHOR = lambda mix=0.3, depth=0.35, rate=0.25: {"MODE": "CHORUS", "RATE": rate, "DEPTH": depth, "WIDTH": 0.85, "MIX": mix}
ECHO = lambda t="1/4D", fb=0.3, mix=0.18: {"TIME": t, "FEEDBACK": fb, "TONE": 0.42, "MIX": mix, "MODE": "PING", "LO CUT": 0.3}
VIB = {"SHAPE 1": "SINE", "RATE 1": 0.52, "FADE 1": 0.55}


def presets():
    L = []
    a = L.append
    a(P("INIT", {"SRC": "PLAITS"}, {"SRC": {"MODEL": "VA VCF", "HARMO": 0.5, "TIMBRE": 0.5, "MORPH": 0.5}},
        {"LEVEL": 0.5}, [("SRC.TIMBRE", 0.6), ("SRC.MORPH", 0.6)]))

    # ------------------------------------------------------------------ KEYS
    a(P("CH ROUND EP", {"SRC": "PLAITS", "FX1": "CHORUS", "FX2": "REVERB"},
        {"SRC": {"MODEL": "2OP FM", "HARMO": 0.5, "TIMBRE": 0.22, "MORPH": 0.18, "ENV>TIMB": 0.25},
         "FX1": CHOR(0.28, 0.3, 0.3), "FX2": PLATE(0.14)},
        {"ATTACK": 0.0, "DECAY": 0.62, "SUSTAIN": 0.3, "RELEASE": 0.38, "VEL AMT": 0.75, "SPREAD": 0.3, "LEVEL": 0.6},
        [("SRC.TIMBRE", 0.3), ("SRC.MORPH", 0.3), ("FX 1.MIX", 0.5), ("FX 2.MIX", 0.3)],
        [["VELO", "SRC.TIMBRE", 0.3]]))
    a(P("CH BRIGHT EP", {"SRC": "PLAITS", "FX1": "CHORUS", "FX2": "DELAY"},
        {"SRC": {"MODEL": "2OP FM", "HARMO": 0.5, "TIMBRE": 0.38, "MORPH": 0.3, "ENV>TIMB": 0.4},
         "FX1": CHOR(0.35, 0.4, 0.45), "FX2": ECHO("1/4D", 0.25, 0.12)},
        {"ATTACK": 0.0, "DECAY": 0.55, "SUSTAIN": 0.25, "RELEASE": 0.32, "VEL AMT": 0.8, "LEVEL": 0.55},
        [("SRC.TIMBRE", 0.35), ("SRC.MORPH", 0.4), ("FX 1.MIX", 0.5), ("FX 2.MIX", 0.24)],
        [["VELO", "SRC.TIMBRE", 0.35]]))
    a(P("CH SOFT PIANO", {"SRC": "PLAITS", "FX1": "REVERB"},
        {"SRC": {"MODEL": "MODAL", "HARMO": 0.45, "TIMBRE": 0.42, "MORPH": 0.62},
         "FX1": HALL(0.18, 0.6)},
        {"ATTACK": 0.0, "DECAY": 0.7, "SUSTAIN": 0.0, "RELEASE": 0.45, "VEL AMT": 0.8, "LEVEL": 0.7},
        [("SRC.TIMBRE", 0.35), ("SRC.HARMO", 0.3), ("FX 1.MIX", 0.3), ("SRC.MORPH", 0.3)],
        [["VELO", "SRC.TIMBRE", 0.3]]))
    a(P("CH MARIMBA", {"SRC": "PLAITS", "FX1": "REVERB"},
        {"SRC": {"MODEL": "MODAL", "HARMO": 0.3, "TIMBRE": 0.55, "MORPH": 0.3},
         "FX1": PLATE(0.18, 0.5)},
        {"ATTACK": 0.0, "DECAY": 0.5, "SUSTAIN": 0.0, "RELEASE": 0.35, "VEL AMT": 0.8, "LEVEL": 0.7},
        [("SRC.TIMBRE", 0.35), ("SRC.HARMO", 0.4), ("FX 1.MIX", 0.3), ("SRC.MORPH", 0.4)],
        [["VELO", "SRC.TIMBRE", 0.25]]))
    a(P("CH VIBES", {"SRC": "PLAITS", "FX1": "CHORUS", "FX2": "REVERB"},
        {"SRC": {"MODEL": "2OP FM", "HARMO": 0.75, "TIMBRE": 0.18, "MORPH": 0.1, "ENV>TIMB": 0.2},
         "FX1": {"MODE": "VIBRATO", "RATE": 0.45, "DEPTH": 0.18, "WIDTH": 0.6, "MIX": 0.5}, "FX2": HALL(0.2, 0.6)},
        {"ATTACK": 0.0, "DECAY": 0.75, "SUSTAIN": 0.0, "RELEASE": 0.55, "VEL AMT": 0.75, "LEVEL": 0.6},
        [("SRC.TIMBRE", 0.3), ("FX 1.DEPTH", 0.3), ("FX 2.MIX", 0.3), ("AMP.DECAY", 0.4)],
        [["VELO", "SRC.TIMBRE", 0.2]]))
    a(P("CH DRAWBAR", {"SRC": "PLAITS", "FX1": "CHORUS", "FX2": "REVERB"},
        {"SRC": {"MODEL": "ADDITIVE", "HARMO": 0.35, "TIMBRE": 0.32, "MORPH": 0.45},
         "FX1": {"MODE": "VIBRATO", "RATE": 0.55, "DEPTH": 0.25, "WIDTH": 0.9, "MIX": 0.55}, "FX2": PLATE(0.12)},
        {"ATTACK": 0.01, "DECAY": 0.3, "SUSTAIN": 1.0, "RELEASE": 0.12, "VEL AMT": 0.2, "LEVEL": 0.5},
        [("SRC.TIMBRE", 0.4), ("SRC.MORPH", 0.4), ("FX 1.RATE", 0.6), ("FX 2.MIX", 0.24)]))
    a(P("CH POLY SYNTH", {"SRC": "BRAIDS", "SHP": "FILTER", "FX1": "CHORUS", "FX2": "REVERB"},
        {"SRC": {"MODEL": "CSAW", "TIMBRE": 0.3, "COLOR": 0.35, "DRIFT": 0.2},
         "SHP": {"TYPE": "LP", "CUTOFF": 0.42, "RESO": 0.2, "DRIVE": 0.15, "ENV AMT": 0.3, "KEY TRK": 0.55, "VEL AMT": 0.25,
                 "ENV SRC": "OWN", "F ATK": 0.0, "F DEC": 0.4, "F SUS": 0.35, "F REL": 0.35},
         "FX1": CHOR(0.35, 0.4), "FX2": PLATE(0.15)},
        {"ATTACK": 0.0, "DECAY": 0.5, "SUSTAIN": 0.75, "RELEASE": 0.35, "VEL AMT": 0.6, "SPREAD": 0.5, "LEVEL": 0.5},
        [("SHP.CUTOFF", 0.4), ("SHP.RESO", 0.4), ("FX 1.MIX", 0.5), ("SHP.F DEC", 0.5)]))
    a(P("CH NYLON", {"SRC": "PLAITS", "FX1": "REVERB"},
        {"SRC": {"MODEL": "STRING", "HARMO": 0.5, "TIMBRE": 0.4, "MORPH": 0.72, "LEVEL": 1.0}, "FX1": HALL(0.16, 0.55)},
        {"ATTACK": 0.0, "DECAY": 0.8, "SUSTAIN": 0.0, "RELEASE": 0.5, "VEL AMT": 0.7, "LEVEL": 0.7},
        [("SRC.TIMBRE", 0.4), ("SRC.MORPH", 0.4), ("FX 1.MIX", 0.3), ("SRC.HARMO", 0.3)],
        [["VELO", "SRC.TIMBRE", 0.25]]))
    a(P("CH HARP", {"SRC": "BRAIDS", "FX1": "DELAY", "FX2": "REVERB"},
        {"SRC": {"MODEL": "PLUK", "TIMBRE": 0.45, "COLOR": 0.55}, "FX1": ECHO("1/4D", 0.25, 0.12), "FX2": HALL(0.22, 0.7)},
        {"ATTACK": 0.0, "DECAY": 0.7, "SUSTAIN": 0.0, "RELEASE": 0.6, "VEL AMT": 0.75, "LEVEL": 0.6},
        [("SRC.TIMBRE", 0.4), ("SRC.COLOR", 0.4), ("FX 1.MIX", 0.24), ("FX 2.MIX", 0.3)],
        [["VELO", "SRC.TIMBRE", 0.2]]))
    a(P("CH CLAV", {"SRC": "PLAITS", "SHP": "FILTER", "FX1": "CHORUS"},
        {"SRC": {"MODEL": "WAVESHP", "HARMO": 0.6, "TIMBRE": 0.45, "MORPH": 0.35},
         "SHP": {"TYPE": "LP", "CUTOFF": 0.5, "RESO": 0.3, "DRIVE": 0.3, "ENV AMT": 0.3, "KEY TRK": 0.7, "VEL AMT": 0.3,
                 "ENV SRC": "OWN", "F ATK": 0.0, "F DEC": 0.22, "F SUS": 0.15, "F REL": 0.1, "GAIN": 0.4},
         "FX1": {"MODE": "FLANGER", "RATE": 0.2, "DEPTH": 0.25, "WIDTH": 0.7, "MIX": 0.2, "FEEDBACK": 0.3}},
        {"ATTACK": 0.0, "DECAY": 0.35, "SUSTAIN": 0.2, "RELEASE": 0.1, "VEL AMT": 0.8, "LEVEL": 0.55},
        [("SHP.CUTOFF", 0.4), ("SRC.TIMBRE", 0.4), ("FX 1.MIX", 0.4), ("SHP.RESO", 0.4)]))
    a(P("CH MUSIC BOX", {"SRC": "PLAITS", "FX1": "DELAY", "FX2": "REVERB"},
        {"SRC": {"MODEL": "MODAL", "HARMO": 0.62, "TIMBRE": 0.72, "MORPH": 0.25, "OCTAVE": 1},
         "FX1": ECHO("1/8D", 0.3, 0.15), "FX2": HALL(0.25, 0.7)},
        {"ATTACK": 0.0, "DECAY": 0.6, "SUSTAIN": 0.0, "RELEASE": 0.5, "VEL AMT": 0.7, "LEVEL": 0.6},
        [("SRC.TIMBRE", 0.3), ("SRC.HARMO", 0.3), ("FX 1.MIX", 0.3), ("FX 2.MIX", 0.3)]))
    a(P("CH KOTO", {"SRC": "BRAIDS", "SHP": "RINGS", "FX1": "REVERB"},
        {"SRC": {"MODEL": "PLUK", "TIMBRE": 0.35, "COLOR": 0.35},
         "SHP": {"MODEL": "STRING", "STRUCT": 0.3, "BRIGHT": 0.55, "DAMPING": 0.65, "POSITION": 0.2, "EXCITE": 0.85,
                 "MIX": 0.65, "SPREAD": 0.4},
         "FX1": PLATE(0.16, 0.5)},
        {"ATTACK": 0.0, "DECAY": 0.6, "SUSTAIN": 0.0, "RELEASE": 0.5, "VEL AMT": 0.75, "LEVEL": 0.6},
        [("SHP.BRIGHT", 0.4), ("SHP.POSITION", 0.3), ("FX 1.MIX", 0.3), ("SHP.DAMPING", 0.3)],
        [["VELO", "SHP.BRIGHT", 0.2]]))

    # ------------------------------------------------------------------ BASS
    mono = {"MODE": "LEGATO", "GLIDE": 0.12}
    a(P("BS ROUND SUB", {"SRC": "PLAITS", "SHP": "FILTER"},
        {"SRC": {"MODEL": "VA VCF", "HARMO": 0.2, "TIMBRE": 0.25, "MORPH": 0.1},
         "SHP": {"TYPE": "LP24", "CUTOFF": 0.32, "RESO": 0.15, "DRIVE": 0.25, "ENV AMT": 0.2, "KEY TRK": 0.5,
                 "ENV SRC": "OWN", "F ATK": 0.0, "F DEC": 0.3, "F SUS": 0.3, "F REL": 0.2}},
        {"ATTACK": 0.0, "DECAY": 0.4, "SUSTAIN": 0.9, "RELEASE": 0.12, "VEL AMT": 0.4, "LEVEL": 0.6, **mono},
        [("SHP.CUTOFF", 0.35), ("SHP.RESO", 0.4), ("SHP.ENV AMT", 0.4), ("AMP.GLIDE", 0.24)],
        osc={"WAVE": 0, "OCTAVE": -1, "OSC LVL": 0.45, "MAIN LVL": 0.85}))
    a(P("BS ANALOG", {"SRC": "BRAIDS", "SHP": "FILTER", "FX1": "CHORUS"},
        {"SRC": {"MODEL": "CSAW", "TIMBRE": 0.25, "COLOR": 0.3, "DRIFT": 0.15},
         "SHP": {"TYPE": "LP24", "CUTOFF": 0.3, "RESO": 0.3, "DRIVE": 0.35, "ENV AMT": 0.35, "KEY TRK": 0.5, "VEL AMT": 0.3,
                 "ENV SRC": "OWN", "F ATK": 0.0, "F DEC": 0.32, "F SUS": 0.2, "F REL": 0.2},
         "FX1": CHOR(0.12, 0.2)},
        {"ATTACK": 0.0, "DECAY": 0.45, "SUSTAIN": 0.85, "RELEASE": 0.15, "VEL AMT": 0.5, "LEVEL": 0.55, **mono},
        [("SHP.CUTOFF", 0.4), ("SHP.RESO", 0.4), ("SHP.ENV AMT", 0.4), ("SHP.F DEC", 0.4)],
        osc={"WAVE": 3, "OCTAVE": -1, "OSC LVL": 0.3}))
    a(P("BS ACID", {"SRC": "BRAIDS", "SHP": "FILTER", "FX1": "DELAY"},
        {"SRC": {"MODEL": "//-_", "TIMBRE": 0.05, "COLOR": 0.5, "DRIFT": 0.05},
         "SHP": {"TYPE": "LP", "CUTOFF": 0.3, "RESO": 0.68, "DRIVE": 0.45, "ENV AMT": 0.5, "KEY TRK": 0.5, "VEL AMT": 0.35,
                 "ENV SRC": "OWN", "F ATK": 0.0, "F DEC": 0.24, "F SUS": 0.0, "F REL": 0.18, "SAT": "ASYM"},
         "FX1": ECHO("1/8D", 0.3, 0.14)},
        {"ATTACK": 0.0, "DECAY": 0.35, "SUSTAIN": 0.8, "RELEASE": 0.1, "VEL AMT": 0.5, "LEVEL": 0.45, "MODE": "LEGATO",
         "GLIDE": 0.22},
        [("SHP.CUTOFF", 0.4), ("SHP.RESO", 0.3), ("SHP.ENV AMT", 0.4), ("SHP.F DEC", 0.4)]))
    a(P("BS PICKED", {"SRC": "PLAITS", "SHP": "FILTER"},
        {"SRC": {"MODEL": "2OP FM", "HARMO": 0.25, "TIMBRE": 0.3, "MORPH": 0.2, "ENV>TIMB": 0.3},
         "SHP": {"TYPE": "LP", "CUTOFF": 0.45, "RESO": 0.1, "DRIVE": 0.2, "ENV AMT": 0.25, "KEY TRK": 0.5, "VEL AMT": 0.35,
                 "ENV SRC": "OWN", "F ATK": 0.0, "F DEC": 0.25, "F SUS": 0.15, "F REL": 0.15}},
        {"ATTACK": 0.0, "DECAY": 0.5, "SUSTAIN": 0.35, "RELEASE": 0.15, "VEL AMT": 0.7, "LEVEL": 0.6, "MODE": "MONO"},
        [("SHP.CUTOFF", 0.35), ("SRC.TIMBRE", 0.4), ("SRC.MORPH", 0.4), ("AMP.DECAY", 0.4)],
        [["VELO", "SRC.TIMBRE", 0.25]]))
    a(P("BS FINGER", {"SRC": "PLAITS", "SHP": "FILTER", "FX1": "CHORUS"},
        {"SRC": {"MODEL": "STRING", "HARMO": 0.5, "TIMBRE": 0.25, "MORPH": 0.75, "OCTAVE": -1, "LEVEL": 1.0},
         "SHP": {"TYPE": "LP", "CUTOFF": 0.42, "RESO": 0.1, "DRIVE": 0.35, "ENV AMT": 0.1, "KEY TRK": 0.5,
                 "ENV SRC": "AMP", "GAIN": 0.5},
         "FX1": CHOR(0.1, 0.15)},
        {"ATTACK": 0.0, "DECAY": 0.65, "SUSTAIN": 0.0, "RELEASE": 0.2, "VEL AMT": 0.75, "LEVEL": 0.7, "MODE": "MONO"},
        [("SHP.CUTOFF", 0.35), ("SRC.TIMBRE", 0.4), ("SRC.MORPH", 0.3), ("SHP.DRIVE", 0.4)],
        [["VELO", "SRC.TIMBRE", 0.2]], osc={"WAVE": 0, "OCTAVE": -1, "OSC LVL": 0.3}))
    a(P("BS PULSE", {"SRC": "PLAITS", "SHP": "FILTER"},
        {"SRC": {"MODEL": "VA VCF", "HARMO": 0.3, "TIMBRE": 0.4, "MORPH": 0.55},
         "SHP": {"TYPE": "LP24", "CUTOFF": 0.36, "RESO": 0.35, "DRIVE": 0.3, "ENV AMT": 0.35, "KEY TRK": 0.5, "VEL AMT": 0.3,
                 "ENV SRC": "OWN", "F ATK": 0.0, "F DEC": 0.22, "F SUS": 0.15, "F REL": 0.15}},
        {"ATTACK": 0.0, "DECAY": 0.4, "SUSTAIN": 0.8, "RELEASE": 0.12, "VEL AMT": 0.55, "LEVEL": 0.5, **mono},
        [("SHP.CUTOFF", 0.4), ("SRC.MORPH", 0.4), ("SHP.ENV AMT", 0.4), ("SHP.RESO", 0.4)],
        osc={"WAVE": 4, "OCTAVE": -1, "PW": 0.3, "OSC LVL": 0.3}))
    a(P("BS WARM MOOG", {"SRC": "PLAITS", "SHP": "FILTER"},
        {"SRC": {"MODEL": "PAIR VA", "HARMO": 0.48, "TIMBRE": 0.35, "MORPH": 0.3},
         "SHP": {"TYPE": "LP24", "CUTOFF": 0.28, "RESO": 0.35, "DRIVE": 0.5, "ENV AMT": 0.3, "KEY TRK": 0.5, "VEL AMT": 0.3,
                 "ENV SRC": "OWN", "F ATK": 0.0, "F DEC": 0.35, "F SUS": 0.35, "F REL": 0.2, "SAT": "SOFT"}},
        {"ATTACK": 0.0, "DECAY": 0.45, "SUSTAIN": 0.85, "RELEASE": 0.15, "VEL AMT": 0.5, "LEVEL": 0.5, **mono},
        [("SHP.CUTOFF", 0.4), ("SHP.RESO", 0.4), ("SHP.DRIVE", 0.4), ("AMP.GLIDE", 0.24)]))
    a(P("BS DUB SINE", {"SRC": "PLAITS", "FX1": "DELAY"},
        {"SRC": {"MODEL": "ADDITIVE", "HARMO": 0.1, "TIMBRE": 0.12, "MORPH": 0.2}, "FX1": ECHO("1/4D", 0.35, 0.1)},
        {"ATTACK": 0.01, "DECAY": 0.5, "SUSTAIN": 0.9, "RELEASE": 0.2, "VEL AMT": 0.45, "LEVEL": 0.7, **mono},
        [("SRC.TIMBRE", 0.24), ("SRC.HARMO", 0.2), ("FX 1.MIX", 0.2), ("AMP.GLIDE", 0.24)],
        osc={"WAVE": 0, "OCTAVE": -1, "OSC LVL": 0.5, "MAIN LVL": 0.8}))
    a(P("BS SOFT KEYS", {"SRC": "PLAITS", "FX1": "CHORUS"},
        {"SRC": {"MODEL": "WAVESHP", "HARMO": 0.2, "TIMBRE": 0.22, "MORPH": 0.3, "ENV>TIMB": 0.25}, "FX1": CHOR(0.15, 0.2)},
        {"ATTACK": 0.0, "DECAY": 0.55, "SUSTAIN": 0.4, "RELEASE": 0.18, "VEL AMT": 0.7, "LEVEL": 0.6, "MODE": "MONO"},
        [("SRC.TIMBRE", 0.35), ("SRC.MORPH", 0.4), ("FX 1.MIX", 0.3), ("SRC.HARMO", 0.3)],
        [["VELO", "SRC.TIMBRE", 0.25]]))

    # ------------------------------------------------------------------ LEADS
    a(P("LD SOFT SAW", {"SRC": "BRAIDS", "SHP": "FILTER", "FX1": "DELAY", "FX2": "REVERB"},
        {"SRC": {"MODEL": "CSAW", "TIMBRE": 0.35, "COLOR": 0.4, "DRIFT": 0.15},
         "SHP": {"TYPE": "LP", "CUTOFF": 0.5, "RESO": 0.2, "DRIVE": 0.2, "ENV AMT": 0.2, "KEY TRK": 0.6, "VEL AMT": 0.3,
                 "ENV SRC": "OWN", "F ATK": 0.02, "F DEC": 0.4, "F SUS": 0.5, "F REL": 0.3},
         "FX1": ECHO("1/8D", 0.3, 0.15), "FX2": PLATE(0.14)},
        {"ATTACK": 0.01, "DECAY": 0.5, "SUSTAIN": 0.85, "RELEASE": 0.3, "VEL AMT": 0.5, "LEVEL": 0.45, "MODE": "LEGATO",
         "GLIDE": 0.15},
        [("SHP.CUTOFF", 0.4), ("SHP.RESO", 0.4), ("FX 1.MIX", 0.3), ("AMP.GLIDE", 0.3)],
        [["LFO 1", "PITCH", 0.012]], lfo=VIB))
    a(P("LD SQUARE", {"SRC": "PLAITS", "SHP": "FILTER", "FX1": "DELAY"},
        {"SRC": {"MODEL": "VA VCF", "HARMO": 0.35, "TIMBRE": 0.45, "MORPH": 0.7},
         "SHP": {"TYPE": "LP", "CUTOFF": 0.5, "RESO": 0.15, "DRIVE": 0.15, "ENV AMT": 0.15, "KEY TRK": 0.6,
                 "ENV SRC": "AMP"},
         "FX1": ECHO("1/4D", 0.3, 0.16)},
        {"ATTACK": 0.01, "DECAY": 0.5, "SUSTAIN": 0.85, "RELEASE": 0.25, "VEL AMT": 0.45, "LEVEL": 0.45, "MODE": "LEGATO",
         "GLIDE": 0.12},
        [("SHP.CUTOFF", 0.4), ("SRC.MORPH", 0.4), ("FX 1.MIX", 0.3), ("AMP.GLIDE", 0.24)],
        [["LFO 1", "PITCH", 0.012]], lfo=VIB))
    a(P("LD SYNC", {"SRC": "BRAIDS", "SHP": "FILTER", "FX1": "DELAY"},
        {"SRC": {"MODEL": "SYN/", "TIMBRE": 0.35, "COLOR": 0.4, "AD>TIMB": 0.25, "AD ATK": 0.0, "AD DEC": 0.4},
         "SHP": {"TYPE": "LP", "CUTOFF": 0.6, "RESO": 0.1, "DRIVE": 0.25, "ENV AMT": 0.0, "KEY TRK": 0.6, "ENV SRC": "AMP"},
         "FX1": ECHO("1/8D", 0.3, 0.14)},
        {"ATTACK": 0.0, "DECAY": 0.5, "SUSTAIN": 0.85, "RELEASE": 0.25, "VEL AMT": 0.45, "LEVEL": 0.4, "MODE": "LEGATO",
         "GLIDE": 0.1},
        [("SRC.TIMBRE", 0.5), ("SHP.CUTOFF", 0.4), ("FX 1.MIX", 0.3), ("SRC.AD>TIMB", 0.5)],
        [["LFO 1", "PITCH", 0.012]], lfo=VIB))
    a(P("LD FLUTE", {"SRC": "BRAIDS", "FX1": "REVERB"},
        {"SRC": {"MODEL": "FLUT", "TIMBRE": 0.45, "COLOR": 0.4}, "FX1": HALL(0.2, 0.6)},
        {"ATTACK": 0.06, "DECAY": 0.5, "SUSTAIN": 0.9, "RELEASE": 0.25, "VEL AMT": 0.6, "LEVEL": 0.6, "MODE": "LEGATO",
         "GLIDE": 0.05},
        [("SRC.TIMBRE", 0.4), ("SRC.COLOR", 0.4), ("FX 1.MIX", 0.3), ("AMP.ATTACK", 0.12)],
        [["LFO 1", "PITCH", 0.01]], lfo={"SHAPE 1": "SINE", "RATE 1": 0.5, "FADE 1": 0.7}))
    a(P("LD WHISTLE", {"SRC": "PLAITS", "FX1": "DELAY", "FX2": "REVERB"},
        {"SRC": {"MODEL": "2OP FM", "HARMO": 0.5, "TIMBRE": 0.08, "MORPH": 0.05, "OCTAVE": 1},
         "FX1": ECHO("1/4D", 0.3, 0.14), "FX2": HALL(0.2)},
        {"ATTACK": 0.04, "DECAY": 0.5, "SUSTAIN": 0.9, "RELEASE": 0.3, "VEL AMT": 0.5, "LEVEL": 0.5, "MODE": "LEGATO",
         "GLIDE": 0.12},
        [("SRC.TIMBRE", 0.16), ("AMP.GLIDE", 0.24), ("FX 2.MIX", 0.3), ("FX 1.MIX", 0.28)],
        [["LFO 1", "PITCH", 0.014]], lfo={"SHAPE 1": "SINE", "RATE 1": 0.55, "FADE 1": 0.5}))
    a(P("LD BRASS", {"SRC": "BRAIDS", "SHP": "FILTER", "FX1": "REVERB"},
        {"SRC": {"MODEL": "CSAW", "TIMBRE": 0.3, "COLOR": 0.35, "DRIFT": 0.2},
         "SHP": {"TYPE": "LP", "CUTOFF": 0.32, "RESO": 0.12, "DRIVE": 0.3, "ENV AMT": 0.4, "KEY TRK": 0.6, "VEL AMT": 0.35,
                 "ENV SRC": "OWN", "F ATK": 0.12, "F DEC": 0.4, "F SUS": 0.55, "F REL": 0.3},
         "FX1": PLATE(0.16)},
        {"ATTACK": 0.06, "DECAY": 0.5, "SUSTAIN": 0.85, "RELEASE": 0.25, "VEL AMT": 0.6, "LEVEL": 0.45, "MODE": "UNI 2",
         "DETUNE": 0.12},
        [("SHP.CUTOFF", 0.4), ("SHP.F ATK", 0.2), ("FX 1.MIX", 0.3), ("SHP.ENV AMT", 0.4)]))
    a(P("LD REED", {"SRC": "PLAITS", "SHP": "FILTER", "FX1": "REVERB"},
        {"SRC": {"MODEL": "FORMANT", "HARMO": 0.45, "TIMBRE": 0.5, "MORPH": 0.35},
         "SHP": {"TYPE": "LP", "CUTOFF": 0.55, "RESO": 0.1, "DRIVE": 0.15, "ENV AMT": 0.1, "KEY TRK": 0.6, "ENV SRC": "AMP"},
         "FX1": PLATE(0.16)},
        {"ATTACK": 0.04, "DECAY": 0.5, "SUSTAIN": 0.9, "RELEASE": 0.22, "VEL AMT": 0.55, "LEVEL": 0.5, "MODE": "LEGATO",
         "GLIDE": 0.06},
        [("SRC.TIMBRE", 0.4), ("SRC.MORPH", 0.4), ("FX 1.MIX", 0.3), ("SHP.CUTOFF", 0.4)],
        [["LFO 1", "PITCH", 0.01]], lfo={"SHAPE 1": "SINE", "RATE 1": 0.5, "FADE 1": 0.6}))
    a(P("LD BOWED", {"SRC": "BRAIDS", "SHP": "RINGS", "FX1": "REVERB"},
        {"SRC": {"MODEL": "BOWD", "TIMBRE": 0.45, "COLOR": 0.5},
         "SHP": {"MODEL": "STRING", "STRUCT": 0.25, "BRIGHT": 0.5, "DAMPING": 0.7, "POSITION": 0.3, "EXCITE": 0.5,
                 "MIX": 0.45, "SPREAD": 0.4},
         "FX1": HALL(0.22, 0.7)},
        {"ATTACK": 0.15, "DECAY": 0.5, "SUSTAIN": 0.9, "RELEASE": 0.35, "VEL AMT": 0.5, "LEVEL": 0.5, "MODE": "LEGATO",
         "GLIDE": 0.08},
        [("SHP.BRIGHT", 0.4), ("SRC.TIMBRE", 0.4), ("FX 1.MIX", 0.3), ("SHP.MIX", 0.4)],
        [["LFO 1", "PITCH", 0.01]], lfo={"SHAPE 1": "SINE", "RATE 1": 0.48, "FADE 1": 0.7}))
    a(P("LD UNISON", {"SRC": "BRAIDS", "SHP": "FILTER", "FX1": "DELAY", "FX2": "REVERB"},
        {"SRC": {"MODEL": "CSAW", "TIMBRE": 0.4, "COLOR": 0.45, "DRIFT": 0.2},
         "SHP": {"TYPE": "LP", "CUTOFF": 0.52, "RESO": 0.2, "DRIVE": 0.3, "ENV AMT": 0.2, "KEY TRK": 0.6, "VEL AMT": 0.3,
                 "ENV SRC": "OWN", "F ATK": 0.0, "F DEC": 0.45, "F SUS": 0.55, "F REL": 0.3},
         "FX1": ECHO("1/8D", 0.3, 0.14), "FX2": PLATE(0.14)},
        {"ATTACK": 0.01, "DECAY": 0.5, "SUSTAIN": 0.85, "RELEASE": 0.3, "VEL AMT": 0.45, "LEVEL": 0.4, "MODE": "UNI 4",
         "DETUNE": 0.18, "GLIDE": 0.08},
        [("SHP.CUTOFF", 0.4), ("SHP.RESO", 0.4), ("FX 1.MIX", 0.28), ("AMP.GLIDE", 0.16)]))

    # ------------------------------------------------------------------ PADS
    slow = {"ATTACK": 0.5, "DECAY": 0.6, "SUSTAIN": 0.9, "RELEASE": 0.6, "VEL AMT": 0.35, "SPREAD": 0.6}
    a(P("PD WARM STRINGS", {"SRC": "PLAITS", "SHP": "FILTER", "FX1": "CHORUS", "FX2": "REVERB"},
        {"SRC": {"MODEL": "STR MACH", "HARMO": 0.5, "TIMBRE": 0.55, "MORPH": 0.45},
         "SHP": {"TYPE": "LP", "CUTOFF": 0.55, "RESO": 0.1, "DRIVE": 0.1, "ENV AMT": 0.1, "KEY TRK": 0.6, "ENV SRC": "AMP"},
         "FX1": CHOR(0.4, 0.5, 0.25), "FX2": HALL(0.28)},
        {**slow, "ATTACK": 0.42, "LEVEL": 0.5},
        [("SHP.CUTOFF", 0.4), ("SRC.MORPH", 0.4), ("FX 2.MIX", 0.3), ("AMP.ATTACK", 0.5)],
        [["LFO 1", "SRC.TIMBRE", 0.08]], lfo={"SHAPE 1": "TRI", "RATE 1": 0.2}))
    a(P("PD ANALOG", {"SRC": "PLAITS", "SHP": "FILTER", "FX1": "CHORUS", "FX2": "REVERB"},
        {"SRC": {"MODEL": "PAIR VA", "HARMO": 0.45, "TIMBRE": 0.45, "MORPH": 0.4},
         "SHP": {"TYPE": "LP", "CUTOFF": 0.45, "RESO": 0.15, "DRIVE": 0.1, "ENV AMT": 0.15, "KEY TRK": 0.5,
                 "ENV SRC": "OWN", "F ATK": 0.5, "F DEC": 0.6, "F SUS": 0.6, "F REL": 0.6},
         "FX1": CHOR(0.4, 0.5), "FX2": HALL(0.3)},
        {**slow, "LEVEL": 0.5},
        [("SHP.CUTOFF", 0.4), ("SRC.MORPH", 0.4), ("FX 1.DEPTH", 0.4), ("AMP.ATTACK", 0.5)],
        [["LFO 1", "SRC.TIMBRE", 0.1]], lfo={"SHAPE 1": "TRI", "RATE 1": 0.15}))
    a(P("PD CHOIR", {"SRC": "PLAITS", "FX1": "CHORUS", "FX2": "REVERB"},
        {"SRC": {"MODEL": "FORMANT", "HARMO": 0.4, "TIMBRE": 0.4, "MORPH": 0.55},
         "FX1": CHOR(0.4, 0.45), "FX2": HALL(0.32, 0.85)},
        {**slow, "ATTACK": 0.45, "LEVEL": 0.55},
        [("SRC.MORPH", 0.4), ("SRC.TIMBRE", 0.4), ("FX 2.MIX", 0.3), ("AMP.ATTACK", 0.5)],
        [["LFO 1", "SRC.MORPH", 0.08], ["LFO 2", "PITCH", 0.008]],
        lfo={"SHAPE 1": "DRIFT", "RATE 1": 0.2, "SHAPE 2": "SINE", "RATE 2": 0.45, "FADE 2": 0.5}))
    a(P("PD GLASS", {"SRC": "PLAITS", "FX1": "CHORUS", "FX2": "REVERB"},
        {"SRC": {"MODEL": "WAVETBL", "HARMO": 0.3, "TIMBRE": 0.35, "MORPH": 0.4},
         "FX1": CHOR(0.3, 0.35), "FX2": SHIM(0.28, 0.25)},
        {**slow, "ATTACK": 0.35, "LEVEL": 0.5},
        [("SRC.TIMBRE", 0.4), ("SRC.MORPH", 0.4), ("FX 2.MIX", 0.3), ("FX 2.SHIMMER", 0.4)],
        [["LFO 1", "SRC.MORPH", 0.12]], lfo={"SHAPE 1": "TRI", "SYNC 1": "2 BARS"}))
    a(P("PD SOFT SINE", {"SRC": "PLAITS", "FX1": "CHORUS", "FX2": "REVERB"},
        {"SRC": {"MODEL": "ADDITIVE", "HARMO": 0.15, "TIMBRE": 0.2, "MORPH": 0.35},
         "FX1": CHOR(0.35, 0.4, 0.2), "FX2": HALL(0.3)},
        {**slow, "ATTACK": 0.55, "LEVEL": 0.6},
        [("SRC.TIMBRE", 0.3), ("SRC.HARMO", 0.3), ("FX 2.MIX", 0.3), ("AMP.ATTACK", 0.5)]))
    a(P("PD SYMPATHETIC", {"SRC": "PLAITS", "SHP": "RINGS", "FX1": "REVERB"},
        {"SRC": {"MODEL": "VA VCF", "HARMO": 0.3, "TIMBRE": 0.35, "MORPH": 0.4},
         "SHP": {"MODEL": "SYMPATH", "STRUCT": 0.3, "BRIGHT": 0.5, "DAMPING": 0.75, "POSITION": 0.4, "EXCITE": 0.4,
                 "MIX": 0.55, "SPREAD": 0.6},
         "FX1": HALL(0.3)},
        {**slow, "ATTACK": 0.4, "LEVEL": 0.5},
        [("SHP.STRUCT", 0.4), ("SHP.BRIGHT", 0.4), ("FX 1.MIX", 0.3), ("SHP.MIX", 0.4)],
        [["LFO 1", "SHP.POSITION", 0.15]], lfo={"SHAPE 1": "DRIFT", "RATE 1": 0.2}))
    a(P("PD SHIMMER", {"SRC": "PLAITS", "FX1": "CHORUS", "FX2": "REVERB"},
        {"SRC": {"MODEL": "2OP FM", "HARMO": 0.5, "TIMBRE": 0.15, "MORPH": 0.15},
         "FX1": CHOR(0.3, 0.35), "FX2": SHIM(0.35, 0.4)},
        {**slow, "ATTACK": 0.4, "LEVEL": 0.5},
        [("SRC.TIMBRE", 0.24), ("FX 2.SHIMMER", 0.4), ("FX 2.MIX", 0.3), ("AMP.ATTACK", 0.5)],
        [["LFO 1", "SRC.TIMBRE", 0.06]], lfo={"SHAPE 1": "SINE", "RATE 1": 0.12}))
    a(P("PD HAZE", {"SRC": "BRAIDS", "SHP": "FILTER", "FX1": "CHORUS", "FX2": "REVERB"},
        {"SRC": {"MODEL": "CSAW", "TIMBRE": 0.4, "COLOR": 0.5, "DRIFT": 0.3},
         "SHP": {"TYPE": "LP", "CUTOFF": 0.36, "RESO": 0.2, "DRIVE": 0.1, "ENV AMT": 0.0, "KEY TRK": 0.5, "ENV SRC": "AMP"},
         "FX1": CHOR(0.45, 0.6, 0.2), "FX2": HALL(0.35, 0.9)},
        {**slow, "ATTACK": 0.6, "RELEASE": 0.7, "LEVEL": 0.45},
        [("SHP.CUTOFF", 0.4), ("SHP.RESO", 0.4), ("FX 2.MIX", 0.3), ("AMP.ATTACK", 0.4)],
        [["LFO 1", "SHP.CUTOFF", 0.1]], lfo={"SHAPE 1": "TRI", "SYNC 1": "2 BARS"}))
    a(P("PD STRING ENS", {"SRC": "BRAIDS", "SHP": "FILTER", "FX1": "CHORUS", "FX2": "REVERB"},
        {"SRC": {"MODEL": "CSAW", "TIMBRE": 0.3, "COLOR": 0.35, "DRIFT": 0.25},
         "SHP": {"TYPE": "LP", "CUTOFF": 0.48, "RESO": 0.08, "DRIVE": 0.05, "ENV AMT": 0.1, "KEY TRK": 0.6,
                 "ENV SRC": "OWN", "F ATK": 0.4, "F DEC": 0.5, "F SUS": 0.7, "F REL": 0.5},
         "FX1": CHOR(0.5, 0.6, 0.3), "FX2": HALL(0.28)},
        {**slow, "ATTACK": 0.35, "RELEASE": 0.5, "LEVEL": 0.45},
        [("SHP.CUTOFF", 0.4), ("FX 1.DEPTH", 0.4), ("FX 2.MIX", 0.3), ("AMP.ATTACK", 0.5)],
        [["LFO 1", "PITCH", 0.006]], lfo={"SHAPE 1": "SINE", "RATE 1": 0.45, "FADE 1": 0.6}))
    a(P("PD EVOLVING", {"SRC": "PLAITS", "MODA": "TIDES", "FX1": "CHORUS", "FX2": "REVERB"},
        {"SRC": {"MODEL": "TERRAIN", "HARMO": 0.45, "TIMBRE": 0.4, "MORPH": 0.45},
         "MODA": {"MODE": "LOOP", "FREQ": 0.12, "SHAPE": 0.5, "SLOPE": 0.0, "RANGE": "LO"},
         "FX1": CHOR(0.35, 0.4), "FX2": HALL(0.32, 0.85)},
        {**slow, "ATTACK": 0.55, "RELEASE": 0.7, "LEVEL": 0.5},
        [("SRC.TIMBRE", 0.4), ("SRC.MORPH", 0.4), ("FX 2.MIX", 0.3), ("MOD A.FREQ", 0.2)],
        [["A TIDES", "SRC.TIMBRE", 0.2]]))
    a(P("PD DARK", {"SRC": "BRAIDS", "SHP": "FILTER", "FX1": "REVERB"},
        {"SRC": {"MODEL": "CSAW", "TIMBRE": 0.4, "COLOR": 0.4, "DRIFT": 0.2, "OCTAVE": -1},
         "SHP": {"TYPE": "LP24", "CUTOFF": 0.25, "RESO": 0.25, "DRIVE": 0.2, "ENV AMT": 0.0, "KEY TRK": 0.4, "ENV SRC": "AMP"},
         "FX1": HALL(0.35, 0.9)},
        {**slow, "ATTACK": 0.6, "RELEASE": 0.7, "LEVEL": 0.5},
        [("SHP.CUTOFF", 0.3), ("SHP.RESO", 0.4), ("FX 1.MIX", 0.3), ("AMP.ATTACK", 0.4)],
        [["LFO 1", "SHP.CUTOFF", 0.08]], lfo={"SHAPE 1": "SINE", "SYNC 1": "2 BARS"}))
    a(P("PD ORGAN AIR", {"SRC": "PLAITS", "FX1": "CHORUS", "FX2": "REVERB"},
        {"SRC": {"MODEL": "ADDITIVE", "HARMO": 0.3, "TIMBRE": 0.4, "MORPH": 0.5},
         "FX1": CHOR(0.4, 0.4), "FX2": HALL(0.3)},
        {**slow, "ATTACK": 0.35, "LEVEL": 0.5},
        [("SRC.TIMBRE", 0.4), ("SRC.MORPH", 0.4), ("FX 2.MIX", 0.3), ("AMP.ATTACK", 0.5)],
        osc={"WAVE": 1, "OCTAVE": -1, "OSC LVL": 0.25}))
    a(P("PD BELL PAD", {"SRC": "PLAITS", "FX1": "CHORUS", "FX2": "REVERB"},
        {"SRC": {"MODEL": "2OP FM", "HARMO": 0.65, "TIMBRE": 0.3, "MORPH": 0.25, "ENV>TIMB": 0.2},
         "FX1": CHOR(0.35, 0.4), "FX2": HALL(0.32, 0.85)},
        {**slow, "ATTACK": 0.2, "LEVEL": 0.5},
        [("SRC.TIMBRE", 0.3), ("SRC.HARMO", 0.3), ("FX 2.MIX", 0.3), ("AMP.ATTACK", 0.4)],
        [["LFO 1", "SRC.MORPH", 0.08]], lfo={"SHAPE 1": "TRI", "RATE 1": 0.15}))

    # ------------------------------------------------------------------ TEXTURES (FX)
    tex = {"ATTACK": 0.55, "DECAY": 0.6, "SUSTAIN": 0.9, "RELEASE": 0.75, "VEL AMT": 0.3, "SPREAD": 0.8}
    a(P("FX RESONATOR", {"SRC": "PLAITS", "SHP": "RINGS", "MODA": "TIDES", "FX1": "REVERB"},
        {"SRC": {"MODEL": "2OP FM", "HARMO": 0.3, "TIMBRE": 0.4, "MORPH": 0.2, "LPG": "ON", "DECAY": 0.3},
         "SHP": {"MODEL": "MODAL", "STRUCT": 0.55, "BRIGHT": 0.6, "DAMPING": 0.6, "POSITION": 0.3, "EXCITE": 0.7,
                 "MIX": 0.8, "SPREAD": 0.6},
         "MODA": {"MODE": "LOOP", "FREQ": 0.15, "SHAPE": 0.5, "SLOPE": 0.0, "RANGE": "LO"},
         "FX1": HALL(0.3, 0.85)},
        {"ATTACK": 0.0, "DECAY": 0.7, "SUSTAIN": 0.0, "RELEASE": 0.7, "VEL AMT": 0.6, "SPREAD": 0.6, "LEVEL": 0.6},
        [("SHP.STRUCT", 0.4), ("SHP.BRIGHT", 0.4), ("FX 1.MIX", 0.3), ("SHP.DAMPING", 0.3)],
        [["A TIDES", "SHP.POSITION", 0.2]]))
    a(P("FX DRONE", {"SRC": "PLAITS", "FX1": "CHORUS", "FX2": "REVERB"},
        {"SRC": {"MODEL": "CHORDS", "HARMO": 0.35, "TIMBRE": 0.3, "MORPH": 0.3, "OCTAVE": -1},
         "FX1": CHOR(0.4, 0.5, 0.15), "FX2": HALL(0.35, 0.9)},
        {**tex, "ATTACK": 0.7, "RELEASE": 0.8, "LEVEL": 0.5},
        [("SRC.TIMBRE", 0.4), ("SRC.HARMO", 0.4), ("FX 2.MIX", 0.3), ("SRC.MORPH", 0.4)],
        [["LFO 1", "SRC.MORPH", 0.12], ["LFO 2", "SRC.TIMBRE", 0.08]],
        lfo={"SHAPE 1": "DRIFT", "RATE 1": 0.12, "SHAPE 2": "SINE", "SYNC 2": "2 BARS"}))
    a(P("FX WIND", {"SRC": "PLAITS", "SHP": "FILTER", "FX1": "REVERB"},
        {"SRC": {"MODEL": "NOISE", "HARMO": 0.5, "TIMBRE": 0.6, "MORPH": 0.6},
         "SHP": {"TYPE": "BP", "CUTOFF": 0.5, "RESO": 0.55, "DRIVE": 0.0, "ENV AMT": 0.0, "KEY TRK": 0.6, "ENV SRC": "AMP"},
         "FX1": HALL(0.35, 0.9)},
        {**tex, "LEVEL": 0.6},
        [("SHP.CUTOFF", 0.4), ("SHP.RESO", 0.4), ("FX 1.MIX", 0.3), ("AMP.ATTACK", 0.4)],
        [["LFO 1", "SHP.CUTOFF", 0.15], ["LFO 2", "SHP.RESO", 0.1]],
        lfo={"SHAPE 1": "DRIFT", "RATE 1": 0.18, "SHAPE 2": "SINE", "RATE 2": 0.1}))
    a(P("FX RAIN", {"SRC": "PLAITS", "FX1": "DELAY", "FX2": "REVERB"},
        {"SRC": {"MODEL": "PARTICLE", "HARMO": 0.45, "TIMBRE": 0.4, "MORPH": 0.5},
         "FX1": ECHO("1/8D", 0.35, 0.2), "FX2": HALL(0.35, 0.85)},
        {**tex, "ATTACK": 0.3, "LEVEL": 0.6},
        [("SRC.TIMBRE", 0.4), ("SRC.MORPH", 0.4), ("FX 2.MIX", 0.3), ("FX 1.FEEDBACK", 0.3)]))
    a(P("FX OCEAN", {"SRC": "BRAIDS", "SHP": "FILTER", "MODA": "TIDES", "FX1": "REVERB"},
        {"SRC": {"MODEL": "NOIS", "TIMBRE": 0.5, "COLOR": 0.5},
         "SHP": {"TYPE": "LP", "CUTOFF": 0.45, "RESO": 0.1, "DRIVE": 0.0, "ENV AMT": 0.0, "KEY TRK": 0.3, "ENV SRC": "AMP",
                 "GAIN": 0.4},
         "MODA": {"MODE": "LOOP", "FREQ": 0.08, "SHAPE": 0.6, "SLOPE": 0.2, "RANGE": "LO"},
         "FX1": HALL(0.35, 0.9)},
        {**tex, "LEVEL": 0.6},
        [("SHP.CUTOFF", 0.3), ("MOD A.FREQ", 0.2), ("FX 1.MIX", 0.3), ("SHP.RESO", 0.3)],
        [["A TIDES", "SHP.CUTOFF", 0.3], ["A TIDES", "AMP.LEVEL", 0.2]]))
    a(P("FX STRING BLOOM", {"SRC": "PLAITS", "SHP": "RINGS", "FX1": "REVERB"},
        {"SRC": {"MODEL": "VA VCF", "HARMO": 0.3, "TIMBRE": 0.3, "MORPH": 0.4},
         "SHP": {"MODEL": "STRING", "STRUCT": 0.4, "BRIGHT": 0.45, "DAMPING": 0.8, "POSITION": 0.35, "EXCITE": 0.45,
                 "MIX": 0.6, "SPREAD": 0.7},
         "FX1": SHIM(0.32, 0.25)},
        {**tex, "ATTACK": 0.45, "LEVEL": 0.5},
        [("SHP.BRIGHT", 0.4), ("SHP.STRUCT", 0.4), ("FX 1.MIX", 0.3), ("FX 1.SHIMMER", 0.4)],
        [["LFO 1", "SHP.POSITION", 0.15]], lfo={"SHAPE 1": "DRIFT", "RATE 1": 0.15}))
    a(P("FX CHIMES", {"SRC": "PLAITS", "FX1": "DELAY", "FX2": "REVERB"},
        {"SRC": {"MODEL": "MODAL", "HARMO": 0.7, "TIMBRE": 0.65, "MORPH": 0.2, "OCTAVE": 1},
         "FX1": ECHO("1/4D", 0.4, 0.22), "FX2": HALL(0.35, 0.9)},
        {"ATTACK": 0.0, "DECAY": 0.75, "SUSTAIN": 0.0, "RELEASE": 0.7, "VEL AMT": 0.7, "SPREAD": 0.8, "LEVEL": 0.55},
        [("SRC.TIMBRE", 0.3), ("SRC.HARMO", 0.3), ("FX 1.FEEDBACK", 0.3), ("FX 2.MIX", 0.3)],
        [["RANDOM", "SRC.HARMO", 0.1]]))
    a(P("FX ECHO KEYS", {"SRC": "PLAITS", "FX1": "DELAY", "FX2": "REVERB"},
        {"SRC": {"MODEL": "2OP FM", "HARMO": 0.5, "TIMBRE": 0.3, "MORPH": 0.2, "ENV>TIMB": 0.3},
         "FX1": {**ECHO("1/4D", 0.5, 0.3), "WOW": 0.25, "MODE": "TAPE"}, "FX2": HALL(0.3, 0.85)},
        {"ATTACK": 0.0, "DECAY": 0.6, "SUSTAIN": 0.2, "RELEASE": 0.5, "VEL AMT": 0.7, "SPREAD": 0.5, "LEVEL": 0.55},
        [("SRC.TIMBRE", 0.3), ("FX 1.FEEDBACK", 0.4), ("FX 1.MIX", 0.3), ("FX 1.WOW", 0.4)]))
    a(P("FX CHORD DRIFT", {"SRC": "PLAITS", "SHP": "FILTER", "FX1": "CHORUS", "FX2": "REVERB"},
        {"SRC": {"MODEL": "CHORDS", "HARMO": 0.55, "TIMBRE": 0.4, "MORPH": 0.4},
         "SHP": {"TYPE": "LP", "CUTOFF": 0.45, "RESO": 0.15, "DRIVE": 0.05, "ENV AMT": 0.0, "KEY TRK": 0.5, "ENV SRC": "AMP"},
         "FX1": CHOR(0.4, 0.5), "FX2": HALL(0.32)},
        {**tex, "ATTACK": 0.4, "LEVEL": 0.5},
        [("SRC.HARMO", 0.4), ("SHP.CUTOFF", 0.4), ("FX 2.MIX", 0.3), ("SRC.MORPH", 0.4)],
        [["LFO 1", "SRC.MORPH", 0.15], ["LFO 2", "SHP.CUTOFF", 0.1]],
        lfo={"SHAPE 1": "TRI", "SYNC 1": "2 BARS", "SHAPE 2": "DRIFT", "RATE 2": 0.15}))
    a(P("FX TIDAL WASH", {"SRC": "BRAIDS", "SHP": "FILTER", "MODA": "TIDES", "FX1": "CHORUS", "FX2": "REVERB"},
        {"SRC": {"MODEL": "CSAW", "TIMBRE": 0.4, "COLOR": 0.5, "DRIFT": 0.3},
         "SHP": {"TYPE": "LP", "CUTOFF": 0.35, "RESO": 0.3, "DRIVE": 0.1, "ENV AMT": 0.0, "KEY TRK": 0.5, "ENV SRC": "AMP"},
         "MODA": {"MODE": "LOOP", "FREQ": 0.15, "SHAPE": 0.5, "SLOPE": 0.0, "RANGE": "LO"},
         "FX1": CHOR(0.4, 0.5), "FX2": HALL(0.35, 0.9)},
        {**tex, "LEVEL": 0.45},
        [("SHP.CUTOFF", 0.3), ("SHP.RESO", 0.4), ("FX 2.MIX", 0.3), ("MOD A.FREQ", 0.2)],
        [["A TIDES", "SHP.CUTOFF", 0.25]]))
    a(P("FX SINGING BOWL", {"SRC": "PLAITS", "SHP": "RINGS", "FX1": "REVERB"},
        {"SRC": {"MODEL": "MODAL", "HARMO": 0.5, "TIMBRE": 0.35, "MORPH": 0.7},
         "SHP": {"MODEL": "MODAL", "STRUCT": 0.7, "BRIGHT": 0.45, "DAMPING": 0.85, "POSITION": 0.25, "EXCITE": 0.5,
                 "MIX": 0.75, "SPREAD": 0.7},
         "FX1": HALL(0.32, 0.9)},
        {"ATTACK": 0.0, "DECAY": 0.85, "SUSTAIN": 0.0, "RELEASE": 0.8, "VEL AMT": 0.6, "SPREAD": 0.7, "LEVEL": 0.6},
        [("SHP.STRUCT", 0.3), ("SHP.BRIGHT", 0.4), ("FX 1.MIX", 0.3), ("SHP.DAMPING", 0.2)],
        [["LFO 1", "SHP.POSITION", 0.1]], lfo={"SHAPE 1": "SINE", "RATE 1": 0.1}))
    a(P("FX DEEP DRONE", {"SRC": "BRAIDS", "SHP": "FILTER", "FX1": "CHORUS", "FX2": "REVERB"},
        {"SRC": {"MODEL": "SUB-", "TIMBRE": 0.4, "COLOR": 0.4, "DRIFT": 0.3, "OCTAVE": -1},
         "SHP": {"TYPE": "LP24", "CUTOFF": 0.3, "RESO": 0.3, "DRIVE": 0.3, "ENV AMT": 0.0, "KEY TRK": 0.3, "ENV SRC": "AMP"},
         "FX1": CHOR(0.3, 0.4, 0.12), "FX2": HALL(0.35, 0.9)},
        {**tex, "ATTACK": 0.7, "RELEASE": 0.8, "LEVEL": 0.5},
        [("SHP.CUTOFF", 0.3), ("SHP.RESO", 0.4), ("FX 2.MIX", 0.3), ("SRC.TIMBRE", 0.4)],
        [["LFO 1", "SHP.CUTOFF", 0.1]], lfo={"SHAPE 1": "SINE", "SYNC 1": "2 BARS"}))
    a(P("FX GLASS RAIN", {"SRC": "PLAITS", "MODA": "PEAKS", "FX1": "DELAY", "FX2": "REVERB"},
        {"SRC": {"MODEL": "WAVETBL", "HARMO": 0.5, "TIMBRE": 0.55, "MORPH": 0.5, "LPG": "ON", "DECAY": 0.55, "OCTAVE": 1},
         "MODA": {"FUNC": "LFO", "K1": 0.3, "K2": 0.5},
         "FX1": ECHO("1/8D", 0.45, 0.25), "FX2": SHIM(0.3, 0.3)},
        {"ATTACK": 0.0, "DECAY": 0.6, "SUSTAIN": 0.0, "RELEASE": 0.6, "VEL AMT": 0.6, "SPREAD": 0.8, "LEVEL": 0.55},
        [("SRC.MORPH", 0.4), ("SRC.TIMBRE", 0.3), ("FX 1.FEEDBACK", 0.3), ("FX 2.MIX", 0.3)],
        [["A PEAKS", "SRC.MORPH", 0.15]]))
    return L


if __name__ == "__main__":
    L = presets()
    if LEVELS.exists():
        lv = json.loads(LEVELS.read_text())
        for x in L:
            if x["name"] in lv:
                x["amp"]["LEVEL"] = lv[x["name"]]
    L = L[1:] + L[:1]   # the page opens on the first: a playable one; INIT last (the firmware puts it first)
    names = [x["name"] for x in L]
    assert len(names) == len(set(names)), "a name twice"
    assert all(len(n) <= 16 for n in names), [n for n in names if len(n) > 16]
    (HERE / "presets.json").write_text(json.dumps(L, indent=0))
    print(len(L), "presets")
