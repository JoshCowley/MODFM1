/* SPDX-License-Identifier: MPL-2.0 */
/* FM1 Rack firmware: the sequencer (stage 4), part of app.c after play.c. Ported from the page's sequencer
 * (web/src/5d-sequencer.js, 1.6): 8 banks (A-H) of up to 64 steps, each step up to 6 notes with their own length and
 * force (GHOST SOFT NORM HARD ACC), a ratchet, a nudge, a slide, a fill condition and a chance; per bank its length,
 * division, swing, gate, SPICE and DICE; a song of 16 rows (bank, repeats, transpose); and MSEQ, four lanes of 16
 * values stepping on the tempo, the matrix's sources SEQ 1-4 (5c-tools.js).
 *
 * Like the arpeggiator it plays in the audio interrupt, by the codec's frame, on the internal tempo or MIDI clock:
 * each block it schedules the steps that start before the block ends, and applies what falls due. The main loop owns
 * the editing (the pages, seqpages.c); the interrupt only reads the banks, except REC and step entry, which write the
 * notes played into the step (played keys reach it there, play.c's pl_key_on).
 *
 * Kept in the store by SAVE on a sequencer page (OBJ_BANK0-7, OBJ_SONG): a write stops the sound for a moment, so not
 * automatically while it plays. Not yet from the page: parameter locks, the free take, capture, note repeat, FILL. */

#define SQ_STEPS 64
#define SQ_NOTES 6
#define SQ_BANKS 8
#define SQ_MAGIC 0x53514231u                  /* "SQB1" */
typedef struct {
    uint8_t n[SQ_NOTES], len[SQ_NOTES], lvl[SQ_NOTES];   /* pitch, length in quarter steps, force 0-4 */
    uint8_t cnt, ratchet, chance, flags;      /* notes, 1-4, 0-100 %, bit 0 slide, bits 1-2 fill (1 FILL, 2 NOT FILL) */
    int8_t nudge;                             /* -32..31, 64ths of a step */
} sq_step_t;
typedef struct {
    uint32_t magic;
    uint8_t len, div, swing, gate, spice, dice, rsv0, rsv1;   /* swing gate spice: 0-100 */
    sq_step_t s[SQ_STEPS];
} sq_bank_t;
typedef struct {
    uint32_t magic;
    int8_t bank[16], trn[16];                 /* bank -1: the song's end */
    uint8_t rep[16];
    int8_t ms[4][16];                         /* MSEQ: -100..100 */
    uint8_t ms_rate, ms_smooth[4], rsv[3];
} sq_song_t;
static sq_bank_t sqb[SQ_BANKS];
static sq_song_t sqs;
static const char *const SQ_DIVS[10] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/2", "1BAR", "2BAR", "4BAR"};
static const float SQ_DIV16[10] = {4.0f, 2.0f, 1.0f, 0.5f, 4.0f / 3.0f, 2.0f / 3.0f, 8.0f, 16.0f, 32.0f, 64.0f};
static const uint8_t SQ_DIVTICK[10] = {24, 12, 6, 3, 16, 4, 48, 96, 192, 255};   /* MIDI clock ticks a step */
static const char *const SQ_LEVELS[5] = {"GHOST", "SOFT", "NORM", "HARD", "ACC"};
static const float SQ_LVEL[5] = {0.32f, 0.55f, 0.75f, 0.9f, 1.0f};
static const char *const MS_RATES[7] = {"1/32", "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS"};
static const float MS_BEATS[7] = {0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f};

static void sq_blank_step(sq_step_t *s)
{
    memset(s, 0, sizeof *s);
    s->ratchet = 1;
    s->chance = 100;
}
static void sq_blank_bank(sq_bank_t *b)
{
    int i;
    memset(b, 0, sizeof *b);
    b->magic = SQ_MAGIC;
    b->len = 16;
    b->div = 2;
    b->gate = 50;
    for (i = 0; i < SQ_STEPS; i++)
        sq_blank_step(&b->s[i]);
}
static void sq_blank_song(void)
{
    int i, j;
    memset(&sqs, 0, sizeof sqs);
    sqs.magic = SQ_MAGIC;
    for (i = 0; i < 16; i++) {
        sqs.bank[i] = -1;
        sqs.rep[i] = 1;
    }
    sqs.ms_rate = 2;
    for (j = 0; j < 16; j++) {                /* the page's starting lanes: RAMP+, TRI, SQR, RND */
        float f = (float)j / 15.0f, tri = 1.0f - fm_fabsf((float)j / 7.5f - 1.0f) * 2.0f;
        sqs.ms[0][j] = (int8_t)((f * 2.0f - 1.0f) * 100.0f);
        sqs.ms[1][j] = (int8_t)(tri * 100.0f);
        sqs.ms[2][j] = (int8_t)(j % 8 < 4 ? 100 : -100);
        sqs.ms[3][j] = (int8_t)(((j * 37 + 11) % 17) * 12 - 96);
    }
    sqs.ms_smooth[0] = sqs.ms_smooth[1] = sqs.ms_smooth[3] = 30;
}
static int sq_bank_empty(int b)
{
    int i;
    for (i = 0; i < sqb[b].len; i++)
        if (sqb[b].s[i].cnt)
            return 0;
    return 1;
}

