/*
 * Strip compositor. See gfx.h for the why.
 *
 * SPDX-License-Identifier: MIT
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>

#include "gfx.h"
#include "theme.h"
#include "font5x7.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* 240 * 12 * 2 = 5,760 B. Static, so a failed allocation can never take
 * the dongle down at boot the way an unchecked k_malloc would. */
static uint16_t band[GFX_W * GFX_STRIP_H];
static const struct device *disp;
static int band_y0;
static bool ready;

int gfx_band_y0(void) { return band_y0; }
int gfx_band_y1(void) { return band_y0 + GFX_STRIP_H; }
bool gfx_ready(void)  { return ready; }

int gfx_init(void) {
    disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(disp)) {
        LOG_ERR("gfx: display not ready");
        ready = false;
        return -1;
    }
    ready = true;
    return 0;
}

/* -- blending ------------------------------------------------------- */
/* x/255 without a divide: (t + (t>>8) + 1) >> 8. Off by at most 1/255,
 * invisible in RGB565, and roughly 3x cheaper - this runs per pixel per
 * band, so the divides were costing real milliseconds per repaint. */
static inline uint32_t div255(uint32_t t) { return (t + (t >> 8) + 1u) >> 8; }

static inline uint16_t mix(uint16_t d, uint16_t s, uint8_t a) {
    if (a == 0)   { return d; }
    if (a >= 255) { return s; }
    uint32_t ia = 255u - a;
    uint32_t r = div255((((s >> 11) & 0x1Fu) * a) + (((d >> 11) & 0x1Fu) * ia));
    uint32_t g = div255((((s >>  5) & 0x3Fu) * a) + (((d >>  5) & 0x3Fu) * ia));
    uint32_t b = div255(((  s       & 0x1Fu) * a) + ((  d       & 0x1Fu) * ia));
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static inline void px(int x, int y, gfx_color c, uint8_t a) {
    if (x < 0 || x >= GFX_W) { return; }
    int by = y - band_y0;
    if (by < 0 || by >= GFX_STRIP_H) { return; }
    uint16_t *p = &band[by * GFX_W + x];
    *p = mix(*p, c, a);
}

/* -- render pass ---------------------------------------------------- */
/* Software rotation, on top of the 270 deg MADCTL mount correction that
 * display_rotate_init.c applies at boot. Set CONFIG_ROTATE_DISPLAY in the
 * config repo: 0, 90, 180 or 270. All drawing stays in logical coordinates;
 * only the flush knows the panel is turned. */
#ifndef CONFIG_ROTATE_DISPLAY
#define CONFIG_ROTATE_DISPLAY 0
#endif

#if CONFIG_ROTATE_DISPLAY != 0
/* A rotated band is a different shape, so the transpose needs a destination.
 * Costs nothing when CONFIG_ROTATE_DISPLAY is 0. */
static uint16_t obuf[GFX_W * GFX_STRIP_H];
#endif

static void flush(void) {
#if CONFIG_ROTATE_DISPLAY == 90
    for (int ly = 0; ly < GFX_STRIP_H; ly++) {
        for (int lx = 0; lx < GFX_W; lx++) {
            obuf[lx * GFX_STRIP_H + (GFX_STRIP_H - 1 - ly)] =
                __builtin_bswap16(band[ly * GFX_W + lx]);
        }
    }
    struct display_buffer_descriptor d = {
        .buf_size = sizeof(obuf), .width = GFX_STRIP_H,
        .height = GFX_H, .pitch = GFX_STRIP_H,
    };
    display_write(disp, GFX_H - GFX_STRIP_H - band_y0, 0, &d, (uint8_t *)obuf);

#elif CONFIG_ROTATE_DISPLAY == 180
    for (int ly = 0; ly < GFX_STRIP_H; ly++) {
        for (int lx = 0; lx < GFX_W; lx++) {
            obuf[(GFX_STRIP_H - 1 - ly) * GFX_W + (GFX_W - 1 - lx)] =
                __builtin_bswap16(band[ly * GFX_W + lx]);
        }
    }
    struct display_buffer_descriptor d = {
        .buf_size = sizeof(obuf), .width = GFX_W,
        .height = GFX_STRIP_H, .pitch = GFX_W,
    };
    display_write(disp, 0, GFX_H - GFX_STRIP_H - band_y0, &d, (uint8_t *)obuf);

#elif CONFIG_ROTATE_DISPLAY == 270
    for (int ly = 0; ly < GFX_STRIP_H; ly++) {
        for (int lx = 0; lx < GFX_W; lx++) {
            obuf[(GFX_W - 1 - lx) * GFX_STRIP_H + ly] =
                __builtin_bswap16(band[ly * GFX_W + lx]);
        }
    }
    struct display_buffer_descriptor d = {
        .buf_size = sizeof(obuf), .width = GFX_STRIP_H,
        .height = GFX_H, .pitch = GFX_STRIP_H,
    };
    display_write(disp, band_y0, 0, &d, (uint8_t *)obuf);

#else /* 0 - no software rotation */
    for (int i = 0; i < GFX_W * GFX_STRIP_H; i++) {
        band[i] = __builtin_bswap16(band[i]);   /* panel reads high byte first */
    }
    struct display_buffer_descriptor d = {
        .buf_size = sizeof(band), .width = GFX_W,
        .height = GFX_STRIP_H, .pitch = GFX_W,
    };
    display_write(disp, 0, band_y0, &d, (uint8_t *)band);
#endif
}

void gfx_render_range(gfx_draw_fn draw, void *ctx, int y0, int y1) {
    if (!ready || !draw) { return; }
    if (y0 < 0)      { y0 = 0; }
    if (y1 > GFX_H)  { y1 = GFX_H; }
    if (y1 <= y0)    { return; }

    /* snap to band boundaries - a band is the smallest unit we can push */
    int first = (y0 / GFX_STRIP_H) * GFX_STRIP_H;
    for (band_y0 = first; band_y0 < y1; band_y0 += GFX_STRIP_H) {
        draw(ctx);
        flush();
    }
}

void gfx_render(gfx_draw_fn draw, void *ctx) {
    gfx_render_range(draw, ctx, 0, GFX_H);
}

/* -- primitives ----------------------------------------------------- */
void gfx_clear(gfx_color c) {
    for (int i = 0; i < GFX_W * GFX_STRIP_H; i++) { band[i] = c; }
}

void gfx_vgrad(int y0, int y1, gfx_color top, gfx_color bot) {
    if (y1 <= y0) { return; }
    int span = (y1 - y0) > 1 ? (y1 - y0 - 1) : 1;
    int a = MAX(y0, band_y0), b = MIN(y1, gfx_band_y1());
    for (int y = a; y < b; y++) {
        uint32_t t = (uint32_t)(y - y0) * 255u / (uint32_t)span;
        uint16_t c = mix(top, bot, (uint8_t)(t > 255u ? 255u : t));
        uint16_t *row = &band[(y - band_y0) * GFX_W];
        for (int x = 0; x < GFX_W; x++) { row[x] = c; }
    }
}

void gfx_rect(int x, int y, int w, int h, gfx_color c, uint8_t a) {
    if (w <= 0 || h <= 0 || a == 0) { return; }
    int y_a = MAX(y, band_y0), y_b = MIN(y + h, gfx_band_y1());
    int x_a = MAX(x, 0),       x_b = MIN(x + w, GFX_W);
    for (int yy = y_a; yy < y_b; yy++) {
        uint16_t *row = &band[(yy - band_y0) * GFX_W];
        for (int xx = x_a; xx < x_b; xx++) { row[xx] = mix(row[xx], c, a); }
    }
}

void gfx_hline(int x, int y, int w, gfx_color c, uint8_t a) { gfx_rect(x, y, w, 1, c, a); }
void gfx_vline(int x, int y, int h, gfx_color c, uint8_t a) { gfx_rect(x, y, 1, h, c, a); }

/* Integer sqrt, only ever called on corner pixels. */
static uint32_t isqrt32(uint32_t n) {
    if (n == 0) { return 0; }
    uint32_t x = n, y = (x + 1u) / 2u;
    while (y < x) { x = y; y = (x + n / x) / 2u; }
    return x;
}

/* Coverage of a corner pixel, 0..255, from its distance to the arc centre.
 * Distance is carried in 1/16 px so the edge gets real anti-aliasing
 * instead of the stair-step you normally see at this panel size. */
static inline uint8_t corner_cov(int dx, int dy, int r) {
    uint32_t d16 = isqrt32(((uint32_t)(dx * dx + dy * dy)) * 256u);
    int32_t  cov = (int32_t)(r * 16) + 8 - (int32_t)d16;
    if (cov <= 0)  { return 0; }
    if (cov >= 16) { return 255; }
    return (uint8_t)(cov * 255 / 16);
}

void gfx_round_rect(int x, int y, int w, int h, int r, gfx_color c, uint8_t a) {
    if (w <= 0 || h <= 0 || a == 0) { return; }
    if (r <= 0) { gfx_rect(x, y, w, h, c, a); return; }
    if (r > w / 2) { r = w / 2; }
    if (r > h / 2) { r = h / 2; }

    /* middle band: full width, no corners involved */
    gfx_rect(x, y + r, w, h - 2 * r, c, a);

    int y_a = MAX(y, band_y0), y_b = MIN(y + h, gfx_band_y1());
    for (int yy = y_a; yy < y_b; yy++) {
        int top = yy - (y + r);          /* < 0 while above the top arc centre */
        int bot = yy - (y + h - 1 - r);  /* > 0 while below the bottom centre  */
        int dy;
        if      (top < 0) { dy = -top; }
        else if (bot > 0) { dy =  bot; }
        else              { continue; }  /* covered by the middle rect         */

        for (int i = 0; i < r; i++) {
            int dx = r - i;              /* distance from the arc centre       */
            uint8_t cov = corner_cov(dx, dy, r);
            if (cov == 0) { continue; }
            uint8_t aa = (uint8_t)(((uint32_t)cov * a) / 255u);
            px(x + i,         yy, c, aa);
            px(x + w - 1 - i, yy, c, aa);
        }
        /* the flat span between the two arcs on this row */
        gfx_rect(x + r, yy, w - 2 * r, 1, c, a);
    }
}

void gfx_glass(int x, int y, int w, int h, int r) {
    /* 1. the tint. Over a linear gradient this is a true frosted composite. */
    gfx_round_rect(x, y, w, h, r, gfx_hex(TH_GLASS_TINT), TH_GLASS_A);

    /* 2. light pooling toward the top of the pane. Four short steps keep the
     *    ramp cheap and still read as a curved surface. */
    for (int i = 0; i < 4; i++) {
        uint8_t a = (uint8_t)(14 - i * 3);
        gfx_rect(x + r, y + 1 + i * 2, w - 2 * r, 2, gfx_hex(TH_GLASS_EDGE_HI), a);
    }

    /* 3. the specular top edge - this is what sells it as glass. */
    gfx_hline(x + r, y, w - 2 * r, gfx_hex(TH_GLASS_EDGE_HI), TH_GLASS_EDGE_HI_A);
    /* 4. shaded bottom edge for thickness. */
    gfx_hline(x + r, y + h - 1, w - 2 * r, gfx_hex(TH_GLASS_EDGE_LO), TH_GLASS_EDGE_LO_A);
}

void gfx_meter(int x, int y, int w, int h, uint8_t pct, gfx_color fill, gfx_color track) {
    if (pct > 100) { pct = 100; }
    int r = h / 2;
    gfx_round_rect(x, y, w, h, r, track, 110);
    int fw = (w * pct) / 100;
    if (fw > 0 && fw < h) { fw = h; }   /* keep the cap round at low values */
    if (fw > 0) { gfx_round_rect(x, y, fw, h, r, fill, GFX_OPAQUE); }
}

/* -- text ----------------------------------------------------------- */
int gfx_text_h(int scale) { return FONT_H * scale; }

int gfx_text_w(const char *s, int scale) {
    int n = 0;
    for (const char *p = s; *p; p++) { n++; }
    if (n == 0) { return 0; }
    return n * (FONT_W + 1) * scale - scale;   /* no gap after the last glyph */
}

static const uint8_t *glyph(char ch) {
    if (ch >= 'a' && ch <= 'z') { ch = (char)(ch - 'a' + 'A'); }
    if (ch < FONT_FIRST || ch > FONT_LAST) { ch = '?'; }
    return &font5x7[(ch - FONT_FIRST) * FONT_W];
}

void gfx_text(int x, int y, const char *s, int scale, gfx_color c, uint8_t a) {
    if (scale < 1) { scale = 1; }
    /* whole line off-band? nothing to do */
    if (y + FONT_H * scale <= band_y0 || y >= gfx_band_y1()) { return; }

    int pen = x;
    for (const char *p = s; *p; p++) {
        const uint8_t *g = glyph(*p);
        for (int col = 0; col < FONT_W; col++) {
            uint8_t bits = g[col];
            for (int row = 0; row < FONT_H; row++) {
                if (!(bits & (1u << row))) { continue; }
                gfx_rect(pen + col * scale, y + row * scale, scale, scale, c, a);
            }
        }
        pen += (FONT_W + 1) * scale;
    }
}

void gfx_text_c(int cx, int y, const char *s, int scale, gfx_color c, uint8_t a) {
    gfx_text(cx - gfx_text_w(s, scale) / 2, y, s, scale, c, a);
}
