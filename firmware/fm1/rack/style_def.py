"""STYLE, the arpeggiator's accompaniment (FM-1 only): while a chord is held, three layers play it in a style's rhythm,
each on its own MIDI channel and, if chosen, on the FM-1 (play.c st_*). make_ui.py writes the ARP pages' extra
parameters from ARP_EXTRA and the patterns from STYLES into ui_defs.h.

A pattern is one or two bars of steps: 16ths (div 16) or 8th-note triplets (div 3, twelve to the bar).
  chord: X accent, x hit, o ghost, - holds the note before, . rest
  bass:  R root (accent), r root, 5 fifth, 3 third, 7 seventh, 8 octave up, b fifth below, p the scale step above the
         root, a a semitone below the root (an approach), - holds the note before, . rest
swing: how late every second 16th falls (0-0.6 of a 16th), as the arpeggiator's SWING."""

def _L(l, o, d): return {"l": l, "t": "list", "opts": o, "def": d, "mod": False}
def _I(l, lo, hi, d): return {"l": l, "t": "int", "min": lo, "max": hi, "def": d, "mod": False}
def _n(l, d): return {"l": l, "t": "num", "def": d, "mod": False}

CH = ["OFF"] + [str(i) for i in range(1, 17)]
ONOFF = ["OFF", "ON"]

# name, steps' division, swing, chord, bass
STYLES = [
    # pop, rock, ballad
    ("POP 8THS", 16, 0.0, "X-x-x-x-X-x-x-x-", "R---..r-5---..r-"),
    ("POP PUSH", 16, 0.0, "X--x--X-x-x---x-", "R--r--5-r---8-r-"),
    ("ROCK", 16, 0.0, "X.x.x.x.X.x.x.x.", "R.r.r.r.R.r.5.r."),
    ("POWER ROCK", 16, 0.0, "X-------X---x-x-", "R.R.R.R.R.R.R.R."),
    ("BALLAD", 16, 0.0, "X-------x-------", "R-------5-------"),
    ("6/8 BALLAD", 3, 0.0, "XooxooXooxoo", "R-----5-----"),
    # dance
    ("HOUSE", 16, 0.0, "..X...x...X...x.", "..R...r...R...r."),
    ("DEEP HOUSE", 16, 0.15, "X..x..x...x..x..", "R..r..r.....r.5."),
    ("TECHNO", 16, 0.0, "..o...o...o...X.", ".rrr.rrr.rrr.rrr"),
    ("DISCO", 16, 0.0, "..x...x...x...x.", "R.8.R.8.R.8.R.8."),
    ("TRANCE", 16, 0.0, "x-x-x-x-x-x-x-x-", ".rrr.rrr.rrr.rrr"),
    ("UK GARAGE", 16, 0.35, "x..x..x...x..x..", "R.....r.R..r..5."),
    # drum and bass
    ("DNB ROLLER", 16, 0.0, "X-------....x---", "R-----.rR-----.5"),
    ("LIQUID DNB", 16, 0.0, "X-------x-------", "R-...r....R-.5.."),
    ("JUNGLE", 16, 0.1, "X---------x-----", "R--r..r.R--r..5."),
    # hip-hop, lo-fi
    ("BOOM BAP", 16, 0.3, "X..o..x...X..o..", "R.....r.R.r....."),
    ("TRAP", 16, 0.0, "X-------....x---", "R-----r-..R---.."),
    ("WEST COAST", 16, 0.2, "X.o.x.o.X.o.x.o.", "R..8.r.5R..8.r.3"),
    ("LOFI", 16, 0.45, "X--.---.x--.----", "R-.....rR-....5."),
    ("LOFI JAZZ", 16, 0.5, "X--o..x-.o..x---", "R---3---5---7---"),
    # groove
    ("FUNK", 16, 0.1, "..x.o.xo..x.o.xo", "R..r.8.rR.p.5.r8"),
    ("R&B", 16, 0.2, "X-------x--x----", "R--r..5.R---..3."),
    ("NEO SOUL", 16, 0.3, "X---..x-.--x..o.", "R-.r..5-.a..R..."),
    # ambient
    ("AMB SWELL", 16, 0.0, "X-------------------------------", "R---------------5---------------"),
    ("AMB PULSE", 16, 0.0, "x.o.x.o.x.o.x.o.", "R---------------"),
    ("DRONE", 16, 0.0, "X---------------", "R---------------"),
    # world and jazz
    ("REGGAE", 16, 0.0, "....Xo......Xo..", "..r-.r..5-..r..."),
    ("BOSSA NOVA", 16, 0.0, "X..x..x...x..x..X..x...x..x..x..", "R-----5-R-----5-R-----5-R-----5-"),
    ("SAMBA", 16, 0.0, "x.xx.x.xx.x.xx.x", "R..5R..5R..5R..5"),
    ("AFROBEAT", 16, 0.05, "x.xox.x.ox.xox.x", "R..r.5..r.R.5.r."),
    ("SALSA", 16, 0.0, "x.x..x.x.x..x.x.x.x..x.x.x..x.x.", "......R-....5-........R-....5-.."),
    ("JAZZ SWING", 16, 0.0, "X...x...X...x...", "R...3...5...a..."),
]
for name, div, sw, c, b in STYLES:
    assert len(name) <= 10 and div in (16, 3) and len(c) == len(b) <= 32 and len(c) == (12 if div == 3 else len(c)), name
    assert set(c) <= set("Xxo-.") and set(b) <= set("Rr53781pab-."), name

# the ARP pages after the arpeggiator's two: STYLE, the layers' MIDI channels, which layers the FM-1 plays
ARP_EXTRA = [
    [_L("STYLE", ["OFF", "HELD"] + [s[0] for s in STYLES], 0), _I("BASS OCT", -2, 1, 0), _n("ACCENT", 0.6),
     _n("CHD LEN", 0.6)],
    [_L("CHORD CH", CH, 1), _L("BASS CH", CH, 2), _L("ARP CH", CH, 3)],
    [_L("CHORD", ONOFF, 1), _L("BASS", ONOFF, 0), _L("ARP", ONOFF, 0)],
]


def style_table():
    """the C table: name, division, swing, length, chord and bass steps"""
    rows = [f'    {{{div}, {sw!r}f, {len(c)}, "{c}", "{b}"}},' for _, div, sw, c, b in STYLES]
    return ("typedef struct { uint8_t div; float swing; uint8_t len; const char *chord, *bass; } style_t;\n"
            f"#define NSTYLE {len(STYLES)}\nstatic const style_t STYLES[NSTYLE] = {{\n" + "\n".join(rows) + "\n};")