/* the store */
static void sq_load_all(void)
{
    int b;
    for (b = 0; b < SQ_BANKS; b++)
        if (plat_store_load(OBJ_BANK0 + (uint32_t)b, &sqb[b], sizeof sqb[b]) != (int)sizeof sqb[b] || sqb[b].magic != SQ_MAGIC
            || sqb[b].len < 1 || sqb[b].len > SQ_STEPS || sqb[b].div > 9)
            sq_blank_bank(&sqb[b]);
    if (plat_store_load(OBJ_SONG, &sqs, sizeof sqs) != (int)sizeof sqs || sqs.magic != SQ_MAGIC || sqs.ms_rate > 6)
        sq_blank_song();
}
static uint8_t sq_dirty;                      /* banks changed since the last save (bit per bank), bit 8 the song */
static int sq_save_all(void)
{
    int b, e = 0;
    for (b = 0; b < SQ_BANKS; b++)
        if (sq_dirty >> b & 1u)
            e |= plat_store_save(OBJ_BANK0 + (uint32_t)b, &sqb[b], sizeof sqb[b]);
    e |= plat_store_save(OBJ_SONG, &sqs, sizeof sqs);
    if (!e)
        sq_dirty = 0;
    return e;
}

/* ------------------------------------------------------------------ the transport (the audio interrupt's) --- */
static volatile struct {
    int run, rec, armed;                      /* playing; recording into it; REC pressed while stopped */
    int bank, queue;                          /* the bank playing; the next one (-1 none) */
    int song_on, song_row, song_rep, trn;
    int cur;                                  /* the step playing, -1 stopped */
    uint32_t cur_at;                          /* its frame */
    float dur;                                /* frames a step */
    int req;                                  /* from the main loop: 1 start, 2 stop */
    uint32_t pass;                            /* passes played (the screen's bar count) */
    uint8_t jam[64];                          /* the banks of the last passes (SONG: ALGO left makes rows of them) */
    int njam;
} sq = {0, 0, 0, 0, -1, 0, 0, 0, 0, -1, 0, 0.0f, 0, 0, {0}, 0};
static uint32_t sq_next_at, sq_step_i, sq_ticks;
static int sq_first;                          /* the first pass: no loop end before it */
static uint32_t sq_flash[SQ_STEPS];           /* pl_now when each step last played (the screen) */
static uint8_t sq_on[128];                    /* notes the sequencer holds on */
typedef struct { uint32_t at; uint8_t note, on; float vel; } sq_ev_t;
#define SQ_NPEND 96
static sq_ev_t sq_pend[SQ_NPEND];
static int sq_npend;

