const GATES = ["NOTE", "GRID", "1/16", "1/8", "1/4", "BAR"];
const n = (l, def = 0.5, o = {}) => ({ l, t: "num", def, ...o });          // 0..1
const b = (l, def = 0) => ({ l, t: "bip", def });                           // -1..1
const L = (l, opts, def = 0) => ({ l, t: "list", opts, def, mod: false });
const I = (l, min, max, def) => ({ l, t: "int", min, max, def, mod: false });
const PLAITS_MODELS = ["VA VCF", "PHASE D", "6OP FM 1", "6OP FM 2", "6OP FM 3", "TERRAIN", "STR MACH", "CHIPTUNE", "PAIR VA", "WAVESHP", "2OP FM", "FORMANT", "ADDITIVE", "WAVETBL", "CHORDS", "SPEECH", "SWARM", "NOISE", "PARTICLE", "STRING", "MODAL", "BASS DR", "SNARE", "HI-HAT"];
const BRAIDS_MODELS = ["CSAW", "/\\-_", "//-_", "FOLD", "UUUU", "SUB-", "SUB/", "SYN-", "SYN/", "//x3", "-_x3", "/\\x3", "SIx3", "RING", "////", "//uu", "TOY*", "ZLPF", "ZPKF", "ZBPF", "ZHPF", "VOSM", "VOWL", "VFOF", "HARM", "FM", "FBFM", "WTFM", "PLUK", "BOWD", "BLOW", "FLUT", "BELL", "DRUM", "KICK", "CYMB", "SNAR", "WTBL", "WMAP", "WLIN", "WTx4", "NOIS", "TWLN", "CLKN", "CLOU", "PRTC", "QPSK"];

