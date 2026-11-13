/* FM1 Rack's look (update 51; update 53: split into choices, one macro each, and set from Josh's A/B picks:
 * near-black, dusty knob colours, a quiet colour per section, hairlines with a faint glow, no header tag, hairline
 * sliders with a dot, bevelled cards with no grid, the lighter type).
 *
 * Josh: the old look (bright pastels on black, a pill of colour on every page, neon halos) read as a "common AI
 * colour scheme". Update 51 settled the direction: neutral chrome, colour kept for what it means (mostly the four
 * knobs, K1-K4, the same on every page), softer hues of about one brightness, thin lines. Update 53 makes each part of
 * that a switch, so the A/B rounds can be rendered from the same code:
 *   FM1_BG      1 near-black   2 charcoal      3 slate (cool, lifted)
 *   FM1_KNOBS   1 cobalt/jade/yellow/vermilion   2 primaries   3 dusty pastels   4 one warm family (amber to ivory)
 *   FM1_PIC     1 pictures in ivory   2 each section its own quiet colour   3 amber, like a phosphor screen
 *   FM1_LINE    1 hairline, no halo   2 hairline, faint halo   3 two pixels, soft
 *   FM1_HEAD    1 tag outlined   2 no tag   3 tag as coloured words
 *   FM1_SLIDER  1 line and thumb   2 segments   3 bar, no thumb   4 hairline and a dot
 *   FM1_WELL    1 dark well, dot grid   2 no well   3 well with a hairline edge, no grid
 * (the typeface's weight is make_font.py's FM1_FONTW) */
#pragma once
#ifndef FM1_BG
#define FM1_BG 1
#endif
#ifndef FM1_KNOBS
#define FM1_KNOBS 3
#endif
#ifndef FM1_PIC
#define FM1_PIC 2
#endif
#ifndef FM1_LINE
#define FM1_LINE 2
#endif
#ifndef FM1_HEAD
#define FM1_HEAD 2
#endif
#ifndef FM1_SLIDER
#define FM1_SLIDER 4
#endif
#ifndef FM1_WELL
#define FM1_WELL 6
#endif

/* the greys: GREY(n), n the brightness 0-255, with the background's temperature; L(n) lifts the dark steps */
#if FM1_BG == 3
#define GREY(n) RGB((n) * 88 / 100, (n) * 94 / 100, (n))
#define L(n) ((n) + 14)
#elif FM1_BG == 2
#define GREY(n) RGB((n), (n) * 98 / 100, (n) * 95 / 100)
#define L(n) ((n) + 12)
#else
#define GREY(n) RGB((n), (n) * 98 / 100, (n) * 94 / 100)
#define L(n) (n)
#endif
/* (update 53, after Josh: "graphics need to be separated from the background without losing clarity") FM1_WELL
 * 4-6 set the page and the picture panels apart by brightness and an edge, not by blur:
 *   4 a lifted card (the panel lighter than the page, a hairline edge)   5 an inset (a lifted page, the panel deeper
 *   than it, a lighter edge)   6 a bevelled card (lit top edge, dark bottom edge, like a raised panel) */
#if FM1_WELL == 4
#define K_BG GREY(5)
#define K_WELL GREY(22)
#define K_EDGE GREY(46)
#elif FM1_WELL == 5
#define K_BG GREY(24)
#define K_WELL GREY(5)
#define K_EDGE GREY(54)
#elif FM1_WELL == 6
#define K_BG GREY(7)
#define K_WELL GREY(20)
#define K_EDGE GREY(34)
#define K_EDGE_HI GREY(64)
#define K_EDGE_LO GREY(2)
#else
#define K_BG GREY(L(7))
#define K_WELL GREY(L(10))
#define K_EDGE GREY(L(30))
#endif
#define K_BAR GREY(L(17))
#define K_CELL GREY(L(21))
#define K_OFF GREY(L(25))
#define K_GRID GREY(L(30))
#define K_SEL GREY(L(34))
#define K_MID GREY(L(46))
#define K_FAINT GREY(L(60))
#define K_DIM GREY(150)
#define K_FG GREY(232)
#define K_INK GREY(18)                        /* words on a light band */
#define K_GLO GREY(200)

/* the four knobs */
#if FM1_KNOBS == 2
#define KN1 RGB(60, 120, 230)                 /* primaries, after the OP-1's */
#define KN2 RGB(60, 190, 100)
#define KN3 RGB(220, 220, 214)
#define KN4 RGB(246, 110, 40)
#elif FM1_KNOBS == 3
#define KN1 RGB(132, 158, 196)                /* dusty: grey-blue, sage, sand, clay */
#define KN2 RGB(140, 178, 148)
#define KN3 RGB(214, 196, 150)
#define KN4 RGB(206, 136, 112)
#elif FM1_KNOBS == 4
#define KN1 RGB(236, 156, 64)                 /* one warm family: amber, gold, cream, ivory */
#define KN2 RGB(230, 190, 96)
#define KN3 RGB(236, 216, 160)
#define KN4 RGB(244, 236, 214)
#else
#define KN1 RGB(86, 128, 214)                 /* cobalt */
#define KN2 RGB(84, 176, 126)                 /* jade */
#define KN3 RGB(226, 196, 104)                /* pale chrome yellow (white is the drawings') */
#define KN4 RGB(232, 100, 64)                 /* vermilion */
#endif

/* the pictures and the sections' colours */
#if FM1_PIC == 2
#define K_SRC RGB(212, 162, 96)               /* ochre */
#define K_SHP RGB(104, 172, 164)              /* verdigris */
#define K_MOD RGB(158, 140, 196)              /* dusty violet */
#define K_FX RGB(114, 152, 198)               /* steel blue */
#define K_OSC RGB(220, 192, 146)
#define K_AMP RGB(214, 112, 92)               /* terracotta */
#define K_LFO RGB(174, 156, 206)
#define K_MX RGB(214, 184, 122)
#elif FM1_PIC == 3
#define K_SRC RGB(240, 176, 80)               /* phosphor amber, everything */
#define K_SHP K_SRC
#define K_MOD K_SRC
#define K_FX K_SRC
#define K_OSC K_SRC
#define K_AMP K_SRC
#define K_LFO K_SRC
#define K_MX K_SRC
#else
#define K_SRC RGB(232, 226, 210)              /* ivory */
#define K_SHP K_SRC
#define K_MOD K_SRC
#define K_FX K_SRC
#define K_OSC K_SRC
#define K_AMP K_SRC
#define K_LFO K_SRC
#define K_MX K_SRC
#endif
#define K_CHD RGB(170, 198, 150)              /* the playing pages keep a faint tint: SEL sage, ARP sky, SEQ rose */
#define K_ARP RGB(150, 184, 210)
#define K_SEQC RGB(214, 162, 180)
#define L_BASS RGB(214, 166, 100)             /* STYLE's bass layer (RHYTHM's rings), beside CHORD and ARP */

/* what things mean, whatever the look */
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

/* lines: how strong a line's core and its halo are (vis.c) */
#if FM1_LINE == 1
#define VF_LINE 224u
#define VF_HALO 0u
#elif FM1_LINE == 3
#define VF_LINE 232u
#define VF_HALO 120u
#elif FM1_LINE == 4                           /* a crisp line with a dark keyline either side: it stands off the grid */
#define VF_LINE 236u
#define VF_HALO 200u
#define VF_HALO_C(c) K_WELL
#else
#define VF_LINE 218u
#define VF_HALO 26u
#endif
#ifndef VF_HALO_C
#define VF_HALO_C(c) (c)                      /* the halo's colour: the line's own (FM1_LINE 4: a dark keyline) */
#endif
