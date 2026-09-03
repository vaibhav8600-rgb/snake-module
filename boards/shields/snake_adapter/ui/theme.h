/*
 * Glass-on-gradient design tokens.
 *
 * One place for every colour and metric the UI uses. Values are RGB888;
 * gfx_hex() packs them to RGB565 at use.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef UI_THEME_H
#define UI_THEME_H

/* ── Wallpaper ─────────────────────────────────────────────────────────
 * A vertical gradient is the whole backdrop. It matters more than it
 * looks: blurring a linear gradient returns the same gradient, so an
 * alpha tint over it IS a correct frosted-glass composite. No blur pass.
 */
#define TH_BG_TOP        0x101A3Du   /* deep indigo   */
#define TH_BG_BOT        0x2D1B4Eu   /* violet        */

/* ── Glass ─────────────────────────────────────────────────────────────
 * Panels are a low-alpha white tint, a bright 1px top edge (the specular
 * catch that sells the material) and a dark 1px bottom edge.
 */
#define TH_GLASS_TINT    0xBFD4FFu
#define TH_GLASS_A         30u       /* 0-255 over the wallpaper        */
#define TH_GLASS_EDGE_HI 0xFFFFFFu
#define TH_GLASS_EDGE_HI_A 90u
#define TH_GLASS_EDGE_LO 0x000814u
#define TH_GLASS_EDGE_LO_A 70u
#define TH_GLASS_RADIUS     7

/* ── Accents ───────────────────────────────────────────────────────────*/
#define TH_CYAN          0x4AEDFFu   /* carried over from your config   */
#define TH_MAGENTA       0xFF6BD6u
#define TH_LIME          0x5BE38Au
#define TH_AMBER         0xFFC857u
#define TH_ROSE          0xFF5C7Au

/* ── Text ──────────────────────────────────────────────────────────────*/
#define TH_TEXT          0xEAF2FFu
#define TH_TEXT_DIM      0x93A6C9u
#define TH_TEXT_FAINT    0x5D6B8Cu

/* ── Battery thresholds (percent) ──────────────────────────────────────*/
#define TH_BATT_LOW      20
#define TH_BATT_MID      45

/* ── Layout ────────────────────────────────────────────────────────────*/
#define TH_PAD            9          /* screen edge padding             */
#define TH_GAP            7          /* between panels                  */

#endif /* UI_THEME_H */
