/* SPDX-License-Identifier: MPL-2.0 */
/* FM1 Rack firmware: how the keys and MIDI become notes, part of app.c (included after the settings). Ported from the
 * page (web/src/5b-seq.js: SEL's scale, QNT and chords; 5c-tools.js: the arpeggiator, the tunings; 6-engine.js: MIDI
 * in and out, the clock). Menus stage 3.
 *
 * Two sides. The main loop owns the settings (set.glo, set.sel, set.arp: the GLOBAL, SEL and ARP pages' values) and
 * pushes the keys through rack_core's queue (PLE_* events). The audio interrupt does all the playing, so nothing here
 * is ever changed from two places at once: it turns a key into notes (QNT, a chord, its voicing), strums them, feeds
 * the arpeggiator and runs it on the internal tempo or MIDI clock, counts the notes sounding (two keys may share one),
 * and sends what it plays to MIDI out. Timing: events fall due by the codec frame (44.1 kHz) and apply at the start of
 * the block they fall in (256 frames, 5.8 ms). */

enum { G_TEMPO, G_MIDICH, G_VOICES, G_CLOCK, G_VEL, G_ANIM, G_LED, G_TUNE, G_TROOT, G_A4, G_BEND, G_PROG, G_MOUT,
       G_OUTCH, G_LOCAL, G_LOW, G_MID, G_HIGH, G_COMP, NG };
enum { C_SCALE, C_KEY, C_TYPE, C_VOICE, C_QNT, C_HARM, C_ZONE, C_STRUM, NC };
enum { A_MODE, A_RATE, A_OCT, A_GATE, A_HOLD, A_MUT, A_LOOP, A_SWING, NA };
enum { CT_OFF, CT_TRIAD, CT_7TH, CT_9TH, CT_11TH, CT_SUS4, CT_6TH, CT_POWER };
enum { VO_CLOSE, VO_LEAD, VO_OPEN, VO_INV1, VO_INV2, VO_OCT };
enum { AM_OFF, AM_UP, AM_DOWN, AM_UPDN, AM_DNUP, AM_RANDOM, AM_ORDER, AM_CHORD, AM_REPEAT, AM_UP8, AM_CONV, AM_DIVG,
       AM_PINKY, AM_THUMB, AM_TURING };
/* rack_core's queue: the play events (rc_do hands them to pl_event) */
enum { PLE_KEY_ON = 16, PLE_KEY_OFF, PLE_ZONE, PLE_RESET, PLE_ARP_CLEAR };

#define GI(i) ((int)(set.glo[i] + 0.5f))
/* the sequencer's (seq.c, after this file) */
static void sq_note_in(int raw, const uint8_t *notes, int n);
static void sq_note_out(int raw);
static void sq_clock_tick(void);
static void sq_reset_counts(void);
static void ms_apply(void);
#define CI(i) ((int)(set.sel[i] + 0.5f))
#define AI(i) ((int)(set.arp[i] + 0.5f))
#define PL_HZ 44100.0f                        /* the codec's frames: the clock */
#define PL_RAW_KEY 128                        /* a raw source: MIDI notes 0-127, then the keys */
#define PL_NRAW (PL_RAW_KEY + NKEYS)
#define PL_MAXN 10                            /* notes from one key at most (an 11th, its extensions, an octave) */

static void settings_defaults(void)
{
    int i;
    for (i = 0; i < NG; i++) set.glo[i] = UP_GLO[i].def;
    for (i = 0; i < NC; i++) set.sel[i] = UP_SEL[i].def;
    for (i = 0; i < NA; i++) set.arp[i] = UP_ARP[i].def;
}