static void sq_push(uint32_t at, int note, int on, float vel)
{
    if (sq_npend >= SQ_NPEND || note < 0 || note > 127)
        return;
    sq_pend[sq_npend].at = at;
    sq_pend[sq_npend].note = (uint8_t)note;
    sq_pend[sq_npend].on = (uint8_t)on;
    sq_pend[sq_npend].vel = vel;
    sq_npend++;
}
static float sq_step_frames(void)
{
    const sq_bank_t *b = &sqb[sq.bank];
    if (GI(G_CLOCK) == 1 && pa.tick_frames > 0.0f)
        return pa.tick_frames * SQ_DIV16[b->div] * 6.0f;
    return 60.0f / tempo_bpm() / 4.0f * SQ_DIV16[b->div] * PL_HZ;
}
static float hash01(uint32_t a, uint32_t b)   /* the page's hash01: SPICE's variation, the same each pass */
{
    uint32_t h = a * 374761393u + b * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)(h ^ (h >> 16)) / 4294967296.0f;
}
static void sq_all_off(void)
{
    int i;
    sq_npend = 0;
    for (i = 0; i < 128; i++)
        while (sq_on[i]) {
            sq_on[i]--;
            snd_off(i, 1, 1);
        }
}
static void sq_song_row(int r)
{
    sq.song_row = r;
    sq.song_rep = 0;
    sq.trn = sqs.trn[r];
    if (sqs.bank[r] >= 0)
        sq.bank = sqs.bank[r];
}
static int sq_first_row(void)
{
    int r;
    for (r = 0; r < 16; r++)
        if (sqs.bank[r] >= 0)
            return r;
    return 0;
}
static void sq_loop_end(void)                 /* the end of a pass: the song's next row, or the queued bank */
{
    sq.pass++;
    if (sq.njam < 64)
        sq.jam[sq.njam++] = (uint8_t)sq.bank;
    if (sq.song_on) {
        if (++sq.song_rep >= sqs.rep[sq.song_row]) {
            int r = sq.song_row + 1;
            while (r < 16 && sqs.bank[r] < 0)
                r++;
            sq_song_row(r < 16 ? r : sq_first_row());
        }
        return;
    }
    if (sq.queue >= 0) {
        sq.bank = sq.queue;
        sq.queue = -1;
    }
}
/* one step, its grid frame tg: its notes (SPICE, chance, ratchets, nudge, slide) into the queue */
static void sq_step(int i, uint32_t tg, float d)
{
    const sq_bank_t *b = &sqb[sq.bank];
    const sq_step_t *s = &b->s[i];
    float sw = (b->div != 4 && b->div != 5 && (i & 1)) ? (float)b->swing * 0.005f * d : 0.0f;
    float sp = (float)b->spice * 0.01f, spv = 0.0f, spg = 0.0f;
    int oct = 0, r, k, j;
    uint32_t t;
    sq.cur = i;
    sq.cur_at = tg;
    sq.dur = d;
    sq_flash[i] = tg;
    if (!s->cnt || ((s->flags >> 1) & 3u) == 1u)   /* FILL steps: no fill yet on the FM-1, so not played */
        return;
    if (sp > 0.0f) {
        if (hash01(b->dice, (uint32_t)i * 4u) < sp * 0.3f)
            return;                           /* a rest */
        if (hash01(b->dice, (uint32_t)i * 4u + 1u) < sp * 0.25f)
            oct = hash01(b->dice, (uint32_t)i * 4u + 2u) < 0.5f ? -12 : 12;
        spv = (hash01(b->dice, (uint32_t)i * 4u + 3u) - 0.5f) * sp * 0.6f;
        spg = (hash01(b->dice + 7u, (uint32_t)i) - 0.5f) * sp;
    }
    if (s->chance < 100 && rand01() * 100.0f >= (float)s->chance)
        return;
    t = tg + (uint32_t)(int32_t)(sw + (float)s->nudge / 64.0f * d);   /* (a nudge back: before the grid) */
    r = s->ratchet < 1 ? 1 : s->ratchet > 4 ? 4 : s->ratchet;
    for (j = 0; j < s->cnt && j < SQ_NOTES; j++) {
        int pitch = s->n[j] + oct + (sq.song_on ? sq.trn : 0);
        float vel = SQ_LVEL[s->lvl[j] > 4 ? 2 : s->lvl[j]] + spv;
        vel = vel < 0.05f ? 0.05f : vel > 1.0f ? 1.0f : vel;
        for (k = 0; k < r; k++) {
            uint32_t tr = t + (uint32_t)((float)k * d / (float)r);
            float len = r > 1 ? d / (float)r * 0.55f : (float)s->len[j] * 0.25f * d * (1.0f + spg * 0.5f);
            if (len < 882.0f) len = 882.0f;
            if ((s->flags & 1u) && r == 1 && len < d + 1323.0f)
                len = d + 1323.0f;            /* slide: overlap the next, so a mono voice glides */
            sq_push(tr, pitch, 1, vel);
            sq_push(tr + (uint32_t)len, pitch, 0, 0.0f);
        }
    }
}

/* MSEQ: each lane's value at its step, into the engine's external sources (SEQ 1-4) */
static uint32_t ms_next_at, ms_step;
static volatile int ms_cur = -1;

