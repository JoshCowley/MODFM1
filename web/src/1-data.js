// ================================================================ 1. catalogue, patches, state
const GATES = ["NOTE", "GRID", "1/16", "1/8", "1/4", "BAR"];
const n = (l, def = 0.5, o = {}) => ({ l, t: "num", def, ...o });          // 0..1
const b = (l, def = 0) => ({ l, t: "bip", def });                           // -1..1
const L = (l, opts, def = 0) => ({ l, t: "list", opts, def, mod: false });
const I = (l, min, max, def) => ({ l, t: "int", min, max, def, mod: false });
const PLAITS_MODELS = ["VA VCF", "PHASE D", "6OP FM 1", "6OP FM 2", "6OP FM 3", "TERRAIN", "STR MACH", "CHIPTUNE", "PAIR VA", "WAVESHP", "2OP FM", "FORMANT", "ADDITIVE", "WAVETBL", "CHORDS", "SPEECH", "SWARM", "NOISE", "PARTICLE", "STRING", "MODAL", "BASS DR", "SNARE", "HI-HAT"];
const BRAIDS_MODELS = ["CSAW", "/\\-_", "//-_", "FOLD", "UUUU", "SUB-", "SUB/", "SYN-", "SYN/", "//x3", "-_x3", "/\\x3", "SIx3", "RING", "////", "//uu", "TOY*", "ZLPF", "ZPKF", "ZBPF", "ZHPF", "VOSM", "VOWL", "VFOF", "HARM", "FM", "FBFM", "WTFM", "PLUK", "BOWD", "BLOW", "FLUT", "BELL", "DRUM", "KICK", "CYMB", "SNAR", "WTBL", "WMAP", "WLIN", "WTx4", "NOIS", "TWLN", "CLKN", "CLOU", "PRTC", "QPSK"];