/* ------------------------------------------------------------------ scales, QNT, chords (the page's 5b-seq.js) --- */
static int pc12(int x) { return ((x % 12) + 12) % 12; }
static int idiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
static int fmodi(int a, int b) { return ((a % b) + b) % b; }
static const uint8_t *sdeg(int *n)
{
    int s = CI(C_SCALE) < NSCALE ? CI(C_SCALE) : 0;
    *n = SCALE_N[s];
    return SCALE_DEG[s];
}
static int deg_index(int rel)                 /* rel's degree in the scale, -1 not in it */
{
    int n, i;
    const uint8_t *d = sdeg(&n);
    for (i = 0; i < n; i++)
        if (d[i] == rel)
            return i;
    return -1;
}
static int in_scale(int note) { return deg_index(pc12(note - CI(C_KEY))) >= 0; }
static int step_of(int s)
{
    int n;
    const uint8_t *d = sdeg(&n);
    return d[fmodi(s, n)] + 12 * idiv(s, n);
}
static int snap_down(int note) { int x; for (x = note; x > note - 12; x--) if (in_scale(x)) return x; return note; }
static int snap_up(int note) { int x; for (x = note; x < note + 12; x++) if (in_scale(x)) return x; return note; }
static const int8_t WHITE_IX[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
static int white_to_scale(int note)           /* WHITE: the scale on the white keys, C4 the root */
{
    int n, wi = WHITE_IX[pc12(note)], d;
    const uint8_t *deg = sdeg(&n);
    if (wi < 0)
        return -1;
    d = (idiv(note, 12) - 5) * 7 + wi;
    return 60 + CI(C_KEY) + 12 * idiv(d, n) + deg[fmodi(d, n)];
}
static int quantize(int note)                 /* -1: no note (a black key under WHITE) */
{
    switch (CI(C_QNT)) {
    case 1: return snap_down(note);
    case 2: return snap_up(note);
    case 3: return white_to_scale(note);
    }
    return note;
}

/* ZONE: the seven lowest keys, while chords are on, change the chord the other keys play */
static const struct { uint8_t ext, v; } ZONE[7] = {{1, 9}, {0, 3}, {1, 10}, {0, 2}, {1, 11}, {0, 1}, {1, 14}};
enum { Q_NONE, Q_MAJ, Q_MIN, Q_DIM, Q_SUS };
static const int8_t QUAL[5][3] = {{0, 0, 0}, {0, 4, 7}, {0, 3, 7}, {0, 3, 6}, {0, 5, 7}};
static const int8_t TYPE_STEPS[7][6] = {{0}, {0, 2, 4, -1}, {0, 2, 4, 6, -1}, {0, 2, 4, 6, 8, -1}, {0, 2, 4, 6, 8, 10},
                                        {0, 3, 4, -1}, {0, 2, 4, 5, -1}};
static const int8_t CHROMA[2][7][6] = {
    {{0}, {0, 4, 7, -1}, {0, 4, 7, 11, -1}, {0, 4, 7, 11, 14, -1}, {0, 4, 7, 11, 14, 17}, {0, 5, 7, -1}, {0, 4, 7, 9, -1}},
    {{0}, {0, 3, 7, -1}, {0, 3, 7, 10, -1}, {0, 3, 7, 10, 14, -1}, {0, 3, 7, 10, 14, 17}, {0, 5, 7, -1}, {0, 3, 7, 9, -1}}};
static int zone_on(void) { return CI(C_ZONE) == 0 && CI(C_TYPE) != CT_OFF; }

static int add_unique(int *a, int n, int v)
{
    int i;
    for (i = 0; i < n; i++)
        if (a[i] == v)
            return n;
    if (n < PL_MAXN + 2)
        a[n++] = v;
    return n;
}
static void sort_i(int *a, int n)
{
    int i, j;
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && a[j - 1] > a[j]; j--) {
            int t = a[j];
            a[j] = a[j - 1];
            a[j - 1] = t;
        }
}
/* the intervals above the root for one key (the page's chordIntervals): the scale's own chord in KEY harmony, a
 * chromatic one otherwise; zone: the quality held (Q_*), the extensions held (a bitmask of ZONE keys) */
static int chord_iv(int root, int type, int q, uint8_t ext, int *out)
{
    int n = 0, i, base[8], nb = 0;
    if (type == CT_POWER) {
        out[0] = 0, out[1] = 7, out[2] = 12;
        return 3;
    }
    if (q) {
        for (i = 0; i < 3; i++)
            base[nb++] = QUAL[q][i];
    } else {
        int t = ext ? CT_TRIAD : type, rel = pc12(root - CI(C_KEY)), di = deg_index(rel), ns;
        sdeg(&ns);
        if (CI(C_HARM) == 0 && di >= 0 && ns == 7) {
            const int8_t *st = TYPE_STEPS[t];
            if ((t == CT_9TH || t == CT_11TH) && step_of(di + 8) - step_of(di) == 13)
                st = TYPE_STEPS[CT_7TH];      /* no flat 9 */
            for (i = 0; i < 6 && st[i] >= 0 && (i == 0 || st[i]); i++)
                base[nb++] = step_of(di + st[i]) - step_of(di);
            if (t == CT_SUS4 && base[1] == 6)
                base[1] = step_of(di + 1) - step_of(di);   /* an augmented 4th: sus2 */
        } else {
            int minor = CI(C_HARM) == 1 && deg_index(3) >= 0 && deg_index(4) < 0;
            const int8_t *st = CHROMA[minor][t];
            for (i = 0; i < 6 && st[i] >= 0 && (i == 0 || st[i]); i++)
                base[nb++] = st[i];
        }
    }
    for (i = 0; i < nb; i++)
        n = add_unique(out, n, base[i]);
    for (i = 0; i < 7; i++)
        if (ext >> i & 1u)
            n = add_unique(out, n, ZONE[i].v);
    sort_i(out, n);
    return n;
}

