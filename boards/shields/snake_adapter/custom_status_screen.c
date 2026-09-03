/*
 * Status screen entry point.
 *
 * The dashboard is drawn by ui/screen_status.c through the strip
 * compositor. LVGL is still the host - ZMK requires an lv_obj back - but
 * it draws nothing, exactly as before.
 *
 * The splash still runs through widgets/splash.c so the image you pass in
 * from the config repo keeps working unchanged. Porting that onto the new
 * compositor is the next step; it is the one piece with a byte-order
 * detail worth verifying on hardware rather than guessing.
 *
 * SPDX-License-Identifier: MIT
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "custom_status_screen.h"
#include "widgets/splash.h"
#include "widgets/helpers/buzzer.h"
#include "widgets/helpers/display.h"
#include "widgets/configuration.h"

#include "ui/gfx.h"
#include "ui/screen_status.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define SPLASH_TICK_MS   50
static const uint16_t SPLASH_TICKS = CONFIG_SPLASH_DISPLAY_TIME_MS / SPLASH_TICK_MS;

static uint16_t splash_ticks;
static bool     splash_done;

static void splash_timer(lv_timer_t *timer) {
    if (splash_done) {
        return;
    }

    if (splash_ticks < SPLASH_TICKS) {
        print_splash();
        splash_ticks++;
        return;
    }

    lv_timer_pause(timer);
    clean_up_splash();
    splash_done = true;

    /* hand the panel over to the new UI */
    if (gfx_init() == 0) {
        screen_status_init();
    } else {
        LOG_ERR("ui: compositor init failed, screen stays blank");
    }
}

lv_obj_t *zmk_display_status_screen(void) {
    /* colour tables still feed the splash renderer */
    configure();
    init_display();

#ifdef CONFIG_USE_BUZZER
    app_buzzer_init();
#ifdef CONFIG_USE_SPLASH_SOUND
    play_snake_game_intro();
#endif
#endif

    zmk_widget_splash_init();
    lv_timer_create(splash_timer, SPLASH_TICK_MS, NULL);

    return lv_obj_create(NULL);
}
