/* FM1 Rack's colours (update 51): one place for the palette, so it can be judged and changed as a whole.
 *
 * Josh: the old look (bright pastels on black, a pill of colour on every page, neon halos) read as a "common AI
 * colour scheme". The guidance gathered for update 50 says the same thing more politely: a small palette, neutral
 * chrome, colour kept for what it means. So every theme here has
 *   - greys with a temperature of their own (GREY(n): n is the brightness, 0-255), not pure black and white;
 *   - colour mostly in the four knobs (K1-K4, the same on every page) and in the live pictures;
 *   - softer, less saturated hues, all at about the same brightness so none shouts.
 * FM1_THEME picks one (build with -DFM1_THEME=n; the default is the one shipped). */
#pragma once
#ifndef FM1_THEME
#define FM1_THEME 3
#endif

#if FM1_THEME == 1
/* 1 GRAPHITE: cool greys, muted mineral colours, each section its own quiet hue */
#define GREY(n) RGB((n) * 92 / 100, (n) * 96 / 100, (n))
#define K_SRC RGB(212, 162, 96)               /* ochre */
#define K_SHP RGB(104, 172, 164)              /* verdigris */
#define K_MOD RGB(158, 140, 196)              /* dusty violet */
#define K_FX RGB(114, 152, 198)               /* steel blue */
#define K_OSC RGB(220, 192, 146)              /* sand */
#define K_AMP RGB(214, 112, 92)               /* terracotta */
#define K_LFO RGB(174, 156, 206)
#define K_MX RGB(214, 184, 122)
#define K_CHD RGB(150, 186, 122)              /* moss */
#define K_ARP RGB(122, 176, 206)
#define K_SEQC RGB(210, 132, 160)             /* rose */
#define KN1 RGB(108, 148, 204)
#define KN2 RGB(118, 176, 138)
#define KN3 RGB(214, 212, 206)
#define KN4 RGB(228, 128, 76)
#define L_BASS K_SRC
#elif FM1_THEME == 2
/* 2 INSTRUMENT: warm greys and cream, after a Braun panel: earth colours and one signal orange */
#define GREY(n) RGB((n), (n) * 96 / 100, (n) * 89 / 100)
#define K_SRC RGB(222, 170, 78)               /* amber */
#define K_SHP RGB(120, 158, 136)              /* sage */
#define K_MOD RGB(166, 128, 150)              /* plum */
#define K_FX RGB(120, 146, 172)               /* slate */
#define K_OSC RGB(222, 196, 150)
#define K_AMP RGB(222, 98, 62)                /* signal orange */
#define K_LFO RGB(176, 146, 168)
#define K_MX RGB(222, 186, 116)
#define K_CHD RGB(156, 172, 104)              /* olive */
#define K_ARP RGB(128, 164, 182)
#define K_SEQC RGB(206, 128, 120)             /* brick rose */
#define KN1 RGB(102, 134, 168)
#define KN2 RGB(138, 162, 108)
#define KN3 RGB(226, 216, 196)
#define KN4 RGB(234, 112, 54)
#define L_BASS K_SRC
#else
/* 3 IVORY: neutral greys; the pictures drawn in ivory; the only colours are the four knobs' (and warnings) */
#define GREY(n) RGB((n), (n) * 98 / 100, (n) * 94 / 100)
#define K_SRC RGB(232, 226, 210)
#define K_SHP RGB(232, 226, 210)
#define K_MOD RGB(232, 226, 210)
#define K_FX RGB(232, 226, 210)
#define K_OSC RGB(232, 226, 210)
#define K_AMP RGB(232, 226, 210)
#define K_LFO RGB(232, 226, 210)
#define K_MX RGB(232, 226, 210)
#define K_CHD RGB(170, 198, 150)              /* the playing pages keep a faint tint: SEL sage, ARP sky, SEQ rose */
#define K_ARP RGB(150, 184, 210)
#define K_SEQC RGB(214, 162, 180)
#define L_BASS RGB(214, 166, 100)             /* STYLE's bass layer (RHYTHM's rings), beside CHORD and ARP */
#define KN1 RGB(86, 128, 214)                 /* cobalt */
#define KN2 RGB(84, 176, 126)                 /* jade */
#define KN3 RGB(226, 196, 104)                /* pale chrome yellow (white is the drawings' now) */
#define KN4 RGB(232, 100, 64)                 /* vermilion */
#endif

/* the greys: the same steps in every theme, its temperature from GREY */
#define K_BG GREY(7)
#define K_WELL GREY(10)
#define K_BAR GREY(17)
#define K_CELL GREY(21)
#define K_OFF GREY(25)
#define K_GRID GREY(30)
#define K_SEL GREY(34)
#define K_MID GREY(46)
#define K_FAINT GREY(60)
#define K_DIM GREY(150)
#define K_FG GREY(232)
#define K_INK GREY(18)                        /* words on a light band */
#define K_GLO GREY(200)
/* what things mean, in every theme */
#define K_WARN RGB(232, 96, 78)
#define K_OK RGB(110, 192, 136)
#define K_GOLD RGB(226, 190, 118)
#define K_INKACC RGB(120, 92, 40)             /* a light band's second words */
/* the matrix's sources, by kind (mod_colour) */
#define MC_MACRO K_GOLD
#define MC_VEL K_OK
#define MC_KEY RGB(110, 176, 214)
#define MC_RAND K_FG
#define MC_ENV RGB(222, 116, 96)
#define MC_MOD RGB(224, 150, 72)
#define MC_LFO RGB(164, 136, 220)
#define MC_MSEQ RGB(214, 120, 170)
