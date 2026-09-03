/*
 * Glass-on-gradient status dashboard.
 *
 * ZMK listeners only ever touch `st` and mark a dirty row range; a coalescing
 * work item on ZMK's display queue does the drawing. Without that split every
 * keycode event would trigger a full twenty-band repaint, and holding a
 * modifier would peg the display thread.
 *
 * SPDX-License-Identifier: MIT
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/events/wpm_state_changed.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>
#include <dt-bindings/zmk/modifiers.h>
#include <zmk/keymap.h>
#include <zmk/usb.h>
#include <zmk/wpm.h>

#include "gfx.h"
#include "theme.h"
#include "screen_status.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* -- layout ---------------------------------------------------------- */
#define HDR_Y     TH_PAD
#define HDR_H     42
#define BAT_Y     (HDR_Y + HDR_H + TH_GAP)          /*  58 */
#define BAT_H     68
#define BAT_W     107
#define ROW_Y     (BAT_Y + BAT_H + TH_GAP)          /* 133 */
#define ROW_H     44
#define MOD_Y     (ROW_Y + ROW_H + TH_GAP)          /* 184 */
#define MOD_H     47
#define FULL_W    (GFX_W - 2 * TH_PAD)              /* 222 */
#define UI_PROFILES  MIN(ZMK_BLE_PROFILE_COUNT, 8)  /* 5 on this build */

/* -- state ----------------------------------------------------------- */
static struct {
    uint8_t  batt[2];        /* 0 = left, 1 = right, 0xFF = unknown */
    uint8_t  profile;
    bool     ble_connected;
    uint8_t  prof_conn;      /* bit i = profile i connected */
    uint8_t  prof_open;      /* bit i = profile i open (unpaired) */
    bool     usb_hid;
    bool     on_usb;
    const char *layer_name;
    uint8_t  wpm;
    uint8_t  mods;
} st = {
    .batt = { 0xFF, 0xFF },
    .layer_name = "BASE",
};

/* Dirty region as a row range. Empty when hi <= lo. Each listener marks only
 * the panel it owns, so typing repaints the 44px layer/WPM row (4 bands,
 * 23,040 B over SPI) instead of the whole screen (20 bands, 115,200 B). */
static int dirty_lo = 0;
static int dirty_hi = GFX_H;

/* -- drawing --------------------------------------------------------- */
static gfx_color batt_color(uint8_t pct) {
    if (pct >= TH_BATT_MID) { return gfx_hex(TH_LIME); }
    if (pct >= TH_BATT_LOW) { return gfx_hex(TH_AMBER); }
    return gfx_hex(TH_ROSE);
}

static void draw_header(void) {
    gfx_glass(TH_PAD, HDR_Y, FULL_W, HDR_H, TH_GLASS_RADIUS);

    /* wordmark, with the trailing E in magenta so the lockup has a focal point */
    gfx_text(TH_PAD + 12, HDR_Y + 11, "SNAK", 3, gfx_hex(TH_CYAN), GFX_OPAQUE);
    gfx_text(TH_PAD + 12 + gfx_text_w("SNAK", 3) + 3, HDR_Y + 11, "E", 3,
             gfx_hex(TH_MAGENTA), GFX_OPAQUE);

    /* right cluster: transport tag over a strip of BLE profile dots.
     * Colour is the profile's state, the underline marks the active one. */
    int right = TH_PAD + FULL_W - 12;

    const char *tag = st.on_usb ? "USB" : "BLE";
    bool live = st.on_usb ? st.usb_hid : st.ble_connected;
    gfx_text(right - gfx_text_w(tag, 1), HDR_Y + 7, tag, 1,
             live ? gfx_hex(TH_LIME) : gfx_hex(TH_TEXT_FAINT), GFX_OPAQUE);

    const int d = 7, g = 4;
    int strip = UI_PROFILES * d + (UI_PROFILES - 1) * g;
    int x = right - strip;
    int y = HDR_Y + 19;

    for (int i = 0; i < UI_PROFILES; i++) {
        gfx_color c;
        if (st.prof_conn & (1u << i))      { c = gfx_hex(TH_LIME); }
        else if (st.prof_open & (1u << i)) { c = gfx_hex(TH_AMBER); }
        else                               { c = gfx_hex(TH_TEXT_FAINT); }

        gfx_round_rect(x, y, d, d, d / 2, c, GFX_OPAQUE);
        if (!st.on_usb && i == st.profile) {
            gfx_hline(x, y + d + 3, d, gfx_hex(TH_CYAN), GFX_OPAQUE);
        }
        x += d + g;
    }
}