// cpu: est. units of an 80-unit budget (per voice when poly); ram: est. KB
const MODULES = {
  PLAITS:   { kind: "src", poly: true, cpu: 9, ram: 8, blurb: "MACRO OSC · 24 MODELS", pages: [[L("MODEL", PLAITS_MODELS, 8), n("HARMO", .42), n("TIMBRE", .6), n("MORPH", .31)], [L("LPG", ["OFF", "ON"]), n("DECAY", .5), n("COLOUR", .5), n("LEVEL", .8)], [I("OCTAVE", -3, 3, 0), b("FINE"), b("ENV>TIMB"), b("ENV>MRPH")], [b("ENV>FM"), n("AUX MIX", 0)]] },
  BRAIDS:   { kind: "src", poly: true, cpu: 4, ram: 2, blurb: "47 SHAPES · BIT CRUSH", pages: [[L("MODEL", BRAIDS_MODELS), n("TIMBRE", .5), n("COLOR", .5), n("DRIFT", .1)], [L("BITS", ["2", "3", "4", "6", "8", "12", "16"], 6), L("RATE", ["4K", "8K", "16K", "22K", "32K", "48K"], 5), n("SIGN", 0), n("LEVEL", .8)], [I("OCTAVE", -3, 3, 0), b("FINE"), n("AD ATK", .05), n("AD DEC", .4)], [b("AD>TIMB"), b("AD>COLR"), b("AD>PTCH"), L("AD VCA", ["OFF", "ON"])]] },
  ELEMENTS: { kind: "src", poly: true, cpu: 40, ram: 64, blurb: "BOW BLOW STRIKE VOICE", pages: [[n("BOW", .3), n("BLOW", 0), n("STRIKE", .6), n("CONTOUR", .5)], [n("GEOMETRY", .4), n("BRIGHT", .6), n("DAMPING", .4), n("POSITION", .3)], [n("FLOW", .5), n("MALLET", .5), n("BOW TMB", .5), n("SPACE", .4)], [I("OCTAVE", -3, 3, 0), b("FINE"), b("BLOW TMB"), b("STRK TMB")]] },
  RINGS:    { kind: "shp", poly: true, cpu: 14, ram: 40, blurb: "RESONATOR · 6 MODELS", pages: [[L("MODEL", ["MODAL", "SYMPATH", "STRING", "FM VOICE", "SYM QNT", "STR+VERB"]), n("STRUCT", .5), n("BRIGHT", .7), n("DAMPING", .4)], [n("POSITION", .3), n("EXCITE", .6), n("MIX", .7), n("SPREAD", .3)], [I("TUNE", -24, 24, 0), b("FINE"), L("INT EXC", ["OFF", "ON"]), I("CHORD", 0, 10, 0)]] },
  WARPS:    { kind: "shp", poly: true, cpu: 10, ram: 8, blurb: "CROSS-MOD · VOCODER", pages: [[n("ALGO", .3), n("TIMBRE", .5), L("CARRIER", ["SINE", "TRI", "SAW"], 2), L("MOD IN", ["OSC", "USB IN", "NOISE"])], [n("CAR LVL", .7), n("MOD LVL", .7), n("MIX", 1), I("CAR TUNE", -24, 24, 0)]] },
  FILTER:   { kind: "shp", poly: true, cpu: 2, ram: 1, blurb: "SVF · LP BP HP", pages: [[L("TYPE", ["LP", "BP", "HP", "LP24", "NOTCH", "PEAK"]), n("CUTOFF", .55), n("RESO", .3), n("DRIVE", .2)], [b("ENV AMT", .3), n("KEY TRK", .5), b("VEL AMT"), L("ENV SRC", ["AMP", "OWN", "MOD"])], [n("F ATK", .01), n("F DEC", .4), n("F SUS", .3), n("F REL", .35)], [L("SAT", ["SOFT", "HARD", "FOLD", "ASYM"]), n("AUDIO FM", 0), b("GAIN")]] },
  PEAKS:    { kind: "mod", poly: false, cpu: 1, ram: 1, blurb: "ENVELOPE · LFO · TAP", outs: ["PEAKS"], pages: [[L("FUNC", ["ENV", "LFO", "TAP"]), n("K1", 0), n("K2", .7), L("GATE", GATES)], [n("SUSTAIN", 0), L("INVERT", ["OFF", "ON"])]] },
  TIDES:    { kind: "mod", poly: false, cpu: 1, ram: 1, blurb: "SLOPE GENERATOR", outs: ["TIDES", "TIDE+"], pages: [[L("MODE", ["AD", "LOOP", "AR"], 1), n("FREQ", .3), n("SHAPE", .5), b("SLOPE", -.3)], [b("SMOOTH", .3), L("RANGE", ["LO", "MID", "HI"]), L("GATE", GATES, 4), L("INVERT", ["OFF", "ON"])]] },
  MARBLES:  { kind: "mod", poly: false, cpu: 3, ram: 4, blurb: "RANDOM NOTES · GATES", outs: ["MARB X", "MARB T"], pages: [[n("RATE", .5), n("SPREAD", .4), n("BIAS", .5), n("STEPS", .7)], [n("DEJA VU", .5), L("LENGTH", ["1", "2", "3", "4", "5", "6", "8", "16"], 6), n("JITTER", .1), L("CLOCK", ["FREE", "1/16", "1/8", "1/4"], 2)], [L("SCALE", ["MAJOR", "MINOR", "PENTA", "PELOG", "BHAIRAV", "SHRI"]), L("T MODEL", ["COMPLEM", "CLUSTER", "DRUMS", "INDEP", "DIVIDER", "3 STATE", "MARKOV"]), L("X RANGE", ["2V", "5V", "±5V"]), b("GATE LEN")]] },
  GRIDS:    { kind: "mod", poly: false, cpu: 1, ram: 2, blurb: "TOPOGRAPHIC DRUM MAP", outs: ["GRID", "ACCENT"], pages: [[n("X", .4), n("Y", .8), n("FILL", .8), L("OUT", ["BD", "SD", "HH", "KIT"], 3)], [n("CHAOS", 0), b("DECAY")]] },
  CLOUDS:   { kind: "fx", poly: false, cpu: 22, ram: 118, blurb: "GRANULAR TEXTURES", pages: [[n("POSITION", .2), n("SIZE", .6), b("PITCH"), n("DENSITY", .5)], [n("TEXTURE", .4), n("BLEND", .3), n("SPREAD", .5), n("FEEDBACK", .2)], [n("REVERB", .3), L("MODE", ["GRANULR", "STRETCH", "LOOPDLY", "SPECTRL"]), L("FREEZE", ["OFF", "ON"]), L("QUALITY", ["16 ST", "16 MONO", "8 ST", "8 MONO"])], [b("IN GAIN"), L("NOTE TRG", ["OFF", "ON"]), L("PITCH TRK", ["OFF", "ON"]), L("GRN SYNC", ["OFF", "1/16", "1/8", "1/4", "1/2"])], [n("SPRAY", 0), n("PITCH RND", 0), L("PITCH QNT", ["FREE", "SEMI", "5TH+OCT", "OCT"]), n("SIZE RND", 0)], [b("SCAN"), b("TONE")]] },
  DELAY:    { kind: "fx", poly: false, cpu: 3, ram: 64, blurb: "PING · MONO · TAPE", pages: [[L("TIME", ["1/16", "1/8", "1/8D", "1/4", "1/2", "1/8T", "1/4T", "1/4D", "1/2D"], 2), n("FEEDBACK", .4), n("TONE", .5), n("MIX", .3)], [L("MODE", ["PING", "MONO", "TAPE"]), n("WOW", .4), b("WIDTH"), n("DUCK", 0)], [n("LO CUT", 0), L("SYNC", ["TEMPO", "FREE"]), n("FREE MS", .5)]] },
  REVERB:   { kind: "fx", poly: false, cpu: 8, ram: 72, blurb: "PLATE · HALL · SHIMMER", pages: [[n("SIZE", .6), n("DAMP", .5), n("DIFFUSE", .55), n("MIX", .3)], [L("TYPE", ["PLATE", "HALL", "SHIMMER"]), n("PRE", 0), n("SHIMMER", .4), n("LO CUT", 0)], [b("WIDTH"), n("DUCK", 0)]] },
  CHORUS:   { kind: "fx", poly: false, cpu: 2, ram: 4, blurb: "STEREO CHORUS", pages: [[n("RATE", .3), n("DEPTH", .5), n("WIDTH", .7), n("MIX", .4)], [L("MODE", ["CHORUS", "FLANGER", "VIBRATO"]), n("FEEDBACK", 0), b("DELAY")]] },
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
const GLOBAL = { kind: "glo", pages: [[I("TEMPO", 60, 180, 112), L("MIDI CH", ["OMNI", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"], 0), L("VOICES", ["AUTO", "1", "2", "3", "4", "5", "6"]), L("CLOCK", ["INT", "MIDI"])], [n("VEL CURVE", .5), L("ANIMATION", ANIM_MODES, reducedMotion ? 1 : 0), n("LED BRIGHT", .8)], [L("TUNING", TUNINGS.map((t) => t[0])), L("T ROOT", KEYS12), I("A4 HZ", 400, 480, 440), I("BEND", 0, 24, 2)], [L("PROG CHG", ["OFF", "ON"], 1), L("MIDI OUT", ["OFF", "NOTES", "+CC"]), L("OUT CH", ["1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"]), L("LOCAL", ["ON", "OFF"])], [b("LOW EQ", 0), b("MID EQ", 0), b("HIGH EQ", 0), n("COMP", 0)], [L("STEP ADV", ["ON", "OFF"])]] };
const G_TEMPO = 0, G_MIDICH = 1, G_VOICES = 2, G_CLOCK = 3, G_VEL = 4, G_ANIM = 5, G_LED = 6, G_TUNE = 7, G_TROOT = 8, G_A4 = 9, G_BEND = 10, G_PROG = 11, G_MOUT = 12, G_OUTCH = 13, G_LOCAL = 14, G_LOW = 15, G_MID = 16, G_HIGH = 17, G_COMP = 18, G_ADV = 19;
const SCALES = { MAJOR: [0, 2, 4, 5, 7, 9, 11], MINOR: [0, 2, 3, 5, 7, 8, 10], DORIAN: [0, 2, 3, 5, 7, 9, 10], MIXOLYD: [0, 2, 4, 5, 7, 9, 10], "PENT MAJ": [0, 2, 4, 7, 9], "PENT MIN": [0, 3, 5, 7, 10], "HARM MIN": [0, 2, 3, 5, 7, 8, 11], WHOLE: [0, 2, 4, 6, 8, 10] };
// CHORD (SCL key): scale chords, with scale lock on its second page
const CHORD_TYPES = ["OFF", "TRIAD", "7TH", "SUS2", "SUS4", "ADD9", "6TH", "9TH", "POWER", "QUARTAL"];
const CHORD = { kind: "chd", pages: [[L("CHORD", CHORD_TYPES), L("INVERT", ["ROOT", "1ST", "2ND", "3RD"]), n("SPREAD", 0), n("STRUM", 0)], [L("LOCK", ["OFF", "ON"]), L("KEY", KEYS12), L("SCALE", Object.keys(SCALES))]] };
const C_TYPE = 0, C_INV = 1, C_SPREAD = 2, C_STRUM = 3, C_LOCK = 4, C_KEY = 5, C_SCALE = 6;
// SEQ key: the sequencer, after Clipper / SLOOP 2.7.2: up to 64 steps, each with up to 6 notes of their own length and force,
// a ratchet, a nudge, a fill condition, a slide and parameter locks
const SEQ_DIVS = ["1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/2", "1BAR", "2BAR"], DIV_16THS = [4, 2, 1, .5, 4 / 3, 2 / 3, 8, 16, 32];
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
}const POOL = 256, BUDGET = 80, MAXV = 6;

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
let seq = blankSeq();
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
try { const sc = JSON.parse(localStorage.getItem("fm1rack.chord.v1") || "null"); if (sc && sc.length === chd.p.length) chd.p = sc; } catch { /* defaults */ }
function storeChord() { try { localStorage.setItem("fm1rack.chord.v1", JSON.stringify(chd.p)); } catch { /* this visit only */ } }

const PRESETS = /*PRESETS*/[];
let presetIx = 0, patchName = PRESETS[0].name;
const USER_SLOTS = 16, STORE_KEY = "fm1rack.user.v1";
let userPatches = (() => { try { const v = JSON.parse(localStorage.getItem(STORE_KEY) || "[]"); return Array.from({ length: USER_SLOTS }, (_, i) => v[i] || null); } catch { return Array(USER_SLOTS).fill(null); } })();
function storeUser() { try { localStorage.setItem(STORE_KEY, JSON.stringify(userPatches)); return true; } catch { return false; } }
// the PRESETS encoder walks the factory patches, then the user slots that hold something
function patchList(raw = false) { const all = [...PRESETS.map((p, i) => ({ f: i, name: p.name })), ...userPatches.map((p, i) => p && { u: i, name: `U${String(i + 1).padStart(2, "0")} ${p.name}` }).filter(Boolean)]; if (raw || typeof filterList !== "function") return all; const f = filterList(all); return f.length ? f : all; }
function fitParams(def, arr) { const d = blankParams(def); return d.map((v, i) => (arr && arr[i] !== undefined ? arr[i] : v)); }
function loadPreset(i) {
  const entry = patchList()[i] || { f: 0, name: PRESETS[0].name };
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
  loadSeq(factorySeq(pr.seq));
  mseq = blankMseq();
  histReset(`LOADED ${patchName}`);
}
function restoreSnap(u) {
  for (const s of SLOTS) { const st = u.slots[s.id] || { mod: null, p: [] }; rack[s.id] = { mod: st.mod, p: fitParams(MODULES[st.mod], st.p) }; }
  amp.p = fitParams(AMP, u.amp); lfo.p = fitParams(LFOS, u.lfo);
  matrix = u.matrix.map((r) => ({ ...r })); macros = [...u.macros];
  lfoTgt = u.lfoTgt ? u.lfoTgt.map((t) => ({ ...t })) : blankLfoTgt();
  loadSeq(u.seq ? JSON.parse(JSON.stringify(u.seq)) : blankSeq());
  mseq = u.mseq ? JSON.parse(JSON.stringify(u.mseq)) : blankMseq();
}
// a new pattern keeps the transport running
function loadSeq(sq) { seq = upgradeSeq(sq); }
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
           amp: [...amp.p], lfo: [...lfo.p], matrix: matrix.map((r) => ({ ...r })), lfoTgt: lfoTgt.map((t) => ({ ...t })), macros: [...macros], seq: JSON.parse(JSON.stringify(seq)), mseq: JSON.parse(JSON.stringify(mseq)) };
}
const CHARS = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-#.";
let saveState = null;
function openSave() {
  const base = patchName.replace(/^U\d\d /, "").slice(0, 10).padEnd(10, " ");
  const free = userPatches.findIndex((p) => !p);
  saveState = { name: base.split(""), cursor: 0, slot: free < 0 ? 0 : free, t0: animT };
  page = "SAVE";
}
function commitSave() {
  const name = saveState.name.join("").trim() || "UNTITLED";
  userPatches[saveState.slot] = snapshot(name);
  const ok = storeUser();
  const list = patchList();
  presetIx = list.findIndex((e) => e.u === saveState.slot);
  if (presetIx < 0) { lib.filter = 0; storeLib(); presetIx = patchList().findIndex((e) => e.u === saveState.slot); }
  patchName = patchList()[presetIx].name;
  histReset(`SAVED ${name}`);
  say(ok ? `SAVED TO U${String(saveState.slot + 1).padStart(2, "0")}` : "SAVED FOR THIS VISIT ONLY", "SAVE");
  saveState = null; page = "PERFORM";
}

// ---------------------------------------------------------------- budget
function budgetFor(ids) {
  let shared = 0, poly = AMP.cpu, ram = 0;
  for (const s of SLOTS) {
    const m = MODULES[ids[s.id]];
    if (!m) continue;
    if (m.poly) poly += m.cpu; else shared += m.cpu;
    ram += m.ram;
  }
  const cap = amp.p[A_MODE] === 1 || amp.p[A_MODE] === 2 ? 1 : glo.p[G_VOICES] ? glo.p[G_VOICES] : MAXV;
  const voices = Math.min(cap, Math.floor((BUDGET - shared) / poly));
  return { shared, poly, ram, voices, cpu: shared + poly * Math.max(voices, 0) };
}
const currentIds = () => Object.fromEntries(SLOTS.map((s) => [s.id, rack[s.id].mod]));
const budget = () => budgetFor(currentIds());
function fitOf(slotId, modName) {
  const ids = currentIds(); ids[slotId] = modName;
  const bg = budgetFor(ids);
  if (modName && bg.ram > POOL) return { ok: false, why: `${MODULES[modName].ram}K RAM`, bg };
  if (bg.voices < 1) return { ok: false, why: "CPU", bg };
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