static int pl_lead[PL_MAXN + 2], pl_nlead;    /* LEAD: the last chord voiced */
static int near_dist(int x, const int *s, int n)
{
    int i, b = 999;
    for (i = 0; i < n; i++) {
        int d = x > s[i] ? x - s[i] : s[i] - x;
        if (d < b) b = d;
    }
    return b;
}
static int voice_chord(int *notes, int n, int preview)
{
    int v = CI(C_VOICE), k, i;
    sort_i(notes, n);
    if (v == VO_INV1 || v == VO_INV2)
        for (k = 0; k < (v == VO_INV1 ? 1 : 2) && k < n - 1; k++) {
            notes[0] += 12;
            sort_i(notes, n);
        }
    if (v == VO_OPEN && n >= 3) {
        notes[n - 2] -= 12;
        sort_i(notes, n);
    }
    if (v == VO_OCT && n < PL_MAXN + 2) {
        for (i = n; i > 0; i--)
            notes[i] = notes[i - 1];
        notes[0] -= 12;
        n++;
    }
    if (v == VO_LEAD && pl_nlead && n > 1) {  /* the inversion, moved by octaves, closest to the last chord */
        int best[PL_MAXN + 2], bc = 1 << 30, r, o, c[PL_MAXN + 2];
        for (r = 0; r < n; r++)
            for (o = -2; o <= 2; o++) {
                int cost = 0;
                for (i = 0; i < n; i++)
                    c[i] = notes[i] + (i < r ? 12 : 0) + 12 * o;
                sort_i(c, n);
                for (i = 0; i < n; i++)
                    cost += near_dist(c[i], pl_lead, pl_nlead);
                for (i = 0; i < pl_nlead; i++)
                    cost += near_dist(pl_lead[i], c, n);
                if (cost < bc) {
                    bc = cost;
                    for (i = 0; i < n; i++)
                        best[i] = c[i];
                }
            }
        for (i = 0; i < n; i++)
            notes[i] = best[i];
    }
    if (n > 1 && !preview) {
        for (i = 0; i < n; i++)
            pl_lead[i] = notes[i];
        pl_nlead = n;
    }
    return n;
}
/* one key in, its notes out (the page's chordNotes); 0 notes: the key plays nothing */
static int chord_notes(int raw, int q, uint8_t ext, int preview, uint8_t *out)
{
    int type = CI(C_TYPE), r, n, i, m = 0, iv[PL_MAXN + 2], nt[PL_MAXN + 2];
    if (type == CT_OFF && !q && !ext) {
        r = quantize(raw);
        if (r < 0 || r > 127)
            return 0;
        out[0] = (uint8_t)r;
        return 1;
    }
    r = CI(C_QNT) ? quantize(raw) : raw;      /* QNT on: the root follows the scale; off: a key outside it borrows */
    if (r < 0)
        return 0;
    n = chord_iv(r, type == CT_OFF ? CT_TRIAD : type, q, ext, iv);
    for (i = 0; i < n; i++)
        nt[i] = r + iv[i];
    n = voice_chord(nt, n, preview);
    sort_i(nt, n);
    for (i = 0; i < n && m < PL_MAXN; i++)
        if (nt[i] >= 12 && nt[i] <= 120 && (!m || out[m - 1] != nt[i]))
            out[m++] = (uint8_t)nt[i];
    return m;
}