/* each block, in the interrupt (after the arp): start / stop, the steps that begin before the block ends, the queue */
static void sq_block(uint32_t n)
{
    uint32_t end = pl_now + n;
    int i, j;
    if (rc_audio.pause_req)
        return;
    if (sq.req == 1 && !sq.run) {
        sq.run = 1;
        sq_step_i = 0;
        sq_ticks = 0;
        sq_first = 1;
        sq.pass = 0;
        sq.njam = 0;
        sq_next_at = pl_now;
        ms_next_at = pl_now;
        ms_step = 0;
        if (sq.song_on)
            sq_song_row(sq_first_row());
        if (sq.armed) {
            sq.armed = 0;
            sq.rec = 1;
        }
        if (GI(G_MOUT) && GI(G_CLOCK) != 1)
            plat_midi_send(0x0Fu | 0xFAu << 8, TO_USB);
    } else if (sq.req == 2 && sq.run) {
        sq.run = 0;
        sq.rec = 0;
        sq.cur = -1;
        sq_all_off();
        if (GI(G_MOUT) && GI(G_CLOCK) != 1)
            plat_midi_send(0x0Fu | 0xFCu << 8, TO_USB);
    }
    sq.req = 0;
    if (sq.run && GI(G_CLOCK) != 1) {
        float d = sq_step_frames();
        while ((int32_t)(sq_next_at - end) < 0) {
            if (sq_step_i >= sqb[sq.bank].len)
                sq_step_i = 0;
            if (sq_step_i == 0) {
                if (sq_first) sq_first = 0;
                else sq_loop_end();
                d = sq_step_frames();
            }
            sq_step((int)sq_step_i, sq_next_at, d);
            sq_next_at += (uint32_t)d;
            sq_step_i++;
        }
    }
    {                                         /* MSEQ, always on the tempo while the sound runs */
        float md = MS_BEATS[sqs.ms_rate > 6 ? 2 : sqs.ms_rate] * 60.0f / tempo_bpm() * PL_HZ;
        if ((int32_t)(ms_next_at - pl_now) < -(int32_t)(md * 4.0f))
            ms_next_at = pl_now;
        while ((int32_t)(ms_next_at - end) < 0) {
            int l;
            for (l = 0; l < 4; l++)
                rk_set_ext(l, (float)sqs.ms[l][ms_step & 15u] * 0.01f);
            ms_cur = (int)(ms_step & 15u);
            ms_step++;
            ms_next_at += (uint32_t)md;
        }
    }
    for (i = 0; i < sq_npend;) {
        sq_ev_t e = sq_pend[i];
        if ((int32_t)(e.at - end) < 0) {
            for (j = i; j < sq_npend - 1; j++)
                sq_pend[j] = sq_pend[j + 1];
            sq_npend--;
            if (e.on) {
                if (sq_on[e.note]) {          /* the same pitch still ringing: end it first */
                    sq_on[e.note]--;
                    snd_off(e.note, 1, 1);
                }
                sq_on[e.note]++;
                snd_on(e.note, e.vel, 1, 1);
            } else if (sq_on[e.note]) {
                sq_on[e.note]--;
                snd_off(e.note, 1, 1);
            }
        } else {
            i++;
        }
    }
}
/* CLOCK MIDI: a tick (from pl_clock_tick) steps the sequencer */
static void sq_clock_tick(void)
{
    const sq_bank_t *b;
    uint32_t per;
    if (!sq.run || GI(G_CLOCK) != 1)
        return;
    b = &sqb[sq.bank];
    per = SQ_DIVTICK[b->div];
    if (sq_ticks++ % per == 0u) {
        if (sq_step_i >= b->len)
            sq_step_i = 0;
        if (sq_step_i == 0) {
            if (sq_first) sq_first = 0;
            else sq_loop_end();
        }
        sq_step((int)sq_step_i, pl_now, sq_step_frames());
        sq_step_i++;
    }
}

/* ------------------------------------------------------------------ notes coming in (the interrupt, pl_key_on) --- */
static volatile int sq_cursor;                /* the NOTES page's step (the main loop's) */
static volatile int sq_entry_on;              /* 1: the NOTES page is showing and stopped: keys write the cursor's step */
static int sq_entry_step = -1, sq_entry_held;
static volatile int sq_entry_done;            /* the keys of an entry all up: the main loop moves the cursor on */
static uint8_t sq_last[SQ_NOTES], sq_nlast = 1;   /* the last notes played (a step turned on in the layer gets them) */
static struct { int raw, step; uint32_t at; } sq_rec[8];