static void draw_battery(int x, const char *label, uint8_t pct) {
    gfx_glass(x, BAT_Y, BAT_W, BAT_H, TH_GLASS_RADIUS);
    gfx_text(x + 12, BAT_Y + 10, label, 1, gfx_hex(TH_TEXT_FAINT), GFX_OPAQUE);

    char buf[8];
    if (pct == 0xFF) {
        snprintf(buf, sizeof(buf), "--");
        gfx_text(x + 12, BAT_Y + 24, buf, 4, gfx_hex(TH_TEXT_FAINT), GFX_OPAQUE);
        return;
    }

    snprintf(buf, sizeof(buf), "%u", (unsigned)pct);
    gfx_text(x + 12, BAT_Y + 24, buf, 4, gfx_hex(TH_TEXT), GFX_OPAQUE);
    gfx_text(x + 12 + gfx_text_w(buf, 4) + 4, BAT_Y + 31, "%", 2,
             gfx_hex(TH_TEXT_DIM), GFX_OPAQUE);

    gfx_meter(x + 12, BAT_Y + BAT_H - 16, BAT_W - 24, 7, pct,
              batt_color(pct), gfx_hex(0x000A18));
}

static void draw_row(void) {
    gfx_glass(TH_PAD, ROW_Y, FULL_W, ROW_H, TH_GLASS_RADIUS);

    const char *name = st.layer_name ? st.layer_name : "";
    gfx_text(TH_PAD + 12, ROW_Y + 8,  "LAYER", 1, gfx_hex(TH_TEXT_FAINT), GFX_OPAQUE);
    gfx_text(TH_PAD + 12, ROW_Y + 20, name,    2, gfx_hex(TH_CYAN), GFX_OPAQUE);

    char buf[8];
    snprintf(buf, sizeof(buf), "%u", (unsigned)st.wpm);
    int right = TH_PAD + FULL_W - 12;
    gfx_text(right - gfx_text_w("WPM", 1), ROW_Y + 8, "WPM", 1,
             gfx_hex(TH_TEXT_FAINT), GFX_OPAQUE);
    gfx_text(right - gfx_text_w(buf, 2), ROW_Y + 20, buf, 2,
             gfx_hex(TH_TEXT), GFX_OPAQUE);
}

static void draw_mods(void) {
    gfx_glass(TH_PAD, MOD_Y, FULL_W, MOD_H, TH_GLASS_RADIUS);

    static const char *names[4] = { "SFT", "CTL", "ALT", "GUI" };
    const uint8_t masks[4] = {
        MOD_LSFT | MOD_RSFT,
        MOD_LCTL | MOD_RCTL,
        MOD_LALT | MOD_RALT,
        MOD_LGUI | MOD_RGUI,
    };

    int pill_w = 46, pill_h = 21;
    int gap = (FULL_W - 24 - 4 * pill_w) / 3;
    int x = TH_PAD + 12;
    int y = MOD_Y + (MOD_H - pill_h) / 2;

    for (int i = 0; i < 4; i++) {
        bool on = (st.mods & masks[i]) != 0;
        if (on) {
            gfx_round_rect(x, y, pill_w, pill_h, pill_h / 2, gfx_hex(TH_CYAN), GFX_OPAQUE);
        } else {
            gfx_round_rect(x, y, pill_w, pill_h, pill_h / 2, gfx_hex(0x000A18), 90);
        }
        gfx_text_c(x + pill_w / 2, y + (pill_h - gfx_text_h(2)) / 2, names[i], 2,
                   on ? gfx_hex(0x061018) : gfx_hex(TH_TEXT_FAINT), GFX_OPAQUE);
        x += pill_w + gap;
    }
}