/* ------------------------------------------------------------------ the audio interrupt's side --- */
typedef struct {
    uint8_t n, active, sus;                   /* its notes; held; let go while the sustain pedal was down */
    uint16_t on;                              /* which of them sound (a strum sounds them one by one) */
    uint8_t note[PL_MAXN];
} pl_raw_t;
static pl_raw_t pl_raw[PL_NRAW];
static uint8_t pl_snd[128];                   /* how many keys sound each note */
typedef struct { uint32_t at; uint8_t note, raw, on, k; float vel; } pl_ev_t;
#define PL_NPEND 64
static pl_ev_t pl_pend[PL_NPEND];
static int pl_npend;
static uint32_t pl_now;                       /* codec frames */
static uint8_t pl_zone, pl_zone_order[7], pl_nzone;   /* the zone keys held, in the order pressed */
static int pl_sus;
static float pl_vel = 0.75f;
static uint32_t pl_rng = 0xB5ADu;
/* the arpeggiator */
#define PL_POOL 12
static struct {
    uint8_t raw[PL_POOL], n[PL_POOL], note[PL_POOL][PL_MAXN];
    int count, latch_free, running;
    uint32_t next_at, step, idx;
    uint16_t reg;
    float tick_frames;                        /* MIDI clock: frames per tick, averaged */
    uint32_t last_tick, ticks;
    float clock_acc;                          /* MIDI clock out: frames to the next tick */
} pa = {{0}, {0}, {{0}}, 0, 0, 0, 0, 0, 0, 0xB5ADu, 0.0f, 0, 0, 0.0f};
/* for the screen: the last chord and arp step, and the notes played (the visualisers' pulses) */
static volatile struct {
    uint8_t chord[PL_MAXN], nchord, arp[PL_MAXN], narp;
    uint32_t chord_seq, arp_seq, step;
    uint8_t ring_n[16], ring_v[16];
    uint32_t ring_w;
    float bpm_in;                             /* the MIDI clock's tempo, 0: none */
    uint32_t clock_at;                        /* plat_ms of the last MIDI clock */
} pv;

static float rand01(void) { pl_rng = pl_rng * 1664525u + 1013904223u; return (float)(pl_rng >> 8) * (1.0f / 16777216.0f); }

static void midi_out_note(int note, int vel)
{
    uint32_t ch = (uint32_t)GI(G_OUTCH) & 15u;
    if (!GI(G_MOUT))
        return;
    if (vel)
        plat_midi_send(0x09u | (0x90u | ch) << 8 | (uint32_t)note << 16 | (uint32_t)vel << 24, TO_USB);
    else
        plat_midi_send(0x08u | (0x80u | ch) << 8 | (uint32_t)note << 16, TO_USB);
}
/* a note on / off, counted (two keys may share a note: it ends with the last); local: to the engine; out: to MIDI */
static void snd_on(int note, float vel, int local, int out)
{
    if (note < 0 || note > 127)
        return;
    if (pl_snd[note]++)
        return;
    if (local)
        rc_do(RCE_ON, 0, (float)note, vel);
    if (out)
        midi_out_note(note, (int)(vel * 127.0f + 0.5f) < 1 ? 1 : (int)(vel * 127.0f + 0.5f));
    pv.ring_n[pv.ring_w & 15u] = (uint8_t)note;
    pv.ring_v[pv.ring_w & 15u] = (uint8_t)(vel * 127.0f);
    pv.ring_w++;
}
static void snd_off(int note, int local, int out)
{
    if (note < 0 || note > 127 || !pl_snd[note])
        return;
    if (--pl_snd[note])
        return;
    if (local)
        rc_do(RCE_OFF, 0, (float)note, 0.0f);
    if (out)
        midi_out_note(note, 0);
}
static int raw_local(int raw) { return raw < PL_RAW_KEY || GI(G_LOCAL) == 0; }   /* LOCAL OFF: the keys only send MIDI */
static int raw_out(int raw) { return raw >= PL_RAW_KEY; }                         /* MIDI in is not sent back out */

static void pend(uint32_t at, int note, int raw, int on, float vel)
{
    if (pl_npend >= PL_NPEND)
        return;
    pl_pend[pl_npend].at = at;
    pl_pend[pl_npend].note = (uint8_t)note;
    pl_pend[pl_npend].raw = (uint8_t)raw;
    pl_pend[pl_npend].on = (uint8_t)on;
    pl_pend[pl_npend].vel = vel;
    pl_npend++;
}

/* the arpeggiator's pool: what is held (or latched), a key's notes together, in the order they came */
static void arp_add(int raw, const uint8_t *notes, int n)
{
    int i;
    if (AI(A_HOLD) && pa.latch_free) {        /* HOLD: a new key after letting go starts a new set */
        pa.latch_free = 0;
        pa.count = 0;
        pa.idx = 0;
    }
    if (pa.count >= PL_POOL)
        return;
    pa.raw[pa.count] = (uint8_t)raw;
    pa.n[pa.count] = (uint8_t)n;
    for (i = 0; i < n; i++)
        pa.note[pa.count][i] = notes[i];
    pa.count++;
}
static void arp_remove(int raw)
{
    int i, j, any = 0;
    for (i = 0; i < PL_NRAW; i++)
        if (pl_raw[i].active && i != raw)
            any = 1;
    if (AI(A_HOLD)) {                         /* HOLD: the notes stay when every key is up */
        if (!any)
            pa.latch_free = 1;
        return;
    }
    for (i = 0; i < pa.count; i++)
        if (pa.raw[i] == raw) {
            for (j = i; j < pa.count - 1; j++) {
                pa.raw[j] = pa.raw[j + 1];
                pa.n[j] = pa.n[j + 1];
                memcpy(pa.note[j], pa.note[j + 1], PL_MAXN);
            }
            pa.count--;
            break;
        }
}

