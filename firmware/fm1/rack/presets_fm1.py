"""The FM-1's factory presets (14 Nov 2026): the page's set (web/presets_src.py) fitted to the FM-1's CPU, with FUNC,
LFO targets and ten more. fm1_presets.py turns them into engine calls; the page keeps its own set.

What changed from the page's, and why (CPU: % of the block's time on the busier core, two cores, estimated from the
13 Nov 2026 benchmark; the aim is 80 % or less, as the UI and the run-to-run spread need the rest):
  - The heavy sources keep their voices down: a Plaits voice costs 12-33 points, a Braids one 6-9, so a preset holding
    5-6 Plaits voices with a chorus and a reverb ran 93-109 %. Pads and keys now play 4-5 (6 where the source is cheap).
  - Rings presets use Rings' own reverb (STR+VERB: about 8 points over STRING) and no REVERB module (28 points, on the
    same core as Rings): DAMPING then sets the strings' decay and the reverb's size together.
  - Plaits ADDITIVE (29 points a voice) stays only where nothing cheaper sounds like it: PD SOFT SINE is now WAVESHP
    (its first 16 harmonics within 1.7 dB of the old on average), PD ANALOG plays Plaits VA VCF without the FILTER
    (within 1.2 dB), its slow filter swell now made by FUNC.
  - The CHORDS presets (four notes a voice) and FX RAIN play 3 voices; the delay + reverb presets what fits in RAM.
  - New: FUNC (the Maths-like slopes and mixer) in PD ANALOG, PD EVOLVING and six of the ten new presets; LFO targets
    (the LFO page's own routes) for tremolo, auto-pan and slow movement.
"""
import json
import math
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
WEB = HERE.parent.parent.parent / "web"
LEVELS = HERE / "presets_fm1_levels.json"   # AMP LEVEL per preset, measured on the FM-1's path (level_fm1.py)
sys.path.insert(0, str(WEB))
import presets_src as S                     # noqa: E402
from presets_src import PLATE, HALL, SHIM, CHOR, ECHO   # noqa: E402

S.SLOTKEY["LFO"] = "LFO"


def P(name, slots, p, amp, macros=(), mods=(), lfo=None, osc=None, fm1_voices=None):
    """presets_src.P, with macros on the LFO page's parameters too ("LFO.RATE 2": its base set in lfo)"""
    p = dict(p)
    if lfo:
        p["LFO"] = dict(lfo)
    out = S.P(name, slots, p, amp, macros, mods, None, osc, fm1_voices)
    if "LFO" in out["p"]:
        out["lfo"] = out["p"].pop("LFO")
    return out


S.BIPOLAR |= {"CURVE 1", "CURVE 4", "CH 2 AMT", "CH 3 AMT", "LVL 1", "LVL 2", "LVL 3", "LVL 4"}


def secs(t):
    """FUNC's RISE / FALL for a time in seconds (run_func: 0.001 s x 20000^p)"""
    return round(math.log(t / 0.001) / math.log(20000.0), 3)


def FUNC(**kw):
    """FUNC's parameters as its page names them (func_def.py), RISE / FALL in seconds. CURVE shapes the slope's
    position x (func_shape): + is x^k, so a fall drops fast then slowly (a pluck); - holds up, then drops"""
    out = {}
    for k, v in kw.items():
        name = k.replace("_", " ")
        out[name] = secs(v) if name.startswith(("RISE", "FALL")) else v
    return out


slow = {"ATTACK": 0.5, "DECAY": 0.6, "SUSTAIN": 0.9, "RELEASE": 0.6, "VEL AMT": 0.35, "SPREAD": 0.6}
tex = {"ATTACK": 0.55, "DECAY": 0.6, "SUSTAIN": 0.9, "RELEASE": 0.75, "VEL AMT": 0.3, "SPREAD": 0.8}


