/*
 * Strip compositor for the 240x240 ST7789.
 *
 * The panel takes opaque pixels, so translucency has to happen somewhere
 * we own. A full 240x240 RGB565 framebuffer is 115,200 B, which does not
 * fit alongside BLE and the thread stacks. Instead we composite one
 * 240 x GFX_STRIP_H band at a time (5,760 B), flush it, and move down.
 *
 * A shorter band costs nothing extra per full repaint - the same pixels
 * go down the bus - but it halves the buffer and makes partial repaints
 * finer, so both axes improve together.
 *
 * gfx_render() calls your draw function once per band. Draw the whole
 * scene every time — every op clips itself to the live band, so the ones
 * off-band cost a compare and return.
 *
 * Colours are native-endian RGB565 in the band buffer and byte-swapped
 * once on flush, because this panel reads high byte first.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef UI_GFX_H
#define UI_GFX_H

#include <stdint.h>
#include <stdbool.h>

#define GFX_W        240
#define GFX_H        240
#define GFX_STRIP_H   12            /* 240 % 12 == 0, 5,760 B buffer    */

typedef uint16_t gfx_color;

/* Alpha is 0-255. 255 is opaque. */
#define GFX_OPAQUE   255u

/* ── colour ────────────────────────────────────────────────────────────*/
static inline gfx_color gfx_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (gfx_color)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}
static inline gfx_color gfx_hex(uint32_t rgb888) {
    return gfx_rgb((uint8_t)(rgb888 >> 16), (uint8_t)(rgb888 >> 8), (uint8_t)rgb888);
}

/* ── lifecycle ─────────────────────────────────────────────────────────*/
int  gfx_init(void);                       /* 0 on success              */
bool gfx_ready(void);

typedef void (*gfx_draw_fn)(void *ctx);

/* Repaint the whole panel. */
void gfx_render(gfx_draw_fn draw, void *ctx);

/* Repaint only the bands touching [y0, y1). `draw` is still called with the
 * full scene - ops clip themselves - but bands outside the range are never
 * composited or pushed. A status row changing costs 2 bands over SPI
 * instead of 10. */
void gfx_render_range(gfx_draw_fn draw, void *ctx, int y0, int y1);

/* Band the ops are currently clipped to. Useful for skipping expensive
 * scene setup when a subtree is entirely off-band. */
int  gfx_band_y0(void);
int  gfx_band_y1(void);                    /* exclusive                 */

/* ── primitives (all clip to the live band) ────────────────────────────*/
void gfx_clear(gfx_color c);
void gfx_vgrad(int y0, int y1, gfx_color top, gfx_color bot);
void gfx_rect(int x, int y, int w, int h, gfx_color c, uint8_t a);
void gfx_hline(int x, int y, int w, gfx_color c, uint8_t a);
void gfx_vline(int x, int y, int h, gfx_color c, uint8_t a);

/* Anti-aliased rounded rectangle. r == 0 gives a plain rect. */
void gfx_round_rect(int x, int y, int w, int h, int r, gfx_color c, uint8_t a);

/* The signature panel: tint + lit top edge + shaded bottom edge. */
void gfx_glass(int x, int y, int w, int h, int r);

/* Horizontal capsule meter, 0..100. */
void gfx_meter(int x, int y, int w, int h, uint8_t pct, gfx_color fill, gfx_color track);

/* ── text (5x7 packed bitmap font, integer scaled) ─────────────────────*/
int  gfx_text_w(const char *s, int scale);          /* pixels, no trailing gap */
int  gfx_text_h(int scale);
void gfx_text(int x, int y, const char *s, int scale, gfx_color c, uint8_t a);
void gfx_text_c(int cx, int y, const char *s, int scale, gfx_color c, uint8_t a); /* centred on cx */

#endif /* UI_GFX_H */
