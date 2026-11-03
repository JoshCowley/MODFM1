// ================================================================ 1. catalogue, patches, state
const GATES = ["NOTE", "GRID", "1/16", "1/8", "1/4", "BAR"];
const n = (l, def = 0.5, o = {}) => ({ l, t: "num", def, ...o });          // 0..1
const b = (l, def = 0) => ({ l, t: "bip", def });                           // -1..1
const L = (l, opts, def = 0) => ({ l, t: "list", opts, def, mod: false });
const I = (l, min, max, def) => ({ l, t: "int", min, max, def, mod: false });
const PLAITS_MODELS = ["VA VCF", "PHASE D", "6OP FM 1", "6OP FM 2", "6OP FM 3", "TERRAIN", "STR MACH", "CHIPTUNE", "PAIR VA", "WAVESHP", "2OP FM", "FORMANT", "ADDITIVE", "WAVETBL", "CHORDS", "SPEECH", "SWARM", "NOISE", "PARTICLE", "STRING", "MODAL", "BASS DR", "SNARE", "HI-HAT"];
const BRAIDS_MODELS = ["CSAW", "/\\-_", "//-_", "FOLD", "UUUU", "SUB-", "SUB/", "SYN-", "SYN/", "//x3", "-_x3", "/\\x3", "SIx3", "RING", "////", "//uu", "TOY*", "ZLPF", "ZPKF", "ZBPF", "ZHPF", "VOSM", "VOWL", "VFOF", "HARM", "FM", "FBFM", "WTFM", "PLUK", "BOWD", "BLOW", "FLUT", "BELL", "DRUM", "KICK", "CYMB", "SNAR", "WTBL", "WMAP", "WLIN", "WTx4", "NOIS", "TWLN", "CLKN", "CLOU", "PRTC", "QPSK"];