// cpu: est. units of an 80-unit budget (per voice when poly); ram: est. KB
const MODULES = {
  PLAITS:   { kind: "src", poly: true, cpu: 9, ram: 8, pages: [[L("MODEL", PLAITS_MODELS, 8), n("HARMO", .42), n("TIMBRE", .6), n("MORPH", .31)], [L("LPG", ["OFF", "ON"]), n("DECAY", .5), n("COLOUR", .5), n("LEVEL", .8)]] },
  BRAIDS:   { kind: "src", poly: true, cpu: 4, ram: 2, pages: [[L("MODEL", BRAIDS_MODELS), n("TIMBRE", .5), n("COLOR", .5), n("DRIFT", .1)], [L("BITS", ["2", "3", "4", "6", "8", "12", "16"], 6), L("RATE", ["4K", "8K", "16K", "22K", "32K", "48K"], 5), n("SIGN", 0), n("QUALITY", 1)]] },
  ELEMENTS: { kind: "src", poly: true, cpu: 40, ram: 64, pages: [[n("BOW", .3), n("BLOW", 0), n("STRIKE", .6), n("CONTOUR", .5)], [n("GEOMETRY", .4), n("BRIGHT", .6), n("DAMPING", .4), n("POSITION", .3)], [n("FLOW", .5), n("MALLET", .5), n("BOW TMB", .5), n("SPACE", .4)]] },
  RINGS:    { kind: "shp", poly: true, cpu: 14, ram: 40, pages: [[L("MODEL", ["MODAL", "SYMPATH", "STRING", "FM VOICE", "SYM QNT", "STR+VERB"]), n("STRUCT", .5), n("BRIGHT", .7), n("DAMPING", .4)], [n("POSITION", .3), n("EXCITE", .6), n("MIX", .7), n("SPREAD", .3)]] },
  WARPS:    { kind: "shp", poly: true, cpu: 10, ram: 8, pages: [[n("ALGO", .3), n("TIMBRE", .5), L("CARRIER", ["SINE", "TRI", "SAW"], 2), L("MOD IN", ["OSC", "USB IN", "NOISE"])], [n("CAR LVL", .7), n("MOD LVL", .7), n("MIX", 1)]] },
  FILTER:   { kind: "shp", poly: true, cpu: 2, ram: 1, pages: [[L("TYPE", ["LP", "BP", "HP"]), n("CUTOFF", .55), n("RESO", .3), n("DRIVE", .2)], [b("ENV AMT", .3), n("KEY TRK", .5)]] },
  PEAKS:    { kind: "mod", poly: false, cpu: 1, ram: 1, outs: ["PEAKS"], pages: [[L("FUNC", ["ENV", "LFO", "TAP"]), n("K1", 0), n("K2", .7), L("GATE", GATES)]] },
  TIDES:    { kind: "mod", poly: false, cpu: 1, ram: 1, outs: ["TIDES", "TIDE+"], pages: [[L("MODE", ["AD", "LOOP", "AR"], 1), n("FREQ", .3), n("SHAPE", .5), b("SLOPE", -.3)], [b("SMOOTH", .3), L("RANGE", ["LO", "MID"]), L("GATE", GATES, 4)]] },
  MARBLES:  { kind: "mod", poly: false, cpu: 3, ram: 4, outs: ["MARB X", "MARB T"], pages: [[n("RATE", .5), n("SPREAD", .4), n("BIAS", .5), n("STEPS", .7)], [n("DEJA VU", .5), L("LENGTH", ["1", "2", "3", "4", "5", "6", "8", "16"], 6), n("JITTER", .1), L("CLOCK", ["FREE", "1/16", "1/8", "1/4"], 2)]] },
  GRIDS:    { kind: "mod", poly: false, cpu: 1, ram: 2, outs: ["GRID", "ACCENT"], pages: [[n("X", .4), n("Y", .8), n("FILL", .8), L("OUT", ["BD", "SD", "HH", "KIT"], 3)], [n("CHAOS", 0)]] },
  CLOUDS:   { kind: "fx", poly: false, cpu: 22, ram: 118, pages: [[n("POSITION", .2), n("SIZE", .6), b("PITCH"), n("DENSITY", .5)], [n("TEXTURE", .4), n("BLEND", .3), n("SPREAD", .5), n("FEEDBACK", .2)], [n("REVERB", .3), L("MODE", ["GRANULR", "STRETCH", "LOOPDLY", "SPECTRL"]), L("FREEZE", ["OFF", "ON"])]] },
  DELAY:    { kind: "fx", poly: false, cpu: 3, ram: 64, pages: [[L("TIME", ["1/16", "1/8", "1/8D", "1/4", "1/2"], 2), n("FEEDBACK", .4), n("TONE", .5), n("MIX", .3)]] },
  REVERB:   { kind: "fx", poly: false, cpu: 6, ram: 32, pages: [[n("SIZE", .6), n("DAMP", .5), n("MOD", .3), n("MIX", .3)]] },
  CHORUS:   { kind: "fx", poly: false, cpu: 2, ram: 4, pages: [[n("RATE", .3), n("DEPTH", .5), n("WIDTH", .7), n("MIX", .4)]] },
};
const AMP = { kind: "amp", poly: true, cpu: 1, ram: 0, pages: [[n("ATTACK", .02), n("DECAY", .4), n("SUSTAIN", .7), n("RELEASE", .35)], [n("VEL AMT", .6), n("GLIDE", 0), n("SPREAD", .3), n("LEVEL", .8)], [n("M ATTACK", .05), n("M DECAY", .4), L("M LOOP", ["OFF", "ON"])]] };
const LFO_SHAPES = ["SINE", "TRI", "SAW", "SQUARE", "S&H", "DRIFT"], LFO_SYNC = ["FREE", "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS"];
const lfoPage = (i, shape, rate, sync) => [L(`SHAPE ${i}`, LFO_SHAPES, shape), n(`RATE ${i}`, rate), L(`SYNC ${i}`, LFO_SYNC, sync), L(`RETRIG ${i}`, ["OFF", "ON"])];
const LFOS = { kind: "lfo", pages: [lfoPage(1, 0, .35, 0), lfoPage(2, 1, .2, 5), lfoPage(3, 5, .5, 0)] };
const GLOBAL = { kind: "glo", pages: [[I("TEMPO", 60, 180, 112), L("MIDI CH", ["OMNI", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"], 1), L("VOICES", ["AUTO", "1", "2", "3", "4", "5", "6"]), L("CLOCK", ["INT", "MIDI"])], [n("VEL CURVE", .5), L("KNOBS", ["JUMP", "CATCH"], 1), n("LED BRIGHT", .7)]] };
const SCALES = { MAJOR: [0, 2, 4, 5, 7, 9, 11], MINOR: [0, 2, 3, 5, 7, 8, 10], DORIAN: [0, 2, 3, 5, 7, 9, 10], MIXOLYD: [0, 2, 4, 5, 7, 9, 10], "PENT MAJ": [0, 2, 4, 7, 9], "PENT MIN": [0, 3, 5, 7, 10], "HARM MIN": [0, 2, 3, 5, 7, 8, 11], WHOLE: [0, 2, 4, 6, 8, 10] };
const KEYS12 = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const SCALE = { kind: "glo", pages: [[L("LOCK", ["OFF", "ON"]), L("KEY", KEYS12), L("SCALE", Object.keys(SCALES))]] };
const POOL = 256, BUDGET = 80, MAXV = 6;

const SLOTS = [
  { id: "SRC", kind: "src", name: "SRC", choices: ["PLAITS", "BRAIDS", "ELEMENTS"] },
  { id: "SHP", kind: "shp", name: "SHP", choices: ["RINGS", "WARPS", "FILTER", null] },
  { id: "MODA", kind: "mod", name: "MOD A", choices: ["PEAKS", "TIDES", "MARBLES", "GRIDS", null] },
  { id: "MODB", kind: "mod", name: "MOD B", choices: ["PEAKS", "TIDES", "MARBLES", "GRIDS", null] },
  { id: "FX1", kind: "fx", name: "FX 1", choices: ["CLOUDS", "DELAY", "REVERB", "CHORUS", null] },
  { id: "FX2", kind: "fx", name: "FX 2", choices: ["CLOUDS", "DELAY", "REVERB", "CHORUS", null] },
];

module.exports={MODULES,AMP,LFOS,SLOTS,GLOBAL};