static void raw_release(int raw)
{
    pl_raw_t *r = &pl_raw[raw];
    int i, j;
    for (i = 0; i < pl_npend;)                /* a strum not finished: the rest never sounds */
        if (pl_pend[i].raw == raw && pl_pend[i].on) {
            for (j = i; j < pl_npend - 1; j++)
                pl_pend[j] = pl_pend[j + 1];
            pl_npend--;
        } else {
            i++;
        }
    for (i = 0; i < r->n; i++)
        if (r->on >> i & 1u)
            snd_off(r->note[i], raw_local(raw), raw_out(raw));
    r->on = 0;
    r->n = 0;
}

static void pl_key_on(int raw, int note, float vel)
{
    pl_raw_t *r;
    uint8_t notes[PL_MAXN];
    int n, i, q = Q_NONE;
    float strum;
    if (raw < 0 || raw >= PL_NRAW)
        return;
    r = &pl_raw[raw];
    if (r->active || r->n)                    /* the same key again: the last one ends first */
        raw_release(raw);
    vel = fm_powf(vel < 0.0f ? 0.0f : vel > 1.0f ? 1.0f : vel, fm_exp2f((0.5f - set.glo[G_VEL]) * 3.0f));
    pl_vel = vel;
    if (pl_nzone && zone_on()) {              /* the quality: the last zone key held, MIN + MAJ together sus */
        int mi = 0, ma = 0;
        for (i = 0; i < pl_nzone; i++) {
            int z = pl_zone_order[i];
            if (!ZONE[z].ext) {
                q = ZONE[z].v;
                mi |= q == Q_MIN;
                ma |= q == Q_MAJ;
            }
        }
        if (mi && ma)
            q = Q_SUS;
    }
    n = chord_notes(note, q, zone_on() ? (uint8_t)(pl_zone & 0x55u) : 0u, 0, notes);
    r->active = 1;
    r->sus = 0;
    r->on = 0;
    r->n = (uint8_t)n;
    for (i = 0; i < n; i++)
        r->note[i] = notes[i];
    if (n > 1 || CI(C_TYPE) != CT_OFF) {      /* the screen's chord */
        for (i = 0; i < n; i++)
            pv.chord[i] = notes[i];
        pv.nchord = (uint8_t)n;
        pv.chord_seq++;
    }
    sq_note_in(raw, notes, n);               /* the sequencer: REC, step entry, the last notes (seq.c) */
    if (AI(A_MODE) != AM_OFF) {
        arp_add(raw, notes, n);
        r->n = 0;                             /* the arp plays them */
        return;
    }
    strum = set.sel[C_STRUM] * 0.08f * PL_HZ;
    for (i = 0; i < n; i++) {
        if (i && strum > 1.0f) {
            pend(pl_now + (uint32_t)(strum * (float)i), i, raw, 1, vel);
        } else {
            snd_on(notes[i], vel, raw_local(raw), raw_out(raw));
            r->on |= (uint16_t)(1u << i);
        }
    }
}
static void pl_key_off(int raw)
{
    if (raw < 0 || raw >= PL_NRAW || !pl_raw[raw].active)
        return;
    if (raw < PL_RAW_KEY && pl_sus) {         /* the sustain pedal: later */
        pl_raw[raw].sus = 1;
        return;
    }
    pl_raw[raw].active = 0;
    sq_note_out(raw);
    if (AI(A_MODE) != AM_OFF)
        arp_remove(raw);
    raw_release(raw);
}
static void pl_sustain(int down)
{
    int i;
    pl_sus = down;
    if (!down)
        for (i = 0; i < PL_RAW_KEY; i++)
            if (pl_raw[i].sus) {
                pl_raw[i].sus = 0;
                pl_key_off(i);
            }
}
static void pl_zone_key(int k, int on)
{
    int i, j;
    if (on) {
        pl_zone |= (uint8_t)(1u << k);
        if (pl_nzone < 7)
            pl_zone_order[pl_nzone++] = (uint8_t)k;
    } else {
        pl_zone &= (uint8_t)~(1u << k);
        for (i = 0; i < pl_nzone; i++)
            if (pl_zone_order[i] == k) {
                for (j = i; j < pl_nzone - 1; j++)
                    pl_zone_order[j] = pl_zone_order[j + 1];
                pl_nzone--;
                break;
            }
    }
}
static void pl_reset(int all)                 /* the engine was reset (a preset load, all notes off) */
{
    int i;
    for (i = 0; i < 128; i++)                 /* MIDI out: what it had sent on ends */
        if (pl_snd[i])
            midi_out_note(i, 0);
    memset(pl_snd, 0, sizeof pl_snd);
    sq_reset_counts();
    pl_npend = 0;
    for (i = 0; i < PL_NRAW; i++) {
        pl_raw[i].on = 0;
        pl_raw[i].n = 0;
        if (all)
            pl_raw[i].active = pl_raw[i].sus = 0;
    }
    if (all) {
        pa.count = 0;
        pa.latch_free = 0;
        pl_zone = 0;
        pl_nzone = 0;
    }
}