/* Does a panel overlap the band being composited right now? */
static inline bool hits(int y, int h) {
    return !(y + h <= gfx_band_y0() || y >= gfx_band_y1());
}

static void draw_scene(void *ctx) {
    ARG_UNUSED(ctx);
    /* The gradient clips to the band internally, so it only ever touches
     * GFX_STRIP_H rows. Everything else gets culled here: draw_scene runs
     * once per band, so without this each panel would be re-rendered 20
     * times per full repaint for the 1-2 bands it actually covers. */
    gfx_vgrad(0, GFX_H, gfx_hex(TH_BG_TOP), gfx_hex(TH_BG_BOT));

    if (hits(HDR_Y, HDR_H)) { draw_header(); }
    if (hits(BAT_Y, BAT_H)) {
        draw_battery(TH_PAD,                  "LEFT",  st.batt[0]);
        draw_battery(TH_PAD + BAT_W + TH_GAP, "RIGHT", st.batt[1]);
    }
    if (hits(ROW_Y, ROW_H)) { draw_row(); }
    if (hits(MOD_Y, MOD_H)) { draw_mods(); }
}

void screen_status_render(void) {
    if (!gfx_ready() || dirty_hi <= dirty_lo) { return; }
    gfx_render_range(draw_scene, NULL, dirty_lo, dirty_hi);
    dirty_lo = GFX_H;
    dirty_hi = 0;
}

void screen_status_invalidate(void) {
    dirty_lo = 0;
    dirty_hi = GFX_H;
}

/* Coalesce at 100 ms. Several regions changing inside one window merge into
 * a single span, which is why this unions rather than queueing. */
static void repaint_work_cb(struct k_work *work) {
    ARG_UNUSED(work);
    screen_status_render();
}
static K_WORK_DELAYABLE_DEFINE(repaint_work, repaint_work_cb);

static void schedule_repaint(int y0, int y1) {
    if (y0 < dirty_lo) { dirty_lo = y0; }
    if (y1 > dirty_hi) { dirty_hi = y1; }
    /* Must run on ZMK's display queue, not the system one. LVGL drives the
     * same SPI panel from that thread, and two queues calling display_write()
     * concurrently would interleave on the bus.
     *
     * k_work_SCHEDULE, not k_work_RESCHEDULE. Reschedule restarts the delay on
     * every call, so a stream of events closer together than 100 ms pushes the
     * deadline forever and the repaint never runs - which is why typing never
     * updated the layer/WPM/modifier panels while the 60-second battery events
     * always got through. Schedule is a throttle: first event arms the timer,
     * later ones are no-ops, and the accumulated dirty range paints on time. */
    k_work_schedule_for_queue(zmk_display_work_q(), &repaint_work, K_MSEC(100));
}

/* -- ZMK listeners --------------------------------------------------- */
struct batt_ev { uint8_t source; uint8_t level; };

static void batt_cb(struct batt_ev e) {
    if (e.source < 2) { st.batt[e.source] = e.level; schedule_repaint(BAT_Y, BAT_Y + BAT_H); }
}
static struct batt_ev batt_get(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev =
        as_zmk_peripheral_battery_state_changed(eh);
    return (struct batt_ev){ .source = ev->source, .level = ev->state_of_charge };
}
ZMK_DISPLAY_WIDGET_LISTENER(ui_batt, struct batt_ev, batt_cb, batt_get)
ZMK_SUBSCRIPTION(ui_batt, zmk_peripheral_battery_state_changed);

struct out_ev { uint8_t profile; bool ble; bool usb; bool on_usb;
                uint8_t prof_conn; uint8_t prof_open; };