def changed():
    """the page's presets the FM-1 plays differently: name -> a whole new definition (P), or a dict of changes"""
    C = {}
    # ------------------------------------------------------------------ KEYS
    C["CH ROUND EP"] = {"fm1_voices": 5}
    C["CH BRIGHT EP"] = {"lfo": {"SHAPE 3": "SINE", "SYNC 3": "1/4"}, "lfo_tgt": [None, None, None, None, ("PAN", 0.3)]}
    C["CH SOFT PIANO"] = {"fm1_voices": 5}
    C["CH NYLON"] = {"fm1_voices": 5}
    C["CH MUSIC BOX"] = {"fm1_voices": 4}
    # vibes: the motor's tremolo (level, an LFO target) added; 5 voices
    C["CH VIBES"] = P("CH VIBES", {"SRC": "PLAITS", "FX1": "CHORUS", "FX2": "REVERB"},
        {"SRC": {"MODEL": "2OP FM", "HARMO": 0.75, "TIMBRE": 0.18, "MORPH": 0.1, "ENV>TIMB": 0.2},
         "FX1": {"MODE": "VIBRATO", "RATE": 0.45, "DEPTH": 0.18, "WIDTH": 0.6, "MIX": 0.5}, "FX2": HALL(0.2, 0.6)},
        {"ATTACK": 0.0, "DECAY": 0.75, "SUSTAIN": 0.0, "RELEASE": 0.55, "VEL AMT": 0.75, "LEVEL": 0.6},
        [("SRC.TIMBRE", 0.3), ("FX 1.DEPTH", 0.3), ("FX 2.MIX", 0.3), ("AMP.DECAY", 0.4)],
        [["VELO", "SRC.TIMBRE", 0.2]], lfo={"SHAPE 2": "SINE", "RATE 2": 0.72, "SYNC 2": "FREE"}, fm1_voices=5)
    C["CH VIBES"]["lfo_tgt"] = [None, None, ("AMP.LEVEL", 0.22)]
    # the drawbar organ: nothing cheaper matches ADDITIVE (Braids HARM 12 dB off), so 4 voices, the chorus as the
    # rotary speaker, no reverb
    C["CH DRAWBAR"] = P("CH DRAWBAR", {"SRC": "PLAITS", "FX1": "CHORUS"},
        {"SRC": {"MODEL": "ADDITIVE", "HARMO": 0.35, "TIMBRE": 0.32, "MORPH": 0.45},
         "FX1": {"MODE": "VIBRATO", "RATE": 0.55, "DEPTH": 0.25, "WIDTH": 0.9, "MIX": 0.55}},
        {"ATTACK": 0.01, "DECAY": 0.3, "SUSTAIN": 1.0, "RELEASE": 0.12, "VEL AMT": 0.2, "SPREAD": 0.4, "LEVEL": 0.5},
        [("SRC.TIMBRE", 0.4), ("SRC.MORPH", 0.4), ("FX 1.RATE", 0.6), ("FX 1.DEPTH", 0.3)], fm1_voices=4)

    # ------------------------------------------------------------------ BASS (update 46, Josh: "more musical": less
    # static, less thin, for sequenced electronic music). The page's basses sat an octave down (C1 for the phrase's C2):
    # nearly all their energy under 250 Hz, next to nothing a small speaker or a mix hears. Now: the main voice at
    # pitch, an OSC sine an octave down, low, for weight (the cutoffs ~0.5: 700-900 Hz on C2, key tracking 0.6); drive and SOFT saturation for warm harmonics, low-passed so they
    # don't fizz; a filter envelope on every note, velocity opening it; a slow DRIFT on the tone so held notes breathe.
    # No chorus on the low end (a bass stays in the middle).
    mono = {"MODE": "LEGATO", "GLIDE": 0.1}
    breath = {"SHAPE 1": "DRIFT", "RATE 1": 0.18}          # LFO 1: a slow wander for the tone (matrix routes below)
    C["BS ROUND SUB"] = P("BS ROUND SUB", {"SRC": "PLAITS", "SHP": "FILTER"},
        {"SRC": {"MODEL": "VA VCF", "HARMO": 0.25, "TIMBRE": 0.5, "MORPH": 0.15},
         "SHP": {"TYPE": "LP24", "CUTOFF": 0.58, "RESO": 0.18, "DRIVE": 0.6, "ENV AMT": 0.3, "KEY TRK": 0.6,
                 "VEL AMT": 0.25, "ENV SRC": "OWN", "F ATK": 0.0, "F DEC": 0.32, "F SUS": 0.25, "F REL": 0.2,
                 "SAT": "SOFT"}},
        {"ATTACK": 0.0, "DECAY": 0.4, "SUSTAIN": 0.9, "RELEASE": 0.14, "VEL AMT": 0.45, "LEVEL": 0.6, **mono},
        [("SHP.CUTOFF", 0.35), ("SHP.ENV AMT", 0.4), ("SHP.DRIVE", 0.4), ("AMP.GLIDE", 0.24)],
        [["LFO 1", "SHP.CUTOFF", 0.04]], lfo=breath,
        osc={"WAVE": 0, "OCTAVE": -1, "OSC LVL": 0.25, "MAIN LVL": 0.9})
    C["BS ANALOG"] = P("BS ANALOG", {"SRC": "BRAIDS", "SHP": "FILTER"},
        {"SRC": {"MODEL": "CSAW", "TIMBRE": 0.3, "COLOR": 0.35, "DRIFT": 0.2},
         "SHP": {"TYPE": "LP24", "CUTOFF": 0.47, "RESO": 0.32, "DRIVE": 0.45, "ENV AMT": 0.45, "KEY TRK": 0.6,
                 "VEL AMT": 0.35, "ENV SRC": "OWN", "F ATK": 0.0, "F DEC": 0.3, "F SUS": 0.2, "F REL": 0.2,
                 "SAT": "SOFT"}},
        {"ATTACK": 0.0, "DECAY": 0.45, "SUSTAIN": 0.85, "RELEASE": 0.15, "VEL AMT": 0.5, "LEVEL": 0.55, **mono},
        [("SHP.CUTOFF", 0.4), ("SHP.RESO", 0.4), ("SHP.ENV AMT", 0.4), ("SHP.F DEC", 0.4)],
        [["LFO 1", "SHP.CUTOFF", 0.05]], lfo=breath,
        osc={"WAVE": 0, "OCTAVE": -1, "OSC LVL": 0.25, "MAIN LVL": 0.9})
    C["BS ACID"] = P("BS ACID", {"SRC": "BRAIDS", "SHP": "FILTER", "FX1": "DELAY"},
        {"SRC": {"MODEL": "//-_", "TIMBRE": 0.05, "COLOR": 0.5, "DRIFT": 0.05},
         "SHP": {"TYPE": "LP", "CUTOFF": 0.3, "RESO": 0.7, "DRIVE": 0.5, "ENV AMT": 0.55, "KEY TRK": 0.5, "VEL AMT": 0.5,
                 "ENV SRC": "OWN", "F ATK": 0.0, "F DEC": 0.22, "F SUS": 0.0, "F REL": 0.18, "SAT": "ASYM"},
         "FX1": ECHO("1/8D", 0.28, 0.12)},
        {"ATTACK": 0.0, "DECAY": 0.35, "SUSTAIN": 0.8, "RELEASE": 0.1, "VEL AMT": 0.55, "LEVEL": 0.45, "MODE": "LEGATO",
         "GLIDE": 0.22},
        [("SHP.CUTOFF", 0.4), ("SHP.RESO", 0.3), ("SHP.ENV AMT", 0.4), ("SHP.F DEC", 0.4)],
        [["VELO", "SHP.RESO", 0.12], ["LFO 1", "SHP.CUTOFF", 0.03]], lfo=breath)   # an accent sings: harder, more resonance
    C["BS PICKED"] = P("BS PICKED", {"SRC": "PLAITS", "SHP": "FILTER"},
        {"SRC": {"MODEL": "2OP FM", "HARMO": 0.25, "TIMBRE": 0.32, "MORPH": 0.2, "ENV>TIMB": 0.35},
         "SHP": {"TYPE": "LP", "CUTOFF": 0.55, "RESO": 0.12, "DRIVE": 0.3, "ENV AMT": 0.3, "KEY TRK": 0.6, "VEL AMT": 0.4,
                 "ENV SRC": "OWN", "F ATK": 0.0, "F DEC": 0.25, "F SUS": 0.15, "F REL": 0.15, "SAT": "SOFT"}},
        {"ATTACK": 0.0, "DECAY": 0.5, "SUSTAIN": 0.35, "RELEASE": 0.15, "VEL AMT": 0.7, "LEVEL": 0.6, "MODE": "MONO"},
        [("SHP.CUTOFF", 0.35), ("SRC.TIMBRE", 0.4), ("SRC.MORPH", 0.4), ("AMP.DECAY", 0.4)],
        [["VELO", "SRC.TIMBRE", 0.25]], osc={"WAVE": 0, "OCTAVE": -1, "OSC LVL": 0.25, "MAIN LVL": 0.9})
    C["BS FINGER"] = P("BS FINGER", {"SRC": "PLAITS", "SHP": "FILTER"},
        {"SRC": {"MODEL": "STRING", "HARMO": 0.5, "TIMBRE": 0.3, "MORPH": 0.7, "LEVEL": 1.0},
         "SHP": {"TYPE": "LP", "CUTOFF": 0.58, "RESO": 0.1, "DRIVE": 0.4, "ENV AMT": 0.15, "KEY TRK": 0.6,
                 "VEL AMT": 0.3, "ENV SRC": "AMP", "SAT": "SOFT", "GAIN": 0.5}},
        {"ATTACK": 0.0, "DECAY": 0.65, "SUSTAIN": 0.0, "RELEASE": 0.2, "VEL AMT": 0.75, "LEVEL": 0.7, "MODE": "MONO"},
        [("SHP.CUTOFF", 0.35), ("SRC.TIMBRE", 0.4), ("SRC.MORPH", 0.3), ("SHP.DRIVE", 0.4)],
        [["VELO", "SRC.TIMBRE", 0.2]], osc={"WAVE": 0, "OCTAVE": -1, "OSC LVL": 0.25, "MAIN LVL": 0.9})
    C["BS PULSE"] = P("BS PULSE", {"SRC": "PLAITS", "SHP": "FILTER"},
        {"SRC": {"MODEL": "VA VCF", "HARMO": 0.3, "TIMBRE": 0.58, "MORPH": 0.5},
         "SHP": {"TYPE": "LP24", "CUTOFF": 0.6, "RESO": 0.3, "DRIVE": 0.55, "ENV AMT": 0.4, "KEY TRK": 0.6,
                 "VEL AMT": 0.35, "ENV SRC": "OWN", "F ATK": 0.0, "F DEC": 0.22, "F SUS": 0.15, "F REL": 0.15,
                 "SAT": "SOFT"}},
        {"ATTACK": 0.0, "DECAY": 0.4, "SUSTAIN": 0.8, "RELEASE": 0.12, "VEL AMT": 0.55, "LEVEL": 0.5, **mono},
        [("SHP.CUTOFF", 0.4), ("SRC.MORPH", 0.4), ("SHP.ENV AMT", 0.4), ("SHP.RESO", 0.4)],
        [["LFO 1", "SRC.MORPH", 0.12]], lfo={"SHAPE 1": "TRI", "RATE 1": 0.3},   # the pulse's width moving (PWM)
        osc={"WAVE": 0, "OCTAVE": -1, "OSC LVL": 0.25, "MAIN LVL": 0.9})
    C["BS WARM MOOG"] = P("BS WARM MOOG", {"SRC": "PLAITS", "SHP": "FILTER"},
        {"SRC": {"MODEL": "PAIR VA", "HARMO": 0.48, "TIMBRE": 0.35, "MORPH": 0.3},
         "SHP": {"TYPE": "LP24", "CUTOFF": 0.46, "RESO": 0.32, "DRIVE": 0.55, "ENV AMT": 0.4, "KEY TRK": 0.6,
                 "VEL AMT": 0.35, "ENV SRC": "OWN", "F ATK": 0.0, "F DEC": 0.35, "F SUS": 0.3, "F REL": 0.2,
                 "SAT": "SOFT"}},
        {"ATTACK": 0.0, "DECAY": 0.45, "SUSTAIN": 0.85, "RELEASE": 0.15, "VEL AMT": 0.5, "LEVEL": 0.5, **mono},
        [("SHP.CUTOFF", 0.4), ("SHP.RESO", 0.4), ("SHP.DRIVE", 0.4), ("AMP.GLIDE", 0.24)],
        [["LFO 1", "SHP.CUTOFF", 0.04]], lfo=breath,
        osc={"WAVE": 0, "OCTAVE": -1, "OSC LVL": 0.25, "MAIN LVL": 0.9})
    # the dub sine: 2OP FM nearly a sine (ADDITIVE cost 29 points a voice), a little drive so it's heard on small
    # speakers, the sub under it, the echo kept
    C["BS DUB SINE"] = P("BS DUB SINE", {"SRC": "PLAITS", "SHP": "FILTER", "FX1": "DELAY"},
        {"SRC": {"MODEL": "2OP FM", "HARMO": 0.5, "TIMBRE": 0.3, "MORPH": 0.1},
         "SHP": {"TYPE": "LP", "CUTOFF": 0.6, "RESO": 0.05, "DRIVE": 0.6, "ENV AMT": 0.25, "KEY TRK": 0.6,
                 "VEL AMT": 0.2, "ENV SRC": "OWN", "F ATK": 0.0, "F DEC": 0.4, "F SUS": 0.4, "F REL": 0.25,
                 "SAT": "SOFT"},
         "FX1": ECHO("1/4D", 0.3, 0.1)},
        {"ATTACK": 0.01, "DECAY": 0.5, "SUSTAIN": 0.9, "RELEASE": 0.2, "VEL AMT": 0.45, "LEVEL": 0.7, **mono},
        [("SRC.TIMBRE", 0.24), ("SHP.DRIVE", 0.4), ("FX 1.MIX", 0.2), ("AMP.GLIDE", 0.24)],
        [["LFO 1", "SRC.TIMBRE", 0.04]], lfo=breath,
        osc={"WAVE": 0, "OCTAVE": -1, "OSC LVL": 0.25, "MAIN LVL": 0.9})
    # soft keys: a little darker (its tone sat at 1.6 kHz), the sub for weight, the chorus out
    C["BS SOFT KEYS"] = P("BS SOFT KEYS", {"SRC": "PLAITS", "SHP": "FILTER"},
        {"SRC": {"MODEL": "WAVESHP", "HARMO": 0.2, "TIMBRE": 0.18, "MORPH": 0.3, "ENV>TIMB": 0.25},
         "SHP": {"TYPE": "LP", "CUTOFF": 0.62, "RESO": 0.08, "DRIVE": 0.2, "ENV AMT": 0.2, "KEY TRK": 0.6,
                 "VEL AMT": 0.3, "ENV SRC": "AMP", "SAT": "SOFT"}},
        {"ATTACK": 0.0, "DECAY": 0.55, "SUSTAIN": 0.4, "RELEASE": 0.18, "VEL AMT": 0.7, "LEVEL": 0.6, "MODE": "MONO"},
        [("SRC.TIMBRE", 0.35), ("SRC.MORPH", 0.4), ("SHP.CUTOFF", 0.3), ("SRC.HARMO", 0.3)],
        [["VELO", "SRC.TIMBRE", 0.25]], osc={"WAVE": 0, "OCTAVE": -1, "OSC LVL": 0.25, "MAIN LVL": 0.9})

    # ------------------------------------------------------------------ PADS
    C["PD WARM STRINGS"] = P("PD WARM STRINGS", {"SRC": "PLAITS", "FX1": "REVERB"},
        {"SRC": {"MODEL": "STR MACH", "HARMO": 0.5, "TIMBRE": 0.45, "MORPH": 0.5}, "FX1": HALL(0.3)},
        {**slow, "ATTACK": 0.42, "SPREAD": 0.75, "LEVEL": 0.5},
        [("SRC.TIMBRE", 0.4), ("SRC.MORPH", 0.4), ("FX 1.MIX", 0.3), ("AMP.ATTACK", 0.5)],
        [["LFO 1", "SRC.TIMBRE", 0.08]],
        lfo={"SHAPE 1": "TRI", "RATE 1": 0.2, "SHAPE 2": "SINE", "RATE 2": 0.12, "SYNC 2": "FREE"}, fm1_voices=5)
    C["PD WARM STRINGS"]["lfo_tgt"] = [None, None, ("SRC.MORPH", 0.12)]   # the ensemble's own movement, for the chorus
    # VA VCF's own filter for PAIR VA + FILTER (1.2 dB from it); FUNC swells it while keys are held (GATE, AR), as
    # the filter's slow envelope did
    C["PD ANALOG"] = P("PD ANALOG", {"SRC": "PLAITS", "MODA": "FUNC", "FX1": "CHORUS", "FX2": "REVERB"},
        {"SRC": {"MODEL": "VA VCF", "HARMO": 0.45, "TIMBRE": 0.45, "MORPH": 0.0},   # the phrase's centroid 733 Hz, was 760
         "MODA": FUNC(RISE_1=2.5, FALL_1=1.5, CURVE_1=-0.4, TRIG_1="GATE", HOLD="AR", TRIG_4="OFF", CYCLE_4="OFF"),
         "FX1": CHOR(0.4, 0.5), "FX2": HALL(0.3)},
        {**slow, "LEVEL": 0.5},
        [("SRC.TIMBRE", 0.3), ("SRC.HARMO", 0.3), ("FX 1.DEPTH", 0.4), ("AMP.ATTACK", 0.5)],
        [["LFO 1", "SRC.TIMBRE", 0.06], ["A OUT 1", "SRC.TIMBRE", 0.12]],
        lfo={"SHAPE 1": "TRI", "RATE 1": 0.15}, fm1_voices=4)
    C["PD CHOIR"] = {"fm1_voices": 4}
    C["PD GLASS"] = P("PD GLASS", {"SRC": "PLAITS", "FX1": "REVERB"},
        {"SRC": {"MODEL": "WAVETBL", "HARMO": 0.3, "TIMBRE": 0.35, "MORPH": 0.4}, "FX1": SHIM(0.3, 0.25)},
        {**slow, "ATTACK": 0.35, "SPREAD": 0.8, "LEVEL": 0.5},
        [("SRC.TIMBRE", 0.4), ("SRC.MORPH", 0.4), ("FX 1.MIX", 0.3), ("FX 1.SHIMMER", 0.4)],
        [["LFO 1", "SRC.MORPH", 0.12]], lfo={"SHAPE 1": "TRI", "SYNC 1": "2 BARS"}, fm1_voices=5)
    C["PD GLASS"]["lfo_tgt"] = [None, None, None, None, ("SRC.TIMBRE", 0.06)]   # LFO 3 (DRIFT), for the chorus' unrest
    # 2OP FM, nearly a sine, for ADDITIVE: 18 points a voice for 29, five voices; the whole phrase's spectral centroid
    # 685 Hz against 716 (WAVESHP, closest on the first 16 harmonics, came out at 965-1650 over the phrase)
    C["PD SOFT SINE"] = P("PD SOFT SINE", {"SRC": "PLAITS", "FX1": "CHORUS", "FX2": "REVERB"},
        {"SRC": {"MODEL": "2OP FM", "HARMO": 0.5, "TIMBRE": 0.35, "MORPH": 0.15},
         "FX1": CHOR(0.35, 0.4, 0.2), "FX2": HALL(0.3)},
        {**slow, "ATTACK": 0.55, "LEVEL": 0.6},
        [("SRC.TIMBRE", 0.3), ("SRC.MORPH", 0.3), ("FX 2.MIX", 0.3), ("AMP.ATTACK", 0.5)], fm1_voices=5)
    C["PD RESONANT"] = P("PD RESONANT", {"SRC": "PLAITS", "SHP": "RINGS"},
        {"SRC": {"MODEL": "VA VCF", "HARMO": 0.3, "TIMBRE": 0.2, "MORPH": 0.4},     # darker in, as STR+VERB is
         "SHP": {"MODEL": "STR+VERB", "STRUCT": 0.3, "BRIGHT": 0.35, "DAMPING": 0.6, "POSITION": 0.4, "EXCITE": 0.4,
                 "MIX": 0.5, "SPREAD": 0.6}},                                          # brighter and wider than STRING
        {**slow, "ATTACK": 0.4, "LEVEL": 0.5},
        [("SHP.STRUCT", 0.4), ("SHP.BRIGHT", 0.4), ("SHP.DAMPING", 0.3), ("SHP.MIX", 0.4)],
        [["LFO 1", "SHP.POSITION", 0.15]], lfo={"SHAPE 1": "DRIFT", "RATE 1": 0.2}, fm1_voices=4)
    C["PD SHIMMER"] = P("PD SHIMMER", {"SRC": "PLAITS", "FX1": "REVERB"},
        {"SRC": {"MODEL": "2OP FM", "HARMO": 0.5, "TIMBRE": 0.15, "MORPH": 0.15}, "FX1": SHIM(0.35, 0.4)},
        {**slow, "ATTACK": 0.4, "SPREAD": 0.75, "LEVEL": 0.5},
        [("SRC.TIMBRE", 0.24), ("FX 1.SHIMMER", 0.4), ("FX 1.MIX", 0.3), ("AMP.ATTACK", 0.5)],
        [["LFO 1", "SRC.TIMBRE", 0.06]], lfo={"SHAPE 1": "SINE", "RATE 1": 0.12})
    # FUNC for Tides: two looping slopes at unrelated rates, their mix on TIMBRE and its inverse on MORPH, so the two
    # move against each other and the pattern takes long to repeat; M4 the faster one's share
    C["PD EVOLVING"] = P("PD EVOLVING", {"SRC": "PLAITS", "MODA": "FUNC", "FX1": "CHORUS", "FX2": "REVERB"},
        {"SRC": {"MODEL": "TERRAIN", "HARMO": 0.45, "TIMBRE": 0.2, "MORPH": 0.55},
         "MODA": FUNC(RISE_1=3.1, FALL_1=4.7, CYCLE_1="ON", TRIG_1="OFF", RISE_4=1.9, FALL_4=2.3, CYCLE_4="ON",
                      CURVE_4=-0.5, LVL_1=0.6, LVL_4=0.4, CH_2_SRC="-", CH_2_AMT=-0.5, LVL_2=1.0, OUT_1="SUM",
                      OUT_2="INV"),   # CH 2 with no source: an offset of -0.5, so the mix swings either side of 0
         "FX1": CHOR(0.35, 0.4), "FX2": HALL(0.32, 0.85)},
        {**slow, "ATTACK": 0.55, "RELEASE": 0.7, "LEVEL": 0.5},
        [("SRC.TIMBRE", 0.4), ("SRC.MORPH", 0.4), ("FX 2.MIX", 0.3), ("MOD A.LVL 4", 0.6)],
        [["A OUT 1", "SRC.TIMBRE", 0.2], ["A OUT 2", "SRC.MORPH", 0.15]], fm1_voices=5)
    C["PD ORGAN AIR"] = P("PD ORGAN AIR", {"SRC": "PLAITS", "FX1": "CHORUS", "FX2": "DELAY"},
        {"SRC": {"MODEL": "ADDITIVE", "HARMO": 0.3, "TIMBRE": 0.4, "MORPH": 0.5},
         "FX1": CHOR(0.4, 0.4),
         "FX2": {"TIME": "1/8", "FEEDBACK": 0.55, "TONE": 0.35, "MIX": 0.24, "MODE": "PING", "WOW": 0.3, "LO CUT": 0.3}},
        {**slow, "ATTACK": 0.35, "LEVEL": 0.5},
        [("SRC.TIMBRE", 0.4), ("SRC.MORPH", 0.4), ("FX 2.MIX", 0.3), ("AMP.ATTACK", 0.5)],
        osc={"WAVE": 1, "OCTAVE": -1, "OSC LVL": 0.25}, fm1_voices=4)
    C["PD BELL PAD"] = {"fm1_voices": 5}

    # ------------------------------------------------------------------ FX
    C["FX RESONATOR"] = P("FX RESONATOR", {"SRC": "PLAITS", "SHP": "RINGS", "MODA": "TIDES"},
        {"SRC": {"MODEL": "2OP FM", "HARMO": 0.3, "TIMBRE": 0.4, "MORPH": 0.2, "LPG": "ON", "DECAY": 0.3},
         "SHP": {"MODEL": "STR+VERB", "STRUCT": 0.55, "BRIGHT": 0.6, "DAMPING": 0.7, "POSITION": 0.3, "EXCITE": 0.7,
                 "MIX": 0.8, "SPREAD": 0.6},
         "MODA": {"MODE": "LOOP", "FREQ": 0.15, "SHAPE": 0.5, "SLOPE": 0.0, "RANGE": "LO"}},
        {"ATTACK": 0.0, "DECAY": 0.7, "SUSTAIN": 0.0, "RELEASE": 0.7, "VEL AMT": 0.6, "SPREAD": 0.6, "LEVEL": 0.6},
        [("SHP.STRUCT", 0.4), ("SHP.BRIGHT", 0.4), ("SHP.MIX", 0.3), ("SHP.DAMPING", 0.3)],
        [["A TIDES", "SHP.POSITION", 0.2]], fm1_voices=4)
    C["FX DRONE"] = {"fm1_voices": 3}
    C["FX WIND"] = P("FX WIND", {"SRC": "PLAITS", "SHP": "FILTER", "FX1": "REVERB"},   # 5 voices; louder at the source
        {"SRC": {"MODEL": "NOISE", "HARMO": 0.5, "TIMBRE": 0.6, "MORPH": 0.6, "LEVEL": 1.0},  # (7 dB under at full LEVEL)
         "SHP": {"TYPE": "BP", "CUTOFF": 0.5, "RESO": 0.55, "DRIVE": 0.0, "ENV AMT": 0.0, "KEY TRK": 0.6, "ENV SRC": "AMP",
                 },
         "FX1": HALL(0.35, 0.9)},
        {**tex, "LEVEL": 0.6},
        [("SHP.CUTOFF", 0.4), ("SHP.RESO", 0.4), ("FX 1.MIX", 0.3), ("AMP.ATTACK", 0.4)],
        [["LFO 1", "SHP.CUTOFF", 0.15], ["LFO 2", "SHP.RESO", 0.1]],
        lfo={"SHAPE 1": "DRIFT", "RATE 1": 0.18, "SHAPE 2": "SINE", "RATE 2": 0.1}, fm1_voices=5)
    C["FX RAIN"] = {"fm1_voices": 3}
    C["FX STRING BLOOM"] = P("FX STRING BLOOM", {"SRC": "PLAITS", "SHP": "RINGS"},
        {"SRC": {"MODEL": "VA VCF", "HARMO": 0.3, "TIMBRE": 0.4, "MORPH": 0.4},
         "SHP": {"MODEL": "STR+VERB", "STRUCT": 0.4, "BRIGHT": 0.45, "DAMPING": 0.8, "POSITION": 0.35, "EXCITE": 0.45,
                 "MIX": 0.7, "SPREAD": 0.7}},
        {**tex, "ATTACK": 0.45, "LEVEL": 0.5},
        [("SHP.BRIGHT", 0.4), ("SHP.STRUCT", 0.4), ("SHP.DAMPING", 0.3), ("AMP.ATTACK", 0.5)],
        [["LFO 1", "SHP.POSITION", 0.15]], lfo={"SHAPE 1": "DRIFT", "RATE 1": 0.15}, fm1_voices=4)
    C["FX CHIMES"] = {"fm1_voices": 4}
    C["FX ECHO KEYS"] = {"fm1_voices": 4}
    C["FX CHORD DRIFT"] = {"fm1_voices": 3}
    # the bowl's beating (an LFO target on level) for the chorus
    C["FX SINGING BOWL"] = P("FX SINGING BOWL", {"SRC": "PLAITS", "FX1": "REVERB"},
        {"SRC": {"MODEL": "MODAL", "HARMO": 0.62, "TIMBRE": 0.3, "MORPH": 0.88}, "FX1": HALL(0.32, 0.9)},
        {"ATTACK": 0.0, "DECAY": 0.9, "SUSTAIN": 0.0, "RELEASE": 0.85, "VEL AMT": 0.6, "SPREAD": 0.7, "LEVEL": 0.6},
        [("SRC.HARMO", 0.3), ("SRC.TIMBRE", 0.4), ("FX 1.MIX", 0.3), ("SRC.MORPH", 0.2)],
        [["LFO 1", "SRC.TIMBRE", 0.06]],
        lfo={"SHAPE 1": "SINE", "RATE 1": 0.1, "SHAPE 2": "SINE", "RATE 2": 0.3, "SYNC 2": "FREE"}, fm1_voices=4)
    C["FX SINGING BOWL"]["lfo_tgt"] = [None, None, ("AMP.LEVEL", 0.12)]
    C["FX GLASS RAIN"] = {"fm1_voices": 4}

    # ------------------------------------------------------------------ LEADS
    # the vibrato: the page's RATE 0.48-0.55 is 1.0-1.3 Hz (run_lfos: 0.03 Hz x 2^(10 x RATE)), a slow wobble; a
    # player's vibrato is 4.5-6 Hz: 0.72-0.75 (its depth and fade-in as they were)
    for n, r in (("LD SOFT SAW", 0.75), ("LD SQUARE", 0.75), ("LD SYNC", 0.75), ("LD FLUTE", 0.72), ("LD WHISTLE", 0.74),
                 ("LD REED", 0.72), ("LD BOWED", 0.72)):
        C[n] = {"lfo": {"RATE 1": r}}
    return C