/* the arp's order of the pool (the page's arpSequence): up to 4 octaves of 12 keys of PL_MAXN notes */
#define PL_SEQ 96
static int arp_sequence(uint8_t *out)
{
    uint8_t asc[PL_SEQ], ord[PL_SEQ];
    int na = 0, no = 0, i, j, o, oct = AI(A_OCT), n = 0, mode = AI(A_MODE);
    for (o = 0; o < oct && o < 4; o++)
        for (i = 0; i < pa.count; i++)
            for (j = 0; j < pa.n[i]; j++)
                if (no < PL_SEQ / 2 && pa.note[i][j] + 12 * o <= 127)
                    ord[no++] = (uint8_t)(pa.note[i][j] + 12 * o);
    for (i = 0; i < no; i++)
        asc[na++] = ord[i];
    for (i = 1; i < na; i++)                  /* sorted, repeats kept as the page's */
        for (j = i; j > 0 && asc[j - 1] > asc[j]; j--) {
            uint8_t t = asc[j];
            asc[j] = asc[j - 1];
            asc[j - 1] = t;
        }
    if (!na)
        return 0;
    switch (mode) {
    case AM_DOWN: for (i = na - 1; i >= 0; i--) out[n++] = asc[i]; break;
    case AM_UPDN:
        for (i = 0; i < na; i++) out[n++] = asc[i];
        if (na > 2) for (i = na - 2; i >= 1; i--) out[n++] = asc[i];
        break;
    case AM_DNUP:
        for (i = na - 1; i >= 0; i--) out[n++] = asc[i];
        if (na > 2) for (i = 1; i <= na - 2; i++) out[n++] = asc[i];
        break;
    case AM_ORDER: for (i = 0; i < no; i++) out[n++] = ord[i]; break;
    case AM_REPEAT: for (i = 0; i < na; i++) { out[n++] = asc[i]; out[n++] = asc[i]; } break;
    case AM_UP8: for (i = 0; i < na; i++) { out[n++] = asc[i]; out[n++] = (uint8_t)(asc[i] + 12 > 127 ? asc[i] : asc[i] + 12); } break;
    case AM_CONV: { int a = 0, b = na - 1; while (a <= b) { out[n++] = asc[a++]; if (a <= b) out[n++] = asc[b--]; } break; }
    case AM_DIVG: {
        int m = (na - 1) / 2, k = 0;
        while (n < na && k < 4 * na) {
            int jj = m + (k % 2 ? -((k + 1) / 2) : (k + 1) / 2);
            if (jj >= 0 && jj < na) out[n++] = asc[jj];
            k++;
        }
        break;
    }
    case AM_PINKY:
        for (i = 0; i < na - 1; i++) { out[n++] = asc[i]; out[n++] = asc[na - 1]; }
        if (na == 1) out[n++] = asc[0];
        break;
    case AM_THUMB:
        for (i = 1; i < na; i++) { out[n++] = asc[0]; out[n++] = asc[i]; }
        if (na == 1) out[n++] = asc[0];
        break;
    default: for (i = 0; i < na; i++) out[n++] = asc[i];
    }
    return n;
}
/* the next step's notes (the page's arpPick) */
static int arp_pick(uint8_t *out)
{
    uint8_t sq[PL_SEQ * 2];
    int n, mode = AI(A_MODE), i, j;
    if (mode == AM_CHORD) {
        n = 0;
        for (i = 0; i < pa.count; i++)
            for (j = 0; j < pa.n[i] && n < PL_MAXN; j++)
                out[n++] = pa.note[i][j];
        return n;
    }
    n = arp_sequence(sq);
    if (!n)
        return 0;
    if (mode == AM_RANDOM) {
        out[0] = sq[(int)(rand01() * (float)n) % n];
        return 1;
    }
    if (mode == AM_TURING) {                  /* a looping shift register: MUTATE, how often a bit flips */
        int L = AI(A_LOOP) < 1 ? 1 : AI(A_LOOP) > 16 ? 16 : AI(A_LOOP);
        uint32_t mask = (1u << L) - 1u, bit = (uint32_t)pa.reg >> (L - 1) & 1u;
        uint8_t asc[PL_SEQ * 2];
        if (rand01() < set.arp[A_MUT] * 0.5f)
            bit ^= 1u;
        pa.reg = (uint16_t)((((uint32_t)pa.reg << 1) | bit) & mask);
        for (i = 0; i < n; i++)
            asc[i] = sq[i];
        for (i = 1; i < n; i++)
            for (j = i; j > 0 && asc[j - 1] > asc[j]; j--) {
                uint8_t t = asc[j];
                asc[j] = asc[j - 1];
                asc[j - 1] = t;
            }
        i = (int)((float)pa.reg / (float)(mask ? mask : 1u) * (float)n);
        out[0] = asc[i >= n ? n - 1 : i];
        return 1;
    }
    out[0] = sq[pa.idx++ % (uint32_t)n];
    return 1;
}
static const float ARP_BEATS[6] = {1.0f, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f};
static const uint8_t ARP_TICKS[6] = {24, 12, 8, 6, 4, 3};   /* MIDI clock ticks a step */
static float tempo_bpm(void) { return set.glo[G_TEMPO] < 30.0f ? 112.0f : set.glo[G_TEMPO]; }
static float step_frames(void)
{
    int r = AI(A_RATE) > 5 ? 5 : AI(A_RATE);
    if (GI(G_CLOCK) == 1 && pa.tick_frames > 0.0f)
        return pa.tick_frames * (float)ARP_TICKS[r];
    return ARP_BEATS[r] * 60.0f / tempo_bpm() * PL_HZ;
}
static void arp_step(uint32_t at)             /* a step at frame at: its notes on, and off after GATE */
{
    uint8_t notes[PL_MAXN];
    float d = step_frames();
    int n = arp_pick(notes), i;
    uint32_t t = at + (pa.step & 1u ? (uint32_t)(set.arp[A_SWING] * 0.5f * d) : 0u);
    uint32_t len = (uint32_t)(set.arp[A_GATE] * d * 0.95f);
    if (len < 882u)
        len = 882u;                           /* 20 ms at least */
    for (i = 0; i < n; i++) {
        pend(t, notes[i], 255, 1, pl_vel);
        pend(t + len, notes[i], 255, 0, 0.0f);
    }
    for (i = 0; i < n; i++)
        pv.arp[i] = notes[i];
    pv.narp = (uint8_t)n;
    pv.arp_seq++;
    pv.step = pa.step;
    pa.step++;
}

