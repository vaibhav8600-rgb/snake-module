#define DT_DRV_COMPAT zmk_behavior_anti_idle

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

/* Behaviors run on the central (dongle), which owns the HID endpoints.
 * Peripheral builds just need this to compile, so all HID work is
 * guarded by CONFIG_ZMK_POINTING. */
#if IS_ENABLED(CONFIG_ZMK_POINTING)

static bool active;
static uint8_t steps_left;

/* ponytail: xorshift32 instead of the entropy subsystem — plenty random
 * for humanizing timings and immune to Kconfig/Zephyr header churn. */
static uint32_t rng = 2463534242u;

static uint32_t rnd(uint32_t max) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng % max;
}

static void jiggle_work_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(jiggle_work, jiggle_work_cb);

static void jiggle_work_cb(struct k_work *work) {
    if (!active) {
        return;
    }

    if (steps_left == 0) {
        /* new burst: 4-9 tiny moves, like a hand nudging the mouse */
        steps_left = 4 + rnd(6);
    }

    int16_t dx = (int16_t)(rnd(5)) - 2; /* -2..2 px */
    int16_t dy = (int16_t)(rnd(5)) - 2;
    if (dx == 0 && dy == 0) {
        dx = 1;
    }

    zmk_hid_mouse_movement_set(dx, dy);
    zmk_endpoints_send_mouse_report();
    zmk_hid_mouse_movement_set(0, 0); /* don't leak movement into later reports */

    steps_left--;
    if (steps_left > 0) {
        /* steps inside a burst: 15-50 ms apart */
        k_work_reschedule(&jiggle_work, K_MSEC(15 + rnd(35)));
    } else {
        /* pause between bursts: 20-60 s, random each time */
        k_work_reschedule(&jiggle_work, K_SECONDS(20 + rnd(40)));
    }
}

static void anti_idle_toggle(void) {
    active = !active;
    if (active) {
        rng ^= k_cycle_get_32(); /* reseed so patterns differ each session */
        steps_left = 0;
        /* first burst almost immediately: the cursor twitch confirms it's ON */
        k_work_reschedule(&jiggle_work, K_MSEC(300));
    } else {
        k_work_cancel_delayable(&jiggle_work);
    }
    LOG_INF("anti-idle %s", active ? "enabled" : "disabled");
}

#else /* !CONFIG_ZMK_POINTING */

static void anti_idle_toggle(void) {}

#endif /* CONFIG_ZMK_POINTING */

static int on_binding_pressed(struct zmk_behavior_binding *binding,
                              struct zmk_behavior_binding_event event) {
    anti_idle_toggle();
    return ZMK_BEHAVIOR_OPAQUE; /* swallow — nothing sent to host for the key itself */
}

static int on_binding_released(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_anti_idle_driver_api = {
    .binding_pressed = on_binding_pressed,
    .binding_released = on_binding_released,
};

BEHAVIOR_DT_INST_DEFINE(0,
                        NULL, NULL,
                        NULL, NULL,
                        POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &behavior_anti_idle_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