def added():
    """the FM-1's own ten"""
    L = []
    a = L.append
    # keys
    a(P("CH SYNTH PLUCK", {"SRC": "BRAIDS", "SHP": "FILTER", "FX1": "DELAY", "FX2": "REVERB"},
        {"SRC": {"MODEL": "//-_", "TIMBRE": 0.3, "COLOR": 0.4, "DRIFT": 0.1},
         "SHP": {"TYPE": "LP", "CUTOFF": 0.3, "RESO": 0.3, "DRIVE": 0.2, "ENV AMT": 0.5, "KEY TRK": 0.6, "VEL AMT": 0.35,
                 "ENV SRC": "OWN", "F ATK": 0.0, "F DEC": 0.24, "F SUS": 0.05, "F REL": 0.2},
         "FX1": ECHO("1/8D", 0.3, 0.16), "FX2": PLATE(0.14)},
        {"ATTACK": 0.0, "DECAY": 0.45, "SUSTAIN": 0.0, "RELEASE": 0.3, "VEL AMT": 0.7, "SPREAD": 0.5, "LEVEL": 0.5},
        [("SHP.CUTOFF", 0.4), ("SHP.F DEC", 0.4), ("FX 1.MIX", 0.3), ("SHP.RESO", 0.4)]))
    a(P("CH REED KEYS", {"SRC": "PLAITS", "FX1": "CHORUS", "FX2": "REVERB"},
        {"SRC": {"MODEL": "WAVESHP", "HARMO": 0.3, "TIMBRE": 0.32, "MORPH": 0.55, "ENV>TIMB": 0.3},
         "FX1": CHOR(0.15, 0.2, 0.3), "FX2": PLATE(0.12, 0.4)},
        {"ATTACK": 0.0, "DECAY": 0.55, "SUSTAIN": 0.35, "RELEASE": 0.25, "VEL AMT": 0.8, "SPREAD": 0.3, "LEVEL": 0.55},
        [("SRC.TIMBRE", 0.35), ("LFO.RATE 2", 0.2), ("FX 2.MIX", 0.24), ("SRC.ENV>TIMB", 0.4)],
        [["VELO", "SRC.TIMBRE", 0.3]], lfo={"SHAPE 2": "SINE", "RATE 2": 0.74, "SYNC 2": "FREE"}))
    L[-1]["lfo_tgt"] = [None, None, ("AMP.LEVEL", 0.18)]   # the amplifier's tremolo
    # bass
    a(P("BS SUB BOOM", {"SRC": "PLAITS", "SHP": "FILTER"},   # (update 46: at pitch, saturated: heard, not only felt)
        {"SRC": {"MODEL": "BASS DR", "HARMO": 0.3, "TIMBRE": 0.35, "MORPH": 0.75},
         "SHP": {"TYPE": "LP", "CUTOFF": 0.66, "RESO": 0.05, "DRIVE": 0.75, "ENV AMT": 0.1, "KEY TRK": 0.6,
                 "ENV SRC": "AMP", "SAT": "SOFT"}},
        {"ATTACK": 0.0, "DECAY": 0.8, "SUSTAIN": 1.0, "RELEASE": 0.25, "VEL AMT": 0.6, "LEVEL": 0.6, "MODE": "MONO"},
        [("SRC.TIMBRE", 0.3), ("SRC.HARMO", 0.4), ("SHP.DRIVE", 0.4), ("AMP.RELEASE", 0.3)],
        [["VELO", "SRC.HARMO", 0.2]], osc={"WAVE": 0, "OSC LVL": 0.2, "MAIN LVL": 0.9}))
    # FUNC: a pluck on each note (CH 1) and a wobble on the eighths (CH 4), mixed (SUM) onto the cutoff; M2 is the
    # wobble's level in the mix
    a(P("BS WOBBLE", {"SRC": "BRAIDS", "SHP": "FILTER", "MODA": "FUNC"},
        {"SRC": {"MODEL": "//-_", "TIMBRE": 0.2, "COLOR": 0.5, "DRIFT": 0.05},
         "SHP": {"TYPE": "LP24", "CUTOFF": 0.22, "RESO": 0.45, "DRIVE": 0.4, "ENV AMT": 0.0, "KEY TRK": 0.5,
                 "ENV SRC": "AMP", "SAT": "SOFT"},
         "MODA": FUNC(RISE_1=0.003, FALL_1=0.25, CURVE_1=0.6, TRIG_1="NOTE", RISE_4=0.09, FALL_4=0.14,
                      CYCLE_4="OFF", TRIG_4="1/8", CURVE_4=0.3, LVL_1=0.55, LVL_4=0.5, OUT_1="SUM", OUT_2="CH 4")},
        {"ATTACK": 0.0, "DECAY": 0.4, "SUSTAIN": 0.9, "RELEASE": 0.12, "VEL AMT": 0.5, "LEVEL": 0.5, "MODE": "LEGATO",
         "GLIDE": 0.1},
        [("SHP.CUTOFF", 0.3), ("MOD A.LVL 4", 0.8), ("SHP.RESO", 0.4), ("MOD A.FALL 4", 0.3)],
        [["A OUT 1", "SHP.CUTOFF", 0.45], ["VELO", "SHP.CUTOFF", 0.1]], osc={"WAVE": 0, "OCTAVE": -1, "OSC LVL": 0.35}))
    # lead: phase distortion, FUNC sweeping its amount down on each note (CH 1) and fading the vibrato in over 0.7 s
    # (CH 4, then 6 s back down: HOLD is both channels', and AR would hold the sweep up too)
    a(P("LD PHASE SWEEP", {"SRC": "PLAITS", "MODA": "FUNC", "FX1": "DELAY", "FX2": "REVERB"},
        {"SRC": {"MODEL": "PHASE D", "HARMO": 0.4, "TIMBRE": 0.25, "MORPH": 0.35},
         "MODA": FUNC(RISE_1=0.004, FALL_1=0.6, CURVE_1=0.5, TRIG_1="NOTE", RISE_4=0.7, FALL_4=6.0, CYCLE_4="OFF",
                      TRIG_4="NOTE", OUT_1="CH 1", OUT_2="CH 4"),
         "FX1": ECHO("1/8D", 0.3, 0.14), "FX2": PLATE(0.14)},
        {"ATTACK": 0.01, "DECAY": 0.5, "SUSTAIN": 0.85, "RELEASE": 0.3, "VEL AMT": 0.5, "LEVEL": 0.5, "MODE": "LEGATO",
         "GLIDE": 0.1},
        [("SRC.TIMBRE", 0.3), ("MOD A.FALL 1", 0.3), ("FX 1.MIX", 0.3), ("AMP.GLIDE", 0.24)],
        [["A OUT 1", "SRC.TIMBRE", 0.35], ["LFO 1", "PITCH", 0.014, "A OUT 2"], ["VELO", "SRC.TIMBRE", 0.15]],
        lfo={"SHAPE 1": "SINE", "RATE 1": 0.74}))
    # pads
    a(P("PD SAW SWARM", {"SRC": "PLAITS", "FX1": "REVERB"},
        {"SRC": {"MODEL": "SWARM", "HARMO": 0.4, "TIMBRE": 0.4, "MORPH": 0.35}, "FX1": HALL(0.3, 0.8)},
        {**slow, "ATTACK": 0.45, "SPREAD": 0.8, "LEVEL": 0.5},
        [("SRC.TIMBRE", 0.4), ("SRC.MORPH", 0.4), ("FX 1.MIX", 0.3), ("AMP.ATTACK", 0.5)],
        lfo={"SHAPE 2": "SINE", "RATE 2": 0.1, "SYNC 2": "FREE"}, fm1_voices=5))
    L[-1]["lfo_tgt"] = [None, None, ("SRC.MORPH", 0.1)]
    # FUNC: an eighth-note gate on the cutoff (CH 1) and a slow swell over each bar on the resonance (CH 4)
    a(P("PD TEMPO GATE", {"SRC": "BRAIDS", "SHP": "FILTER", "MODA": "FUNC", "FX1": "CHORUS", "FX2": "REVERB"},
        {"SRC": {"MODEL": "CSAW", "TIMBRE": 0.35, "COLOR": 0.4, "DRIFT": 0.2},
         "SHP": {"TYPE": "LP", "CUTOFF": 0.22, "RESO": 0.25, "DRIVE": 0.15, "ENV AMT": 0.0, "KEY TRK": 0.5, "ENV SRC": "AMP"},
         "MODA": FUNC(RISE_1=0.002, FALL_1=0.22, CURVE_1=0.5, TRIG_1="1/8", RISE_4=1.6, FALL_4=0.5, TRIG_4="BAR",
                      CYCLE_4="OFF", OUT_1="CH 1", OUT_2="CH 4"),
         "FX1": CHOR(0.3, 0.4), "FX2": HALL(0.28)},
        {**slow, "ATTACK": 0.2, "LEVEL": 0.5},
        [("SHP.CUTOFF", 0.3), ("MOD A.FALL 1", 0.3), ("FX 2.MIX", 0.3), ("SHP.RESO", 0.3)],
        [["A OUT 1", "SHP.CUTOFF", 0.4], ["A OUT 2", "SHP.RESO", 0.15], ["A OUT 1", "AMP.LEVEL", 0.12]]))
    # FUNC: the filter blooms while a chord is held (GATE, AR: 3.5 s up, 1.8 s down); a slow loop breathes the chorus.
    # (Braids' triple saw: TIMBRE and COLOR are the other two saws' intervals, 0.5 unison; near it, a few cents)
    a(P("PD SLOW BLOOM", {"SRC": "BRAIDS", "SHP": "FILTER", "MODA": "FUNC", "FX1": "CHORUS", "FX2": "REVERB"},
        {"SRC": {"MODEL": "//x3", "TIMBRE": 0.535, "COLOR": 0.465, "DRIFT": 0.2},   # three saws, about 7 cents apart
         "SHP": {"TYPE": "LP", "CUTOFF": 0.2, "RESO": 0.2, "DRIVE": 0.1, "ENV AMT": 0.0, "KEY TRK": 0.5, "ENV SRC": "AMP"},
         "MODA": FUNC(RISE_1=3.5, FALL_1=1.8, CURVE_1=0.3, TRIG_1="GATE", HOLD="AR", RISE_4=4.0, FALL_4=5.0,
                      CYCLE_4="ON", TRIG_4="OFF", OUT_1="CH 1", OUT_2="CH 4"),
         "FX1": CHOR(0.35, 0.4, 0.15), "FX2": HALL(0.32, 0.85)},
        {**slow, "ATTACK": 0.35, "RELEASE": 0.7, "LEVEL": 0.5},
        [("SHP.CUTOFF", 0.3), ("MOD A.RISE 1", 0.3), ("FX 2.MIX", 0.3), ("SHP.RESO", 0.3)],
        [["A OUT 1", "SHP.CUTOFF", 0.4], ["A OUT 2", "FX 1.DEPTH", 0.25]], fm1_voices=5))
    # fx: FUNC's two channels in turn: CH 1 loops, CH 4 starts as CH 1 starts to fall (its EOR, OUT 1, is CH 4's
    # trigger); OUT 2 is CH 4 less CH 1, so the sound swings left with CH 1 and right, brighter, with CH 4
    a(P("FX BOUNCE", {"SRC": "PLAITS", "MODA": "FUNC", "FX1": "DELAY", "FX2": "REVERB"},
        {"SRC": {"MODEL": "WAVESHP", "HARMO": 0.5, "TIMBRE": 0.3, "MORPH": 0.6, "ENV>TIMB": 0.2},
         "MODA": FUNC(RISE_1=0.15, FALL_1=0.35, CYCLE_1="ON", TRIG_1="OFF", RISE_4=0.15, FALL_4=0.3, CYCLE_4="OFF",
                      TRIG_4="SOURCE", TRIG_SRC="A OUT 1", LVL_1=-1.0, LVL_4=1.0, OUT_1="EOR", OUT_2="SUM"),
         "FX1": ECHO("1/8", 0.35, 0.18), "FX2": HALL(0.3, 0.8)},
        {**tex, "ATTACK": 0.05, "LEVEL": 0.5},
        [("SRC.TIMBRE", 0.3), ("MOD A.FALL 1", 0.4), ("FX 2.MIX", 0.3), ("MOD A.FALL 4", 0.4)],
        [["A OUT 2", "PAN", 0.7], ["A OUT 2", "SRC.TIMBRE", 0.15]], fm1_voices=5))
    # FUNC: a slow loop through the words (MORPH) of Plaits' speech model
    a(P("FX SPEAK", {"SRC": "PLAITS", "MODA": "FUNC", "FX1": "DELAY"},
        {"SRC": {"MODEL": "SPEECH", "HARMO": 0.85, "TIMBRE": 0.5, "MORPH": 0.2},
         "MODA": FUNC(RISE_1=1.2, FALL_1=1.2, CYCLE_1="ON", TRIG_1="OFF", TRIG_4="OFF", CYCLE_4="OFF"),
         "FX1": ECHO("1/4", 0.3, 0.16)},
        {"ATTACK": 0.01, "DECAY": 0.5, "SUSTAIN": 0.9, "RELEASE": 0.3, "VEL AMT": 0.5, "SPREAD": 0.4, "LEVEL": 0.55},
        [("SRC.TIMBRE", 0.4), ("SRC.HARMO", 0.3), ("FX 1.MIX", 0.3), ("MOD A.RISE 1", 0.4)],
        [["A OUT 1", "SRC.MORPH", 0.6]], fm1_voices=4))
    return L


def fm1_presets(page):
    """the FM-1's set: the page's (presets.json, in its order) as changed above, then the new ones"""
    designed = {p["name"]: p for p in S.presets()}
    C = changed()
    out = []
    for p in page:
        q = json.loads(json.dumps(designed[p["name"]]))
        c = C.get(p["name"])
        if c and "slots" in c:
            q = c
        elif c:
            for k, v in c.items():
                q[k] = {**q.get(k, {}), **v} if k == "lfo" else v
        q["amp"]["LEVEL"] = p["amp"]["LEVEL"]          # the page's measured level, until measured on the FM-1
        out.append(q)
    out += added()
    names = [p["name"] for p in out]
    unknown = set(C) - set(names)
    if unknown or len(names) != len(set(names)) or any(len(n) > 16 for n in names):
        raise SystemExit(f"presets_fm1: {unknown or 'a name twice, or longer than 16'}")
    if LEVELS.exists():
        lv = json.loads(LEVELS.read_text())
        for p in out:
            if p["name"] in lv:
                p["amp"]["LEVEL"] = lv[p["name"]]
    return out