/* a MIDI clock tick (0xF8), in the interrupt; CLOCK MIDI: the arp's steps */
static void pl_clock_tick(void)
{
    uint32_t gap = pl_now - pa.last_tick;
    if (pa.last_tick && gap > 0u && gap < 44100u) {
        pa.tick_frames = pa.tick_frames > 0.0f ? pa.tick_frames + ((float)gap - pa.tick_frames) * 0.1f : (float)gap;
        pv.bpm_in = 60.0f * PL_HZ / (pa.tick_frames * 24.0f);
    }
    pa.last_tick = pl_now;
    pv.clock_at = plat_ms();
    sq_clock_tick();
    if (GI(G_CLOCK) != 1 || AI(A_MODE) == AM_OFF || !pa.count)
        return;
    if (pa.ticks++ % ARP_TICKS[AI(A_RATE) > 5 ? 5 : AI(A_RATE)] == 0u)
        arp_step(pl_now);
}
static void pl_clock_start(void) { pa.ticks = 0; pa.idx = 0; pa.step = 0; }

/* each block (the interrupt, before the render): the arp's steps on the internal clock, then what falls due */
static void pl_block(uint32_t n)
{
    uint32_t end = pl_now + n;
    int i, j;
    if (rc_audio.pause_req)
        return;
    if (AI(A_MODE) != AM_OFF && pa.count && GI(G_CLOCK) != 1) {
        float d = step_frames();
        if (!pa.running || (int32_t)(pa.next_at - pl_now) < -(int32_t)d) {
            pa.running = 1;
            pa.next_at = pl_now;
            pa.step = 0;
        }
        while ((int32_t)(pa.next_at - end) < 0) {
            arp_step(pa.next_at);
            pa.next_at += (uint32_t)d;
        }
    } else {
        pa.running = 0;
    }
    if (GI(G_MOUT) && GI(G_CLOCK) != 1) {     /* MIDI clock out on the internal tempo */
        pa.clock_acc -= (float)n;
        while (pa.clock_acc <= 0.0f) {
            plat_midi_send(0x0Fu | 0xF8u << 8, TO_USB);
            pa.clock_acc += 60.0f / tempo_bpm() / 24.0f * PL_HZ;
        }
    }
    for (i = 0; i < pl_npend;) {              /* what falls due in this block, in time order enough */
        pl_ev_t e = pl_pend[i];
        if ((int32_t)(e.at - end) < 0) {
            for (j = i; j < pl_npend - 1; j++)
                pl_pend[j] = pl_pend[j + 1];
            pl_npend--;
            if (e.raw == 255) {               /* the arp: always local, always out */
                if (e.on) snd_on(e.note, e.vel, 1, 1);
                else snd_off(e.note, 1, 1);
            } else if (e.raw < PL_NRAW) {     /* a strummed note: e.note is its place in the key's chord */
                pl_raw_t *r = &pl_raw[e.raw];
                if (e.note < r->n) {
                    snd_on(r->note[e.note], e.vel, raw_local(e.raw), raw_out(e.raw));
                    r->on |= (uint16_t)(1u << e.note);
                }
            }
        } else {
            i++;
        }
    }
    pl_now = end;
}