// cpu: units of the FM-1 budget, per voice when poly. The ratios are measured (each module timed in rack.wasm,
//      1.9); the size of the budget is an assumption, anchored so one Plaits voice costs 9 units.
// ram: KB the module needs once; ramV: KB more per voice. These are the engine's real buffer sizes on a
//      32-bit chip, with the delay, chorus and reverb lines stored as 16-bit (as a port would).
const MODULES = {
  PLAITS:   { kind: "src", poly: true, cpu: 9, ram: 0, ramV: 25, blurb: "MACRO OSC · 24 MODELS", pages: [[L("MODEL", PLAITS_MODELS, 8), n("HARMO", .42), n("TIMBRE", .6), n("MORPH", .31)], [L("LPG", ["OFF", "ON"]), n("DECAY", .5), n("COLOUR", .5), n("LEVEL", .8)], [I("OCTAVE", -3, 3, 0), b("FINE"), b("ENV>TIMB"), b("ENV>MRPH")], [b("ENV>FM"), n("AUX MIX", 0)]] },
  BRAIDS:   { kind: "src", poly: true, cpu: 5, ram: 0, ramV: 17, blurb: "47 SHAPES · BIT CRUSH", pages: [[L("MODEL", BRAIDS_MODELS), n("TIMBRE", .5), n("COLOR", .5), n("DRIFT", .1)], [L("BITS", ["2", "3", "4", "6", "8", "12", "16"], 6), L("RATE", ["4K", "8K", "16K", "22K", "32K", "48K"], 5), n("SIGN", 0), n("LEVEL", .8)], [I("OCTAVE", -3, 3, 0), b("FINE"), n("AD ATK", .05), n("AD DEC", .4)], [b("AD>TIMB"), b("AD>COLR"), b("AD>PTCH"), L("AD VCA", ["OFF", "ON"])]] },
  ELEMENTS: { kind: "src", poly: true, cpu: 50, ram: 0, ramV: 175, blurb: "BOW BLOW STRIKE VOICE", pages: [[n("BOW", .3), n("BLOW", 0), n("STRIKE", .6), n("CONTOUR", .5)], [n("GEOMETRY", .4), n("BRIGHT", .6), n("DAMPING", .4), n("POSITION", .3)], [n("FLOW", .5), n("MALLET", .5), n("BOW TMB", .5), n("SPACE", .4)], [I("OCTAVE", -3, 3, 0), b("FINE"), b("BLOW TMB"), b("STRK TMB")]] },
  RINGS:    { kind: "shp", poly: true, cpu: 32, ram: 0, ramV: 172, blurb: "RESONATOR · 6 MODELS", pages: [[L("MODEL", ["MODAL", "SYMPATH", "STRING", "FM VOICE", "SYM QNT", "STR+VERB"]), n("STRUCT", .5), n("BRIGHT", .7), n("DAMPING", .4)], [n("POSITION", .3), n("EXCITE", .6), n("MIX", .7), n("SPREAD", .3)], [I("TUNE", -24, 24, 0), b("FINE"), L("INT EXC", ["OFF", "ON"]), I("CHORD", 0, 10, 0)]] },
  WARPS:    { kind: "shp", poly: true, cpu: 14, ram: 0, ramV: 71, blurb: "CROSS-MOD · VOCODER", pages: [[n("ALGO", .3), n("TIMBRE", .5), L("CARRIER", ["SINE", "TRI", "SAW"], 2), L("MOD IN", ["OSC", "USB IN", "NOISE"])], [n("CAR LVL", .7), n("MOD LVL", .7), n("MIX", 1), I("CAR TUNE", -24, 24, 0)]] },
  FILTER:   { kind: "shp", poly: true, cpu: 5, ram: 0, ramV: 1, blurb: "SVF · LP BP HP", pages: [[L("TYPE", ["LP", "BP", "HP", "LP24", "NOTCH", "PEAK"]), n("CUTOFF", .55), n("RESO", .3), n("DRIVE", .2)], [b("ENV AMT", .3), n("KEY TRK", .5), b("VEL AMT"), L("ENV SRC", ["AMP", "OWN", "MOD"])], [n("F ATK", .01), n("F DEC", .4), n("F SUS", .3), n("F REL", .35)], [L("SAT", ["SOFT", "HARD", "FOLD", "ASYM"]), n("AUDIO FM", 0), b("GAIN")]] },
  PEAKS:    { kind: "mod", poly: false, cpu: 3, ram: 1, ramV: 0, blurb: "ENVELOPE · LFO · TAP", outs: ["PEAKS"], pages: [[L("FUNC", ["ENV", "LFO", "TAP"]), n("K1", 0), n("K2", .7), L("GATE", GATES)], [n("SUSTAIN", 0), L("INVERT", ["OFF", "ON"])]] },
  TIDES:    { kind: "mod", poly: false, cpu: 2, ram: 1, ramV: 0, blurb: "SLOPE GENERATOR", outs: ["TIDES", "TIDE+"], pages: [[L("MODE", ["AD", "LOOP", "AR"], 1), n("FREQ", .3), n("SHAPE", .5), b("SLOPE", -.3)], [b("SMOOTH", .3), L("RANGE", ["LO", "MID", "HI"]), L("GATE", GATES, 4), L("INVERT", ["OFF", "ON"])]] },
  MARBLES:  { kind: "mod", poly: false, cpu: 3, ram: 8, ramV: 0, blurb: "RANDOM NOTES · GATES", outs: ["MARB X", "MARB T"], pages: [[n("RATE", .5), n("SPREAD", .4), n("BIAS", .5), n("STEPS", .7)], [n("DEJA VU", .5), L("LENGTH", ["1", "2", "3", "4", "5", "6", "8", "16"], 6), n("JITTER", .1), L("CLOCK", ["FREE", "1/16", "1/8", "1/4"], 2)], [L("SCALE", ["MAJOR", "MINOR", "PENTA", "PELOG", "BHAIRAV", "SHRI"]), L("T MODEL", ["COMPLEM", "CLUSTER", "DRUMS", "INDEP", "DIVIDER", "3 STATE", "MARKOV"]), L("X RANGE", ["2V", "5V", "±5V"]), b("GATE LEN")]] },
  GRIDS:    { kind: "mod", poly: false, cpu: 1, ram: 1, ramV: 0, blurb: "TOPOGRAPHIC DRUM MAP", outs: ["GRID", "ACCENT"], pages: [[n("X", .4), n("Y", .8), n("FILL", .8), L("OUT", ["BD", "SD", "HH", "KIT"], 3)], [n("CHAOS", 0), b("DECAY")]] },
  CLOUDS:   { kind: "fx", poly: false, cpu: 35, ram: 188, ramV: 0, blurb: "GRANULAR TEXTURES", pages: [[n("POSITION", .2), n("SIZE", .6), b("PITCH"), n("DENSITY", .5)], [n("TEXTURE", .4), n("BLEND", .3), n("SPREAD", .5), n("FEEDBACK", .2)], [n("REVERB", .3), L("MODE", ["GRANULR", "STRETCH", "LOOPDLY", "SPECTRL"]), L("FREEZE", ["OFF", "ON"]), L("QUALITY", ["16 ST", "16 MONO", "8 ST", "8 MONO"])], [b("IN GAIN"), L("NOTE TRG", ["OFF", "ON"]), L("PITCH TRK", ["OFF", "ON"]), L("GRN SYNC", ["OFF", "1/16", "1/8", "1/4", "1/2"])], [n("SPRAY", 0), n("PITCH RND", 0), L("PITCH QNT", ["FREE", "SEMI", "5TH+OCT", "OCT"]), n("SIZE RND", 0)], [b("SCAN"), b("TONE")]] },
  DELAY:    { kind: "fx", poly: false, cpu: 4, ram: 188, ramV: 0, blurb: "PING · MONO · TAPE", pages: [[L("TIME", ["1/16", "1/8", "1/8D", "1/4", "1/2", "1/8T", "1/4T", "1/4D", "1/2D"], 2), n("FEEDBACK", .4), n("TONE", .5), n("MIX", .3)], [L("MODE", ["PING", "MONO", "TAPE"]), n("WOW", .4), b("WIDTH"), n("DUCK", 0)], [n("LO CUT", 0), L("SYNC", ["TEMPO", "FREE"]), n("FREE MS", .5)]] },
  REVERB:   { kind: "fx", poly: false, cpu: 5, ram: 40, ramV: 0, blurb: "PLATE · HALL · SHIMMER", pages: [[n("SIZE", .6), n("DAMP", .5), n("DIFFUSE", .55), n("MIX", .3)], [L("TYPE", ["PLATE", "HALL", "SHIMMER"]), n("PRE", 0), n("SHIMMER", .4), n("LO CUT", 0)], [b("WIDTH"), n("DUCK", 0)]] },
  CHORUS:   { kind: "fx", poly: false, cpu: 5, ram: 16, ramV: 0, blurb: "STEREO CHORUS", pages: [[n("RATE", .3), n("DEPTH", .5), n("WIDTH", .7), n("MIX", .4)], [L("MODE", ["CHORUS", "FLANGER", "VIBRATO"]), n("FEEDBACK", 0), b("DELAY")]] },
};
for (const m of Object.values(MODULES)) if (m.kind === "fx") m.pages.flat().forEach((p) => { p.mod = true; });
const VOICE_MODES = ["POLY", "MONO", "LEGATO", "UNI 2", "UNI 4"];
const AMP = { kind: "amp", poly: true, cpu: 1, ram: 0, pages: [[n("ATTACK", .02), n("DECAY", .4), n("SUSTAIN", .7), n("RELEASE", .35)], [n("VEL AMT", .6), n("GLIDE", 0), n("SPREAD", .3), n("LEVEL", .8)], [n("M ATTACK", .05), n("M DECAY", .4), L("M LOOP", ["OFF", "ON"])], [L("MODE", VOICE_MODES), n("DETUNE", .15, { mod: false }), n("VINTAGE", 0, { mod: false })], [n("DELAY", 0), n("HOLD", 0), b("A CURVE"), b("D CURVE")], [b("VEL>ATK"), b("KEY>TIME"), n("M SUST", 0), b("PITCH ENV")]] };
const A_MODE = 11, A_DETUNE = 12, A_VINTAGE = 13;
const LFO_SHAPES = ["SINE", "TRI", "SAW", "SQUARE", "S&H", "DRIFT"], LFO_SYNC = ["FREE", "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS"];
const lfoPage = (i, shape, rate, sync) => [L(`SHAPE ${i}`, LFO_SHAPES, shape), n(`RATE ${i}`, rate), L(`SYNC ${i}`, LFO_SYNC, sync), L(`RETRIG ${i}`, ["OFF", "ON"])];
const lfoMore = (i) => [n(`FADE ${i}`, 0), n(`PHASE ${i}`, 0), L(`POLAR ${i}`, ["BI", "UNI"]), L(`1SHOT ${i}`, ["OFF", "ON"])];
// the extra pages go after the first three so every older patch, lock and matrix row keeps its parameter numbers
const LFOS = { kind: "lfo", pages: [lfoPage(1, 0, .35, 0), lfoPage(2, 1, .2, 5), lfoPage(3, 5, .5, 0), lfoMore(1), lfoMore(2), lfoMore(3)] };
const ANIM_MODES = ["FULL", "CALM", "STILL"];
const reducedMotion = matchMedia("(prefers-reduced-motion: reduce)").matches;
const KEYS12 = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
// tunings: [name, cents of each degree from the root, notes per period]; 12-note ones keep the keyboard's layout
const TUNINGS = [
  ["EQUAL", [0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100]],
  ["JUST", [0, 111.73, 203.91, 315.64, 386.31, 498.04, 590.22, 701.96, 813.69, 884.36, 1017.6, 1088.27]],
  ["PYTHAG", [0, 90.22, 203.91, 294.13, 407.82, 498.04, 611.73, 701.96, 792.18, 905.87, 996.09, 1109.78]],
  ["MEANTONE", [0, 76.05, 193.16, 310.26, 386.31, 503.42, 579.47, 696.58, 772.63, 889.74, 1006.84, 1082.89]],
  ["WERCK III", [0, 90.22, 192.18, 294.13, 390.22, 498.04, 588.27, 696.09, 792.18, 888.27, 996.09, 1092.18]],
  ["KIRNB III", [0, 90.22, 193.16, 294.13, 386.31, 498.04, 590.22, 696.58, 792.18, 889.74, 996.09, 1088.27]],
  ["RAST", [0, 100, 200, 300, 350, 500, 600, 700, 800, 900, 1000, 1050]],
  ["BAYATI", [0, 150, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100]],
  ["SLENDRO", [0, 240, 480, 720, 960]],
  ["PELOG", [0, 120, 270, 540, 670, 785, 950]],
  ["19-EDO", Array.from({ length: 19 }, (_, i) => i * 1200 / 19)],
  ["24-EDO", Array.from({ length: 24 }, (_, i) => i * 50)],
  ["31-EDO", Array.from({ length: 31 }, (_, i) => i * 1200 / 31)],
];
const GLOBAL = { kind: "glo", pages: [[I("TEMPO", 60, 180, 112), L("MIDI CH", ["OMNI", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"], 0), L("VOICES", ["AUTO", "1", "2", "3", "4", "5", "6"]), L("CLOCK", ["INT", "MIDI"])], [n("VEL CURVE", .5), L("ANIMATION", ANIM_MODES, reducedMotion ? 1 : 0), n("LED BRIGHT", .8)], [L("TUNING", TUNINGS.map((t) => t[0])), L("T ROOT", KEYS12), I("A4 HZ", 400, 480, 440), I("BEND", 0, 24, 2)], [L("PROG CHG", ["OFF", "ON"], 1), L("MIDI OUT", ["OFF", "NOTES", "+CC"]), L("OUT CH", ["1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"]), L("LOCAL", ["ON", "OFF"])], [b("LOW EQ", 0), b("MID EQ", 0), b("HIGH EQ", 0), n("COMP", 0)], [L("STEP ADV", ["ON", "OFF"])], [L("TARGET", ["FM-1", "BROWSER"]), L("FREE RAM", ["256K", "384K", "512K"], 1), L("CPU SPEED", ["SLOW", "MID", "FAST"], 1)]] };
const G_TEMPO = 0, G_MIDICH = 1, G_VOICES = 2, G_CLOCK = 3, G_VEL = 4, G_ANIM = 5, G_LED = 6, G_TUNE = 7, G_TROOT = 8, G_A4 = 9, G_BEND = 10, G_PROG = 11, G_MOUT = 12, G_OUTCH = 13, G_LOCAL = 14, G_LOW = 15, G_MID = 16, G_HIGH = 17, G_COMP = 18, G_ADV = 19, G_TARGET = 20, G_RAM = 21, G_CPU = 22;
const SCALES = { MAJOR: [0, 2, 4, 5, 7, 9, 11], MINOR: [0, 2, 3, 5, 7, 8, 10], DORIAN: [0, 2, 3, 5, 7, 9, 10], MIXOLYD: [0, 2, 4, 5, 7, 9, 10], "PENT MAJ": [0, 2, 4, 7, 9], "PENT MIN": [0, 3, 5, 7, 10], "HARM MIN": [0, 2, 3, 5, 7, 8, 11], WHOLE: [0, 2, 4, 6, 8, 10], LYDIAN: [0, 2, 4, 6, 7, 9, 11] };
// SEL (CHORD): scale, root, chords and voicing on page 1; how the keys follow the scale on page 2.
// After ChordStep FM-1 (one key per chord, the lowest keys force the quality and add extensions) and Melodee's CHRD/VOIC/QNT.
const CHORD_TYPES = ["OFF", "TRIAD", "7TH", "9TH", "11TH", "SUS4", "6TH", "POWER"];
const VOICINGS = ["CLOSE", "LEAD", "OPEN", "INV1", "INV2", "+OCT"];
const QNT_MODES = ["OFF", "SNAP", "ALL", "WHITE"];
const CHORD = { kind: "chd", pages: [[L("SCALE", Object.keys(SCALES)), L("ROOT", KEYS12), L("CHORD", CHORD_TYPES), L("VOICING", VOICINGS)], [L("QNT", QNT_MODES), L("HARMONY", ["KEY", "FREE"]), L("ZONE", ["ON", "OFF"]), n("STRUM", 0)]] };
const C_SCALE = 0, C_KEY = 1, C_TYPE = 2, C_VOICE = 3, C_QNT = 4, C_HARM = 5, C_ZONE = 6, C_STRUM = 7;
// SEQ key: the sequencer, after Clipper / SLOOP 2.7.2: up to 64 steps, each with up to 6 notes of their own length and force,
// a ratchet, a nudge, a fill condition, a slide and parameter locks
const SEQ_DIVS = ["1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/2", "1BAR", "2BAR", "4BAR"], DIV_16THS = [4, 2, 1, .5, 4 / 3, 2 / 3, 8, 16, 32, 64];
const LEVELS = ["GHOST", "SOFT", "NORM", "HARD", "ACC"], LEVEL_VEL = [.32, .55, null, .9, 1];
const FILLS = ["—", "FILL", "NO FILL"];
const MAXSTEPS = 64;
const blankStep = () => ({ notes: [], ratchet: 1, nudge: 0, locks: {}, fill: 0, slide: false, chance: 1 });
const blankSeq = () => ({ len: 16, div: 2, swing: 0, gate: .5, spice: 0, dice: 0, steps: Array.from({ length: MAXSTEPS }, blankStep) });
// earlier formats: 1.1 / 1.2 patterns had 16 steps of { trig, notes: [numbers], vel, len (index), chance, ratchet, nudge, locks }
const OLD_LEN_F = [.125, .25, .5, .75, .97, 1], OLD_DIV = [1, 2, 5, 3];
function upgradeSeq(sq) {
  if (!sq) return blankSeq();
  if (sq.p) {
    const out = blankSeq(); out.len = sq.p[1] || 16; out.div = OLD_DIV[sq.p[2]] ?? 2; out.swing = sq.p[3] || 0; out.spice = sq.spice || 0; out.dice = sq.dice || 0;
    (sq.steps || []).forEach((x, i) => {
      if (i >= MAXSTEPS || !x) return;
      const st = out.steps[i];
      st.locks = x.trig ? x.locks || {} : {}; st.ratchet = x.ratchet || 1; st.nudge = Math.round((x.nudge || 0) * 32); st.chance = x.chance ?? 1;
      if (x.trig && x.notes && x.notes.length) st.notes = x.notes.map((nn) => ({ n: nn, len: OLD_LEN_F[x.len ?? 2] ?? .5, lvl: 2, vel: x.vel ?? .75 }));
    });
    tieLengths(out, sq.steps || [], (x) => x && x.trig && x.len === 5);
    return out;
  }
  while (sq.steps.length < MAXSTEPS) sq.steps.push(blankStep());
  for (const st of sq.steps) { st.locks = st.locks || {}; st.notes = st.notes || []; }
  return sq;
}
// a tied step holds on until the next step that plays
function tieLengths(out, src, isTie) {
  const L = out.len;
  for (let i = 0; i < L; i++) if (isTie(src[i])) {
    let j = 1; while (j < L && !(out.steps[(i + j) % L].notes.length)) j++;
    for (const nt of out.steps[i].notes) nt.len = Math.min(j, MAXSTEPS);
  }
}const MAXV = 6;
// FM-1 preview (1.9). Nobody has published the FM-1's free RAM or how fast the Mutable code runs on its pi32v2 core,
// so both are settings (GLOBAL > HARDWARE). FREE RAM is what's left for sound: about 578 KB of SRAM (unverified)
// less firmware, a 240x240 screen buffer and audio buffers. BASE_* is the engine itself (voices, matrix, LFOs, master).
const RAM_OPTS = [256, 384, 512], CPU_OPTS = [60, 80, 110], BASE_CPU = 12, BASE_RAM = 16;
const hwOn = () => glo.p[G_TARGET] === 0;
const hwPool = () => RAM_OPTS[glo.p[G_RAM] ?? 1];
const hwBudget = () => CPU_OPTS[glo.p[G_CPU] ?? 1];

const SLOTS = [
  { id: "SRC", kind: "src", name: "SRC", choices: ["PLAITS", "BRAIDS", "ELEMENTS"] },
  { id: "SHP", kind: "shp", name: "SHP", choices: ["RINGS", "WARPS", "FILTER", null] },
  { id: "MODA", kind: "mod", name: "MOD A", choices: ["PEAKS", "TIDES", "MARBLES", "GRIDS", null] },
  { id: "MODB", kind: "mod", name: "MOD B", choices: ["PEAKS", "TIDES", "MARBLES", "GRIDS", null] },
  { id: "FX1", kind: "fx", name: "FX 1", choices: ["CLOUDS", "DELAY", "REVERB", "CHORUS", null] },
  { id: "FX2", kind: "fx", name: "FX 2", choices: ["CLOUDS", "DELAY", "REVERB", "CHORUS", null] },
];
const KC = { src: "#ffb347", shp: "#4fd1c5", mod: "#b48cff", fx: "#6aa9ff", amp: "#ff7a6b", glo: "#d8d7d3", lfo: "#c9a7ff", chd: "#9be37a", seq: "#ff8fc8", mx: "#ffd27a", arp: "#7fd6ff", tool: "#f2c14e" };

// ---------------------------------------------------------------- state
const blankParams = (mod) => mod ? mod.pages.flat().map((p) => p.def) : [];
const rack = {};                    // slot id -> { mod: name|null, p: [] }
const amp = { p: blankParams(AMP) };
const glo = { p: blankParams(GLOBAL) };
const chd = { p: blankParams(CHORD) };
// 1.6, after Melodee: eight pattern banks (A-H) and a song of 16 rows (bank, repeats, transpose)
const BANK_NAMES = ["A", "B", "C", "D", "E", "F", "G", "H"];
let banks = Array.from({ length: 8 }, blankSeq), bankIx = 0;
let seq = banks[0];
const blankSong = () => ({ rows: Array.from({ length: 16 }, () => ({ bank: -1, rep: 1, trn: 0 })) });
let song = blankSong();
function useBank(i) { bankIx = i; seq = banks[i]; }
const lfo = { p: blankParams(LFOS) };
const BUILT_SRC = ["—", "M1", "M2", "M3", "M4", "VELO", "KEY TRK", "RANDOM", "AMP ENV"];
const EXTRA_SRC = ["LFO 1", "LFO 2", "LFO 3", "MOD ENV", "SEQ 1", "SEQ 2", "SEQ 3", "SEQ 4", "WHEEL", "PRESS"];
let matrix = [];
// each LFO's two direct targets (1.5): LFO 1 A, LFO 1 B, LFO 2 A, ... sent to the engine as routes 12-17
const blankLfoTgt = () => Array.from({ length: 6 }, () => ({ dst: "—", amt: 0 }));
let lfoTgt = blankLfoTgt();
let macros = [0.5, 0.5, 0.5, 0.5];
try { const sg = JSON.parse(localStorage.getItem("fm1rack.global.v1") || "null"); if (sg && sg.length <= glo.p.length) sg.forEach((v, i) => { glo.p[i] = v; }); } catch { /* defaults */ }
function storeGlobal() { try { localStorage.setItem("fm1rack.global.v1", JSON.stringify(glo.p)); } catch { /* this visit only */ } }
try { const sc = JSON.parse(localStorage.getItem("fm1rack.chord.v2") || "null"); if (sc && sc.length === chd.p.length) chd.p = sc; } catch { /* defaults */ }
function storeChord() { try { localStorage.setItem("fm1rack.chord.v2", JSON.stringify(chd.p)); } catch { /* this visit only */ } }

const PRESETS = /*PRESETS*/[];
let presetIx = 0, patchName = PRESETS[0].name;
// 64 user slots, 4 pages of 16. Saved compactly: empty banks and steps are left out, and the duplicate pattern is dropped.
const USER_SLOTS = 64, STORE_KEY = "fm1rack.user.v2", OLD_STORE_KEY = "fm1rack.user.v1";
const CATS = ["—", "BS", "PD", "LD", "CH", "FX"], CAT_NAMES = ["NONE", "BASS", "PADS", "LEAD", "CHORD", "FX"], CAT_COL = ["#7d7a72", "#ffb347", "#6aa9ff", "#ff8fc8", "#9be37a", "#b48cff"];
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
function packSnap(u) {
  const o = { ...u, fmt: 2 }; delete o.seq;
  o.banks = (u.banks || []).map((b) => {
    if (!b || same(b, blankSeq())) return null;
    const blank = blankStep(), sp = [];
    b.steps.forEach((st, i) => { if (!same(st, blank)) sp.push([i, st]); });
    const out = { ...b, sp }; delete out.steps; return out;
  });
  while (o.banks.length && o.banks[o.banks.length - 1] === null) o.banks.pop();
  if (same(o.song, blankSong())) delete o.song;
  if (typeof blankMseq === "function" && same(o.mseq, blankMseq())) delete o.mseq;
  if (same(o.lfoTgt, blankLfoTgt())) delete o.lfoTgt;
  return o;
}
function unpackSnap(o) {
  if (!o || o.fmt !== 2) return o;
  const u = { ...o };
  u.banks = Array.from({ length: 8 }, (_, i) => {
    const b = o.banks[i]; if (!b) return null;
    const sq = { ...b, steps: Array.from({ length: MAXSTEPS }, blankStep) }; delete sq.sp;
    for (const [i2, st] of b.sp) sq.steps[i2] = { ...blankStep(), ...st };
    return sq;
  });
  return u;
}
let userPatches = (() => {
  try {
    let v = JSON.parse(localStorage.getItem(STORE_KEY) || "null");
    if (!v) v = JSON.parse(localStorage.getItem(OLD_STORE_KEY) || "[]");   // 1.7 and earlier: 16 slots, kept as U01–U16
    return Array.from({ length: USER_SLOTS }, (_, i) => unpackSnap(v[i]) || null);
  } catch { return Array(USER_SLOTS).fill(null); }
})();
function storeUser() { try { localStorage.setItem(STORE_KEY, JSON.stringify(userPatches.map((p) => p && packSnap(p)))); return true; } catch { return false; } }
const slotName = (i) => `U${String(i + 1).padStart(2, "0")}`;
// a patch's category: chosen when saved, or read from the name's prefix (BS, PD, LD, CH, FX)
function catOfName(nm) { const k = CATS.indexOf(String(nm).replace(/^U\d\d /, "").slice(0, 2)); return k > 0 && /^.. /.test(String(nm).replace(/^U\d\d /, "")) ? k : 0; }
// would this preset fit on an FM-1 (GLOBAL > HARDWARE)? Used to mark the library.
function entryFit(e) {
  if (e.u !== undefined) { const u = userPatches[e.u]; if (!u) return null; return budgetFor(Object.fromEntries(SLOTS.map((s) => [s.id, u.slots[s.id] ? u.slots[s.id].mod : null])), u.amp[A_MODE]); }
  const pr = PRESETS[e.f], md = pr.amp && pr.amp.MODE !== undefined ? Math.max(0, VOICE_MODES.indexOf(String(pr.amp.MODE))) : 0;
  return budgetFor(Object.fromEntries(SLOTS.map((s) => [s.id, pr.slots[s.id] || null])), md);
}
function entryCat(e) { if (e.u !== undefined) { const p = userPatches[e.u]; return p && p.cat ? p.cat : catOfName(p ? p.name : ""); } return catOfName(PRESETS[e.f].name); }
// the PRESETS encoder walks the factory patches, then the user slots that hold something
function patchList(raw = false) { const all = [...PRESETS.map((p, i) => ({ f: i, name: p.name })), ...userPatches.map((p, i) => p && { u: i, name: `${slotName(i)} ${p.name}` }).filter(Boolean)]; if (raw || typeof filterList !== "function") return all; const f = filterList(all); return f.length ? f : all; }
function fitParams(def, arr) { const d = blankParams(def); return d.map((v, i) => (arr && arr[i] !== undefined ? arr[i] : v)); }
function loadPreset(i) { loadEntry(patchList()[i] || { f: 0, name: PRESETS[0].name }); }
function loadEntry(entry) {
  patchName = entry.name;
  if (entry.u !== undefined) { restoreSnap(userPatches[entry.u]); histReset(`LOADED ${patchName}`); return; }
  const pr = PRESETS[entry.f];
  for (const s of SLOTS) { const m = pr.slots[s.id]; rack[s.id] = { mod: m, p: blankParams(MODULES[m]) }; if (m) applyOver(MODULES[m], rack[s.id].p, pr.p && pr.p[s.id]); }
  amp.p = blankParams(AMP); applyOver(AMP, amp.p, pr.amp);
  lfo.p = blankParams(LFOS); applyOver(LFOS, lfo.p, pr.lfo);
  matrix = Array.from({ length: 12 }, (_, k) => {
    const r = pr.matrix[k];
    return r ? { src: r[0], dst: r[1], amt: r[2], via: r[3] || "—" } : { src: "—", dst: "—", amt: 0, via: "—" };
  });
  macros = [...(pr.macros || [0.5, 0.5, 0.5, 0.5])];
  lfoTgt = blankLfoTgt();
  banks = Array.from({ length: 8 }, blankSeq); song = blankSong(); bankIx = 0;
  loadSeq(factorySeq(pr.seq));
  mseq = blankMseq();
  histReset(`LOADED ${patchName}`);
}
function restoreSnap(u) {
  for (const s of SLOTS) { const st = u.slots[s.id] || { mod: null, p: [] }; rack[s.id] = { mod: st.mod, p: fitParams(MODULES[st.mod], st.p) }; }
  amp.p = fitParams(AMP, u.amp); lfo.p = fitParams(LFOS, u.lfo);
  matrix = u.matrix.map((r) => ({ ...r })); macros = [...u.macros];
  lfoTgt = u.lfoTgt ? u.lfoTgt.map((t) => ({ ...t })) : blankLfoTgt();
  banks = Array.from({ length: 8 }, (_, i) => (u.banks && u.banks[i] ? upgradeSeq(JSON.parse(JSON.stringify(u.banks[i]))) : blankSeq()));
  bankIx = u.banks ? u.bankIx || 0 : 0;
  if (!u.banks) banks[0] = upgradeSeq(u.seq ? JSON.parse(JSON.stringify(u.seq)) : blankSeq());   // patches before 1.6 had one pattern
  seq = banks[bankIx];
  song = u.song ? JSON.parse(JSON.stringify(u.song)) : blankSong();
  mseq = u.mseq ? JSON.parse(JSON.stringify(u.mseq)) : blankMseq();
}
// a new pattern keeps the transport running
function loadSeq(sq) { banks[bankIx] = upgradeSeq(sq); seq = banks[bankIx]; }
// factory patterns: { len, rate, swing, steps: [ { n: [notes], v, l: "1/2" | "TIE", c, lock: { "SHP.CUTOFF": 0.7 } } | null ] }
function factorySeq(f) {
  const sq = blankSeq(); if (!f) return sq;
  sq.len = f.len || 16; sq.div = Math.max(0, SEQ_DIVS.indexOf(f.rate === "1/16T" ? "16T" : f.rate || "1/16")); sq.swing = f.swing || 0;
  (f.steps || []).forEach((x, i) => {
    if (!x || i >= MAXSTEPS) return;
    const st = sq.steps[i], li = Math.max(0, ["1/8", "1/4", "1/2", "3/4", "FULL", "TIE"].indexOf(x.l || "1/2"));
    st.notes = (x.n || []).map((nn) => ({ n: nn, len: OLD_LEN_F[li], lvl: (x.v ?? .75) >= .95 ? 4 : 2, vel: x.v ?? .75 }));
    st.chance = x.c ?? 1;
    for (const [name, v] of Object.entries(x.lock || {})) { const ref = refByName(name); if (ref) st.locks[lockKey(ref)] = { ref, v }; }
  });
  tieLengths(sq, f.steps || [], (x) => x && x.l === "TIE");
  return sq;
}
// "SHP.CUTOFF", "AMP.DECAY", "LFO.RATE 1", "M2" to a knob reference
function refByName(name) {
  if (/^M[1-4]$/.test(name)) return { w: "macro", i: +name[1] - 1 };
  const [head, label] = [name.slice(0, name.indexOf(".")), name.slice(name.indexOf(".") + 1)];
  if (head === "AMP") { const j = AMP.pages.flat().findIndex((p) => p.l === label); return j < 0 ? null : { w: "AMP", idx: j }; }
  if (head === "LFO") { const j = LFOS.pages.flat().findIndex((p) => p.l === label); return j < 0 ? null : { w: "LFOS", idx: j }; }
  const s = SLOTS.find((x) => x.name === head || x.id === head); if (!s) return null;
  const m = MODULES[rack[s.id].mod]; if (!m) return null;
  const j = m.pages.flat().findIndex((p) => p.l === label);
  return j < 0 ? null : { w: "slot", slot: s.id, mod: rack[s.id].mod, idx: j };
}
const lockKey = (r) => r.w === "macro" ? `m${r.i}` : r.w === "slot" ? `${r.slot}.${r.mod}.${r.idx}` : `${r.w}.${r.idx}`;
// factory patches name their values by label, and list values by option
function applyOver(def, values, over) {
  if (!over) return;
  const flat = def.pages.flat();
  for (const [label, v] of Object.entries(over)) {
    const j = flat.findIndex((p) => p.l === label);
    if (j < 0) continue;
    values[j] = flat[j].t === "list" ? Math.max(0, flat[j].opts.indexOf(String(v))) : v;
  }
}
function snapshot(name) {
  return { name, slots: Object.fromEntries(SLOTS.map((s) => [s.id, { mod: rack[s.id].mod, p: [...rack[s.id].p] }])),
           amp: [...amp.p], lfo: [...lfo.p], matrix: matrix.map((r) => ({ ...r })), lfoTgt: lfoTgt.map((t) => ({ ...t })), macros: [...macros], seq: JSON.parse(JSON.stringify(seq)), banks: JSON.parse(JSON.stringify(banks)), bankIx, song: JSON.parse(JSON.stringify(song)), mseq: JSON.parse(JSON.stringify(mseq)) };
}
const CHARS = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-#.";
let saveState = null;
// SAVE page 1 names the patch and picks a slot and category; page 2 is the library of every preset
function openSave() {
  const base = patchName.replace(/^U\d\d /, "").slice(0, 10).padEnd(10, " ");
  const cur = typeof currentEntry === "function" ? currentEntry() : null;
  const free = userPatches.findIndex((p) => !p);
  saveState = { name: base.split(""), cursor: 0, slot: free < 0 ? 0 : free, cat: cur ? entryCat(cur) : catOfName(patchName), t0: animT, view: "NAME", rename: -1,
                lib: { tab: 0, ix: 0, act: 0, arm: -1 } };
  page = "SAVE";
}
function commitSave() {
  const st = saveState, name = st.name.join("").trim() || "UNTITLED";
  if (st.rename >= 0) {   // renaming keeps the sound
    const p = userPatches[st.rename]; const was = p.name; p.name = name; p.cat = st.cat;
    if (patchName === `${slotName(st.rename)} ${was}`) patchName = `${slotName(st.rename)} ${name}`;
    const ok = storeUser(); say(ok ? `RENAMED ${slotName(st.rename)}` : "RENAMED FOR THIS VISIT ONLY", "SAVE");
    st.rename = -1; st.view = "LIB"; return;
  }
  userPatches[st.slot] = { ...snapshot(name), cat: st.cat };
  const ok = storeUser();
  const list = patchList();
  presetIx = list.findIndex((e) => e.u === st.slot);
  if (presetIx < 0) { lib.filter = 0; storeLib(); presetIx = patchList().findIndex((e) => e.u === st.slot); }
  patchName = patchList()[presetIx].name;
  histReset(`SAVED ${name}`);
  say(ok ? `SAVED TO ${slotName(st.slot)}` : "STORAGE FULL: SAVED FOR THIS VISIT ONLY", "SAVE");
  saveState = null; page = "PERFORM";
}
// the library: every factory and user preset, in category tabs
const LIB_TABS = ["ALL", "USER", "BASS", "PADS", "LEAD", "CHORD", "FX", "OTHER"];
const LIB_ACTS = ["LOAD", "COPY", "RENAME", "DELETE"];
function libList() {
  const t = LIB_TABS[saveState.lib.tab], all = patchList(true);
  if (t === "ALL") return all;
  if (t === "USER") return all.filter((e) => e.u !== undefined);
  if (t === "OTHER") return all.filter((e) => entryCat(e) === 0);
  return all.filter((e) => CAT_NAMES[entryCat(e)] === t);
}
function libEntry() { const l = libList(); const L = saveState.lib; L.ix = clamp(L.ix, 0, Math.max(0, l.length - 1)); return l[L.ix]; }
function libSelectLoaded(e) {
  let ix = patchList().findIndex((x) => (e.u !== undefined ? x.u === e.u : x.f === e.f));
  if (ix < 0) { lib.filter = 0; storeLib(); ix = patchList().findIndex((x) => (e.u !== undefined ? x.u === e.u : x.f === e.f)); }
  presetIx = Math.max(0, ix);
}
function libDo() {
  const L = saveState.lib, e = libEntry(), act = LIB_ACTS[L.act];
  if (!e) { say("NOTHING HERE", "SAVE"); return; }
  if (act !== "DELETE") L.arm = -1;
  if (act === "LOAD") {
    loadEntry(e); libSelectLoaded(e); engine(["all_off"]); sounding.clear(); refreshKeys();
    if (glo.p[G_MOUT] === 2) midiSend([0xC0 | (glo.p[G_OUTCH] & 15), presetIx & 127]);
    say(patchName, "HOME"); return;
  }
  if (act === "COPY") {
    const free = userPatches.findIndex((p) => !p);
    if (free < 0) { say("ALL 64 SLOTS ARE FULL", "SAVE"); return; }
    let snap;
    if (e.u !== undefined) snap = JSON.parse(JSON.stringify(userPatches[e.u]));
    else { loadEntry(e); libSelectLoaded(e); engine(["all_off"]); sounding.clear(); snap = { ...snapshot(PRESETS[e.f].name.slice(0, 10)), cat: entryCat(e) }; }   // a factory copy loads it first
    userPatches[free] = snap;
    const ok = storeUser(); say(ok ? `COPIED TO ${slotName(free)}` : "STORAGE FULL: COPY FOR THIS VISIT ONLY", "SAVE"); return;
  }
  if (e.u === undefined) { say("FACTORY: COPY IT FIRST", "SAVE"); return; }
  if (act === "RENAME") {
    const p = userPatches[e.u];
    saveState.name = p.name.slice(0, 10).padEnd(10, " ").split(""); saveState.cursor = 0; saveState.slot = e.u; saveState.cat = entryCat(e);
    saveState.rename = e.u; saveState.view = "NAME"; say(`RENAME ${slotName(e.u)}: SAVE TO KEEP`, "SAVE"); return;
  }
  if (L.arm !== e.u) { L.arm = e.u; say(`OCT+ AGAIN TO DELETE ${slotName(e.u)}`, "SAVE"); return; }
  const gone = userPatches[e.u].name; userPatches[e.u] = null; L.arm = -1;
  if (patchName === `${slotName(e.u)} ${gone}`) { const l = patchList(); presetIx = Math.min(presetIx, l.length - 1); }
  const ok = storeUser(); say(ok ? `DELETED ${slotName(e.u)}` : "DELETED FOR THIS VISIT ONLY", "SAVE");
}

// ---------------------------------------------------------------- budget
function budgetFor(ids, mode = amp.p[A_MODE]) {
  let shared = BASE_CPU, poly = AMP.cpu, ram = BASE_RAM, ramV = 0;
  for (const s of SLOTS) {
    const m = MODULES[ids[s.id]];
    if (!m) continue;
    if (m.poly) poly += m.cpu; else shared += m.cpu;
    ram += m.ram; ramV += m.ramV;
  }
  const cap = mode === 1 || mode === 2 ? 1 : glo.p[G_VOICES] ? glo.p[G_VOICES] : MAXV;
  if (!hwOn()) return { shared, poly, ram: ram + ramV * cap, ramV, voices: cap, cpu: shared + poly * cap, limit: "", over: "", hw: false };
  const B = hwBudget(), pool = hwPool();
  const byCpu = Math.floor((B - shared) / poly), byRam = ramV ? Math.floor((pool - ram) / ramV) : 99;
  const fit = Math.min(cap, byCpu, byRam);
  const limit = fit >= cap ? "" : byRam <= byCpu ? "RAM" : "CPU";
  const voices = Math.max(1, fit);   // a patch that doesn't fit still plays one voice, flagged as over
  const over = fit >= 1 ? "" : byRam < 1 ? "RAM" : "CPU";
  return { shared, poly, ram: ram + ramV * voices, ramV, voices, cpu: shared + poly * voices, limit, over, hw: true, B, pool };
}
const currentIds = () => Object.fromEntries(SLOTS.map((s) => [s.id, rack[s.id].mod]));
const budget = () => budgetFor(currentIds());
function fitOf(slotId, modName) {
  const ids = currentIds(); ids[slotId] = modName;
  const bg = budgetFor(ids);
  if (modName && bg.over && !budget().over) return { ok: false, why: bg.over, bg };   // a patch that's already over can still be trimmed
  if (modName && (slotId === "FX1" || slotId === "FX2")) { const other = slotId === "FX1" ? "FX2" : "FX1"; if (rack[other].mod === modName) return { ok: false, why: `IN ${other === "FX1" ? "FX 1" : "FX 2"}`, bg }; }
  return { ok: true, bg };
}

// sources and destinations
function sources() {
  const out = [...BUILT_SRC, ...EXTRA_SRC];
  for (const id of ["MODA", "MODB"]) { const m = MODULES[rack[id].mod]; if (m) for (const o of m.outs) out.push(`${id.slice(-1)} ${o}`); }
  return out;
}
function destinations() {
  const out = ["—", "PITCH", "PAN", ...AMP.pages.flat().filter((p) => p.mod !== false).map((p) => `AMP.${p.l}`), ...LFOS.pages.flat().filter((p) => p.mod !== false).map((p) => `LFO.${p.l}`)];
  for (const s of SLOTS) {
    const m = MODULES[rack[s.id].mod];
    if (m) for (const p of m.pages.flat()) if (p.mod !== false) out.push(`${s.name}.${p.l}`);
  }
  return out;
}

// ---------------------------------------------------------------- UI state
let page = "PERFORM";           // PERFORM | RACK | slot id | AMP | LFOS | MATRIX | CHORD | SEQ | STEP | GLOBAL | SAVE
let subpage = 0, mxRow = 0, octave = 0, master = 0.7;
let picker = null;              // { slot, ix }
let toast = null, toastIcon = null, toastT = 0;
let learn = false, learnMacro = 0;
let animT = 0;                  // animation clock in seconds (slows or stops with GLO > ANIMATION)
const phrase = { state: "idle", events: [], t0: 0, len: 0, start: 0, last: 0, bars: 0 };
const playKeys = new Set();
const held = new Set(), latched = new Set();
const say = (msg, ic = null) => { toast = msg; toastIcon = ic; toastT = performance.now(); draw(); };
