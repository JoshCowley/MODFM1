/* Written by make_ui.py from web/src/1-data.js: the page's module, AMP and LFO pages. Don't edit. */
typedef struct { const char *l; uint8_t t, mod; int8_t lo, hi; uint16_t opt; float def; } ui_param_t;
enum { UT_NUM, UT_BIP, UT_LIST, UT_INT };
typedef struct { const char *name, *blurb; uint8_t id, kind, npages, nparams; const uint8_t *pages; const ui_param_t *p; uint8_t nouts; const char *outs[2]; } ui_mod_t;
static const char *const UI_OPT[] = {
    "VA VCF", "PHASE D", "6OP FM 1", "6OP FM 2", "6OP FM 3", "TERRAIN", "STR MACH", "CHIPTUNE",
    "PAIR VA", "WAVESHP", "2OP FM", "FORMANT", "ADDITIVE", "WAVETBL", "CHORDS", "SPEECH",
    "SWARM", "NOISE", "PARTICLE", "STRING", "MODAL", "BASS DR", "SNARE", "HI-HAT",
    "OFF", "ON", "CSAW", "/\\-_", "//-_", "FOLD", "UUUU", "SUB-",
    "SUB/", "SYN-", "SYN/", "//x3", "-_x3", "/\\x3", "SIx3", "RING",
    "////", "//uu", "TOY*", "ZLPF", "ZPKF", "ZBPF", "ZHPF", "VOSM",
    "VOWL", "VFOF", "HARM", "FM", "FBFM", "WTFM", "PLUK", "BOWD",
    "BLOW", "FLUT", "BELL", "DRUM", "KICK", "CYMB", "SNAR", "WTBL",
    "WMAP", "WLIN", "WTx4", "NOIS", "TWLN", "CLKN", "CLOU", "PRTC",
    "QPSK", "2", "3", "4", "6", "8", "12", "16",
    "4K", "8K", "16K", "22K", "32K", "48K", "MODAL", "SYMPATH",
    "STRING", "FM VOICE", "SYM QNT", "STR+VERB", "LP", "BP", "HP", "LP24",
    "NOTCH", "PEAK", "AMP", "OWN", "MOD", "SOFT", "HARD", "FOLD",
    "ASYM", "ENV", "LFO", "TAP", "NOTE", "GRID", "1/16", "1/8",
    "1/4", "BAR", "AD", "LOOP", "AR", "LO", "MID", "HI",
    "BD", "SD", "HH", "KIT", "1/16", "1/8", "1/8D", "1/4",
    "1/2", "1/8T", "1/4T", "1/4D", "1/2D", "PING", "MONO", "TAPE",
    "TEMPO", "FREE", "PLATE", "HALL", "SHIMMER", "CHORUS", "FLANGER", "VIBRATO",
    "POLY", "MONO", "LEGATO", "UNI 2", "UNI 4", "SINE", "TRI", "SAW",
    "SQUARE", "S&H", "DRIFT", "FREE", "1/16", "1/8", "1/4", "1/2",
    "1 BAR", "2 BARS", "BI", "UNI",
};
static const ui_param_t UP_PLAITS[] = {
    {"MODEL", 2, 0, 0, 23, 0, 8.0f},
    {"HARMO", 0, 1, 0, 1, 0, 0.42f},
    {"TIMBRE", 0, 1, 0, 1, 0, 0.6f},
    {"MORPH", 0, 1, 0, 1, 0, 0.31f},
    {"LPG", 2, 0, 0, 1, 24, 0.0f},
    {"DECAY", 0, 1, 0, 1, 0, 0.5f},
    {"COLOUR", 0, 1, 0, 1, 0, 0.5f},
    {"LEVEL", 0, 1, 0, 1, 0, 0.8f},
    {"OCTAVE", 3, 0, -3, 3, 0, 0.0f},
    {"FINE", 1, 1, -1, 1, 0, 0.0f},
    {"ENV>TIMB", 1, 1, -1, 1, 0, 0.0f},
    {"ENV>MRPH", 1, 1, -1, 1, 0, 0.0f},
    {"ENV>FM", 1, 1, -1, 1, 0, 0.0f},
    {"AUX MIX", 0, 1, 0, 1, 0, 0.0f},
};
static const uint8_t UG_PLAITS[] = {4, 4, 4, 2};
static const ui_param_t UP_BRAIDS[] = {
    {"MODEL", 2, 0, 0, 46, 26, 0.0f},
    {"TIMBRE", 0, 1, 0, 1, 0, 0.5f},
    {"COLOR", 0, 1, 0, 1, 0, 0.5f},
    {"DRIFT", 0, 1, 0, 1, 0, 0.1f},
    {"BITS", 2, 0, 0, 6, 73, 6.0f},
    {"RATE", 2, 0, 0, 5, 80, 5.0f},
    {"SIGN", 0, 1, 0, 1, 0, 0.0f},
    {"LEVEL", 0, 1, 0, 1, 0, 0.8f},
    {"OCTAVE", 3, 0, -3, 3, 0, 0.0f},
    {"FINE", 1, 1, -1, 1, 0, 0.0f},
    {"AD ATK", 0, 1, 0, 1, 0, 0.05f},
    {"AD DEC", 0, 1, 0, 1, 0, 0.4f},
    {"AD>TIMB", 1, 1, -1, 1, 0, 0.0f},
    {"AD>COLR", 1, 1, -1, 1, 0, 0.0f},
    {"AD>PTCH", 1, 1, -1, 1, 0, 0.0f},
    {"AD VCA", 2, 0, 0, 1, 24, 0.0f},
};
static const uint8_t UG_BRAIDS[] = {4, 4, 4, 4};
static const ui_param_t UP_RINGS[] = {
    {"MODEL", 2, 0, 0, 5, 86, 0.0f},
    {"STRUCT", 0, 1, 0, 1, 0, 0.5f},
    {"BRIGHT", 0, 1, 0, 1, 0, 0.7f},
    {"DAMPING", 0, 1, 0, 1, 0, 0.4f},
    {"POSITION", 0, 1, 0, 1, 0, 0.3f},
    {"EXCITE", 0, 1, 0, 1, 0, 0.6f},
    {"MIX", 0, 1, 0, 1, 0, 0.7f},
    {"SPREAD", 0, 1, 0, 1, 0, 0.3f},
    {"TUNE", 3, 0, -24, 24, 0, 0.0f},
    {"FINE", 1, 1, -1, 1, 0, 0.0f},
    {"INT EXC", 2, 0, 0, 1, 24, 0.0f},
    {"CHORD", 3, 0, 0, 10, 0, 0.0f},
};
static const uint8_t UG_RINGS[] = {4, 4, 4};
static const ui_param_t UP_FILTER[] = {
    {"TYPE", 2, 0, 0, 5, 92, 0.0f},
    {"CUTOFF", 0, 1, 0, 1, 0, 0.55f},
    {"RESO", 0, 1, 0, 1, 0, 0.3f},
    {"DRIVE", 0, 1, 0, 1, 0, 0.2f},
    {"ENV AMT", 1, 1, -1, 1, 0, 0.3f},
    {"KEY TRK", 0, 1, 0, 1, 0, 0.5f},
    {"VEL AMT", 1, 1, -1, 1, 0, 0.0f},
    {"ENV SRC", 2, 0, 0, 2, 98, 0.0f},
    {"F ATK", 0, 1, 0, 1, 0, 0.01f},
    {"F DEC", 0, 1, 0, 1, 0, 0.4f},
    {"F SUS", 0, 1, 0, 1, 0, 0.3f},
    {"F REL", 0, 1, 0, 1, 0, 0.35f},
    {"SAT", 2, 0, 0, 3, 101, 0.0f},
    {"AUDIO FM", 0, 1, 0, 1, 0, 0.0f},
    {"GAIN", 1, 1, -1, 1, 0, 0.0f},
};
static const uint8_t UG_FILTER[] = {4, 4, 4, 3};
static const ui_param_t UP_PEAKS[] = {
    {"FUNC", 2, 0, 0, 2, 105, 0.0f},
    {"K1", 0, 1, 0, 1, 0, 0.0f},
    {"K2", 0, 1, 0, 1, 0, 0.7f},
    {"GATE", 2, 0, 0, 5, 108, 0.0f},
    {"SUSTAIN", 0, 1, 0, 1, 0, 0.0f},
    {"INVERT", 2, 0, 0, 1, 24, 0.0f},
};
static const uint8_t UG_PEAKS[] = {4, 2};
static const ui_param_t UP_TIDES[] = {
    {"MODE", 2, 0, 0, 2, 114, 1.0f},
    {"FREQ", 0, 1, 0, 1, 0, 0.3f},
    {"SHAPE", 0, 1, 0, 1, 0, 0.5f},
    {"SLOPE", 1, 1, -1, 1, 0, -0.3f},
    {"SMOOTH", 1, 1, -1, 1, 0, 0.3f},
    {"RANGE", 2, 0, 0, 2, 117, 0.0f},
    {"GATE", 2, 0, 0, 5, 108, 4.0f},
    {"INVERT", 2, 0, 0, 1, 24, 0.0f},
};
static const uint8_t UG_TIDES[] = {4, 4};
static const ui_param_t UP_GRIDS[] = {
    {"X", 0, 1, 0, 1, 0, 0.4f},
    {"Y", 0, 1, 0, 1, 0, 0.8f},
    {"FILL", 0, 1, 0, 1, 0, 0.8f},
    {"OUT", 2, 0, 0, 3, 120, 3.0f},
    {"CHAOS", 0, 1, 0, 1, 0, 0.0f},
    {"DECAY", 1, 1, -1, 1, 0, 0.0f},
};
static const uint8_t UG_GRIDS[] = {4, 2};
static const ui_param_t UP_DELAY[] = {
    {"TIME", 2, 1, 0, 8, 124, 2.0f},
    {"FEEDBACK", 0, 1, 0, 1, 0, 0.4f},
    {"TONE", 0, 1, 0, 1, 0, 0.5f},
    {"MIX", 0, 1, 0, 1, 0, 0.3f},
    {"MODE", 2, 1, 0, 2, 133, 0.0f},
    {"WOW", 0, 1, 0, 1, 0, 0.4f},
    {"WIDTH", 1, 1, -1, 1, 0, 0.0f},
    {"DUCK", 0, 1, 0, 1, 0, 0.0f},
    {"LO CUT", 0, 1, 0, 1, 0, 0.0f},
    {"SYNC", 2, 1, 0, 1, 136, 0.0f},
    {"FREE MS", 0, 1, 0, 1, 0, 0.5f},
};
static const uint8_t UG_DELAY[] = {4, 4, 3};
static const ui_param_t UP_REVERB[] = {
    {"SIZE", 0, 1, 0, 1, 0, 0.6f},
    {"DAMP", 0, 1, 0, 1, 0, 0.5f},
    {"DIFFUSE", 0, 1, 0, 1, 0, 0.55f},
    {"MIX", 0, 1, 0, 1, 0, 0.3f},
    {"TYPE", 2, 1, 0, 2, 138, 0.0f},
    {"PRE", 0, 1, 0, 1, 0, 0.0f},
    {"SHIMMER", 0, 1, 0, 1, 0, 0.4f},
    {"LO CUT", 0, 1, 0, 1, 0, 0.0f},
    {"WIDTH", 1, 1, -1, 1, 0, 0.0f},
    {"DUCK", 0, 1, 0, 1, 0, 0.0f},
};
static const uint8_t UG_REVERB[] = {4, 4, 2};
static const ui_param_t UP_CHORUS[] = {
    {"RATE", 0, 1, 0, 1, 0, 0.3f},
    {"DEPTH", 0, 1, 0, 1, 0, 0.5f},
    {"WIDTH", 0, 1, 0, 1, 0, 0.7f},
    {"MIX", 0, 1, 0, 1, 0, 0.4f},
    {"MODE", 2, 1, 0, 2, 141, 0.0f},
    {"FEEDBACK", 0, 1, 0, 1, 0, 0.0f},
    {"DELAY", 1, 1, -1, 1, 0, 0.0f},
};
static const uint8_t UG_CHORUS[] = {4, 3};
static const ui_param_t UP_AMP[] = {
    {"ATTACK", 0, 1, 0, 1, 0, 0.02f},
    {"DECAY", 0, 1, 0, 1, 0, 0.4f},
    {"SUSTAIN", 0, 1, 0, 1, 0, 0.7f},
    {"RELEASE", 0, 1, 0, 1, 0, 0.35f},
    {"VEL AMT", 0, 1, 0, 1, 0, 0.6f},
    {"GLIDE", 0, 1, 0, 1, 0, 0.0f},
    {"SPREAD", 0, 1, 0, 1, 0, 0.3f},
    {"LEVEL", 0, 1, 0, 1, 0, 0.8f},
    {"M ATTACK", 0, 1, 0, 1, 0, 0.05f},
    {"M DECAY", 0, 1, 0, 1, 0, 0.4f},
    {"M LOOP", 2, 0, 0, 1, 24, 0.0f},
    {"MODE", 2, 0, 0, 4, 144, 0.0f},
    {"DETUNE", 0, 0, 0, 1, 0, 0.15f},
    {"VINTAGE", 0, 0, 0, 1, 0, 0.0f},
    {"DELAY", 0, 1, 0, 1, 0, 0.0f},
    {"HOLD", 0, 1, 0, 1, 0, 0.0f},
    {"A CURVE", 1, 1, -1, 1, 0, 0.0f},
    {"D CURVE", 1, 1, -1, 1, 0, 0.0f},
    {"VEL>ATK", 1, 1, -1, 1, 0, 0.0f},
    {"KEY>TIME", 1, 1, -1, 1, 0, 0.0f},
    {"M SUST", 0, 1, 0, 1, 0, 0.0f},
    {"PITCH ENV", 1, 1, -1, 1, 0, 0.0f},
};
static const uint8_t UG_AMP[] = {4, 4, 3, 3, 4, 4};
static const ui_param_t UP_LFO[] = {
    {"SHAPE 1", 2, 0, 0, 5, 149, 0.0f},
    {"RATE 1", 0, 1, 0, 1, 0, 0.35f},
    {"SYNC 1", 2, 0, 0, 6, 155, 0.0f},
    {"RETRIG 1", 2, 0, 0, 1, 24, 0.0f},
    {"SHAPE 2", 2, 0, 0, 5, 149, 1.0f},
    {"RATE 2", 0, 1, 0, 1, 0, 0.2f},
    {"SYNC 2", 2, 0, 0, 6, 155, 5.0f},
    {"RETRIG 2", 2, 0, 0, 1, 24, 0.0f},
    {"SHAPE 3", 2, 0, 0, 5, 149, 5.0f},
    {"RATE 3", 0, 1, 0, 1, 0, 0.5f},
    {"SYNC 3", 2, 0, 0, 6, 155, 0.0f},
    {"RETRIG 3", 2, 0, 0, 1, 24, 0.0f},
    {"FADE 1", 0, 1, 0, 1, 0, 0.0f},
    {"PHASE 1", 0, 1, 0, 1, 0, 0.0f},
    {"POLAR 1", 2, 0, 0, 1, 162, 0.0f},
    {"1SHOT 1", 2, 0, 0, 1, 24, 0.0f},
    {"FADE 2", 0, 1, 0, 1, 0, 0.0f},
    {"PHASE 2", 0, 1, 0, 1, 0, 0.0f},
    {"POLAR 2", 2, 0, 0, 1, 162, 0.0f},
    {"1SHOT 2", 2, 0, 0, 1, 24, 0.0f},
    {"FADE 3", 0, 1, 0, 1, 0, 0.0f},
    {"PHASE 3", 0, 1, 0, 1, 0, 0.0f},
    {"POLAR 3", 2, 0, 0, 1, 162, 0.0f},
    {"1SHOT 3", 2, 0, 0, 1, 24, 0.0f},
};
static const uint8_t UG_LFO[] = {4, 4, 4, 4, 4, 4};
#define UI_NMODS 10
static const ui_mod_t UI_MODS[UI_NMODS] = {
    {"PLAITS", "MACRO OSC / 24 MODELS", 1, 0, 4, 14, UG_PLAITS, UP_PLAITS, 0, {0}},
    {"BRAIDS", "47 SHAPES / BIT CRUSH", 2, 0, 4, 16, UG_BRAIDS, UP_BRAIDS, 0, {0}},
    {"RINGS", "RESONATOR / 6 MODELS", 10, 1, 3, 12, UG_RINGS, UP_RINGS, 0, {0}},
    {"FILTER", "SVF / LP BP HP", 12, 1, 4, 15, UG_FILTER, UP_FILTER, 0, {0}},
    {"PEAKS", "ENVELOPE / LFO / TAP", 20, 2, 2, 6, UG_PEAKS, UP_PEAKS, 1, {"PEAKS"}},
    {"TIDES", "SLOPE GENERATOR", 21, 2, 2, 8, UG_TIDES, UP_TIDES, 2, {"TIDES", "TIDE+"}},
    {"GRIDS", "TOPOGRAPHIC DRUM MAP", 23, 2, 2, 6, UG_GRIDS, UP_GRIDS, 2, {"GRID", "ACCENT"}},
    {"DELAY", "PING / MONO / TAPE", 31, 3, 3, 11, UG_DELAY, UP_DELAY, 0, {0}},
    {"REVERB", "PLATE / HALL / SHIMMER", 32, 3, 3, 10, UG_REVERB, UP_REVERB, 0, {0}},
    {"CHORUS", "STEREO CHORUS", 33, 3, 2, 7, UG_CHORUS, UP_CHORUS, 0, {0}},
};
static const ui_mod_t UI_AMP = {"AMP", "ENVELOPES", 0, 4, 6, 22, UG_AMP, UP_AMP, 0, {0}};
static const ui_mod_t UI_LFO = {"LFO", "THREE LFOS", 0, 5, 6, 24, UG_LFO, UP_LFO, 0, {0}};