/* rack_core's queue: the play events from the main loop (rc_do's other types) */
static void pl_event(int type, int a, float x, float y)
{
    switch (type) {
    case PLE_KEY_ON: pl_key_on(a, (int)x, y); break;
    case PLE_KEY_OFF: pl_key_off(a); break;
    case PLE_ZONE: pl_zone_key(a, x > 0.5f); break;
    case PLE_RESET: pl_reset(a); break;
    case PLE_ARP_CLEAR: pa.count = 0; pa.latch_free = 0; pa.idx = 0; break;
    case RCE_ALL_OFF: pl_reset(1); break;
    }
}

/* ------------------------------------------------------------------ the main loop's side --- */
static volatile int pl_prog_req;              /* MIDI program change: the entry to load, + 1 */

/* the tuning (GLOBAL > TUNING) into the engine's note table (the page's tuningCalls) */
static void tune_apply(void)
{
    int t = GI(G_TUNE) < NTUNE ? GI(G_TUNE) : 0, n = TUNE_N[t], root = GI(G_TROOT), x;
    const float *c = TUNE_CENTS + TUNE_AT[t];
    float a4 = 12.0f * fm_log2f(set.glo[G_A4] / 440.0f);
    int R = 60 - fmodi(60 - root, 12);
    for (x = 0; x < 128; x++) {
        float tuned;
        if (n == 12) {
            int pc = fmodi(x - R, 12);
            tuned = (float)x + (c[pc] - (float)pc * 100.0f) / 100.0f;
        } else {
            int dd = x - R, o = idiv(dd, n), deg = fmodi(dd, n);
            tuned = (float)R + ((float)o * 1200.0f + c[deg]) / 100.0f;
        }
        rk_set_tune(x, tuned - (float)x + a4);
    }
}
static void master_apply(void)
{
    rk_set_master_fx(set.glo[G_LOW] * 12.0f, set.glo[G_MID] * 10.0f, set.glo[G_HIGH] * 12.0f, set.glo[G_COMP]);
}
/* after a preset loads (the engine reset): what GLOBAL keeps across presets */
static void glo_apply(void)
{
    rk_set_param(RS_GLO, 0, tempo_bpm());
    master_apply();
    tune_apply();
    ms_apply();                               /* MSEQ's smoothing (seqpages.c) */
}

static uint32_t set_dirty_at;                 /* a setting changed: saved when they rest */
static void settings_touch(void) { set_dirty_at = plat_ms() | 1u; }
static void settings_tick(void)
{
    if (set_dirty_at && plat_ms() - set_dirty_at > 3000u) {
        set_dirty_at = 0;
        plat_store_save(OBJ_SETTINGS, &set, sizeof set);
    }
}