static void sq_write(sq_step_t *s, const uint8_t *notes, int n, int len)
{
    int i, j;
    for (i = 0; i < n; i++) {
        for (j = 0; j < s->cnt; j++)
            if (s->n[j] == notes[i])
                break;
        if (j == s->cnt) {
            if (s->cnt >= SQ_NOTES) {         /* full: the oldest goes */
                for (j = 0; j < SQ_NOTES - 1; j++) {
                    s->n[j] = s->n[j + 1];
                    s->len[j] = s->len[j + 1];
                    s->lvl[j] = s->lvl[j + 1];
                }
                s->cnt = SQ_NOTES - 1;
            }
            j = s->cnt++;
        }
        s->n[j] = notes[i];
        s->len[j] = (uint8_t)len;
        s->lvl[j] = 2;
    }
    for (i = 1; i < s->cnt; i++)              /* low to high */
        for (j = i; j > 0 && s->n[j - 1] > s->n[j]; j--) {
            uint8_t a = s->n[j], l = s->len[j], v = s->lvl[j];
            s->n[j] = s->n[j - 1]; s->len[j] = s->len[j - 1]; s->lvl[j] = s->lvl[j - 1];
            s->n[j - 1] = a; s->len[j - 1] = l; s->lvl[j - 1] = v;
        }
}
/* a key's notes (after SEL): kept as the last notes; recorded at the step playing; or written at the cursor */
static void sq_note_in(int raw, const uint8_t *notes, int n)
{
    sq_bank_t *b = &sqb[sq.bank];
    int i, gate4 = (b->gate * 4 + 50) / 100;
    if (gate4 < 1) gate4 = 1;
    if (n > 0) {
        for (i = 0; i < n && i < SQ_NOTES; i++)
            sq_last[i] = notes[i];
        sq_nlast = (uint8_t)(n < SQ_NOTES ? n : SQ_NOTES);
    }
    if (sq.armed && !sq.run && n > 0) {      /* REC READY: the first note starts it */
        sq.req = 1;
        sq.run = 0;
        sq_block(0);
        sq.rec = 1;
    }
    if (sq.rec && sq.run && n > 0) {
        float pos = (float)sq.cur + (float)(pl_now - sq.cur_at) / (sq.dur > 1.0f ? sq.dur : 1.0f);
        int step = (int)(pos + 0.5f) % b->len;
        sq_write(&b->s[step], notes, n, gate4);
        sq_dirty |= (uint8_t)(1u << sq.bank);
        for (i = 0; i < 8; i++)
            if (sq_rec[i].raw < 0 || i == 7) {
                sq_rec[i].raw = raw;
                sq_rec[i].step = step;
                sq_rec[i].at = pl_now;
                break;
            }
        return;
    }
    if (sq_entry_on && !sq.run && n > 0) {
        int c = sq_cursor < b->len ? sq_cursor : 0;
        if (sq_entry_step != c) {             /* a new step: what it had goes */
            sq_entry_step = c;
            b->s[c].cnt = 0;
        }
        sq_write(&b->s[c], notes, n, gate4);
        sq_entry_held++;
        sq_dirty |= (uint8_t)(1u << sq.bank);
    }
}
static void sq_note_out(int raw)
{
    int i;
    for (i = 0; i < 8; i++)                   /* a recorded note's length: as long as it was held */
        if (sq_rec[i].raw == raw) {
            sq_step_t *s = &sqb[sq.bank].s[sq_rec[i].step];
            float steps = (float)(pl_now - sq_rec[i].at) / (sq.dur > 1.0f ? sq.dur : 1.0f);
            int q = (int)(steps * 4.0f + 0.5f), j;
            q = q < 1 ? 1 : q > 255 ? 255 : q;
            for (j = 0; j < s->cnt; j++)
                s->len[j] = (uint8_t)q;
            sq_rec[i].raw = -1;
        }
    if (sq_entry_held > 0 && --sq_entry_held == 0) {   /* the entry's keys all up: the next step (here, so a key */
        sq_cursor = (sq_entry_step + 1) % sqb[sq.bank].len;   /* pressed in the same block goes there) */
        sq_entry_done = 1;
        sq_entry_step = -1;
    }
}
static void sq_reset_counts(void) { memset(sq_on, 0, sizeof sq_on); sq_npend = 0; }
static void sq_init(void)
{
    int i;
    for (i = 0; i < 8; i++)
        sq_rec[i].raw = -1;
    sq_load_all();
    sq_last[0] = 60;
}