static void out_cb(struct out_ev e) {
    st.profile = e.profile;
    st.ble_connected = e.ble;
    st.usb_hid = e.usb;
    st.on_usb = e.on_usb;
    st.prof_conn = e.prof_conn;
    st.prof_open = e.prof_open;
    schedule_repaint(HDR_Y, HDR_Y + HDR_H);
}
static struct out_ev out_get(const zmk_event_t *eh) {
    ARG_UNUSED(eh);
    struct zmk_endpoint_instance sel = zmk_endpoint_get_selected();
    uint8_t conn = 0, open = 0;
    for (int i = 0; i < UI_PROFILES; i++) {
        if (zmk_ble_profile_is_connected(i)) { conn |= (uint8_t)(1u << i); }
        if (zmk_ble_profile_is_open(i))      { open |= (uint8_t)(1u << i); }
    }
    return (struct out_ev){
        .profile   = zmk_ble_active_profile_index(),
        .ble       = zmk_ble_active_profile_is_connected(),
        .usb       = zmk_usb_is_hid_ready(),
        .on_usb    = (sel.transport == ZMK_TRANSPORT_USB),
        .prof_conn = conn,
        .prof_open = open,
    };
}
ZMK_DISPLAY_WIDGET_LISTENER(ui_out, struct out_ev, out_cb, out_get)
ZMK_SUBSCRIPTION(ui_out, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(ui_out, zmk_ble_active_profile_changed);
ZMK_SUBSCRIPTION(ui_out, zmk_usb_conn_state_changed);

struct layer_ev { const char *name; };

static void layer_cb(struct layer_ev e) {
    st.layer_name = (e.name && e.name[0]) ? e.name : "BASE";
    schedule_repaint(ROW_Y, ROW_Y + ROW_H);
}
static struct layer_ev layer_get(const zmk_event_t *eh) {
    ARG_UNUSED(eh);
    /* On ZMK main a layer's *index* (position in the active stack) and its
     * *id* (stable handle) are different things, and layer_name() wants the
     * id. They happen to be equal until something reorders layers - which is
     * exactly what Studio does - so resolve properly rather than rely on it. */
    zmk_keymap_layer_index_t idx = zmk_keymap_highest_layer_active();
    zmk_keymap_layer_id_t    id  = zmk_keymap_layer_index_to_id(idx);
    return (struct layer_ev){ .name = zmk_keymap_layer_name(id) };
}
ZMK_DISPLAY_WIDGET_LISTENER(ui_layer, struct layer_ev, layer_cb, layer_get)
ZMK_SUBSCRIPTION(ui_layer, zmk_layer_state_changed);

struct wpm_ev { uint8_t wpm; };

static void wpm_cb(struct wpm_ev e)  { st.wpm = e.wpm; schedule_repaint(ROW_Y, ROW_Y + ROW_H); }
static struct wpm_ev wpm_get(const zmk_event_t *eh) {
    const struct zmk_wpm_state_changed *ev = as_zmk_wpm_state_changed(eh);
    return (struct wpm_ev){ .wpm = (uint8_t)ev->state };
}
ZMK_DISPLAY_WIDGET_LISTENER(ui_wpm, struct wpm_ev, wpm_cb, wpm_get)
ZMK_SUBSCRIPTION(ui_wpm, zmk_wpm_state_changed);

struct mod_ev { uint8_t mods; };

static void mod_cb(struct mod_ev e) {
    if (e.mods != st.mods) { st.mods = e.mods; schedule_repaint(MOD_Y, MOD_Y + MOD_H); }
}
static struct mod_ev mod_get(const zmk_event_t *eh) {
    ARG_UNUSED(eh);
    return (struct mod_ev){ .mods = zmk_hid_get_explicit_mods() };
}
ZMK_DISPLAY_WIDGET_LISTENER(ui_mods, struct mod_ev, mod_cb, mod_get)
ZMK_SUBSCRIPTION(ui_mods, zmk_keycode_state_changed);

/* -- entry ----------------------------------------------------------- */
void screen_status_init(void) {
    ui_batt_init();
    ui_out_init();
    ui_layer_init();
    ui_wpm_init();
    ui_mods_init();
    screen_status_render();
}
