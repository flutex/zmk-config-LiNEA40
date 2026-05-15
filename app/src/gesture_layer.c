#include <zephyr/kernel.h>
#include <zephyr/input/input.h>
#include <zephyr/input/input_event_codes.h>
#include <dt-bindings/zmk/keys.h>
#include <zmk/keymap.h>
#include <zmk/events/keycode_state_changed.h>

#define GESTURE_LAYER     CONFIG_ZMK_GESTURE_LAYER_INDEX
#define GESTURE_THRESHOLD CONFIG_ZMK_GESTURE_THRESHOLD
#define GESTURE_COOLDOWN_MS 400

static int32_t accum_x;
static int32_t accum_y;
static int64_t last_gesture_ms;

static void fire_gesture(uint32_t key) {
    zmk_keycode_state_changed_from_encoded(key, true, k_uptime_get());
    zmk_keycode_state_changed_from_encoded(key, false, k_uptime_get());
    last_gesture_ms = k_uptime_get();
    accum_x = 0;
    accum_y = 0;
}

static void gesture_input_cb(struct input_event *evt, void *user_data) {
    if (!zmk_keymap_layer_active(GESTURE_LAYER)) {
        accum_x = 0;
        accum_y = 0;
        return;
    }
    if (evt->type != INPUT_EV_REL) return;
    if (evt->code == INPUT_REL_X) accum_x += evt->value;
    else if (evt->code == INPUT_REL_Y) accum_y += evt->value;
    else return;
    if (!evt->sync) return;
    if ((k_uptime_get() - last_gesture_ms) < GESTURE_COOLDOWN_MS) return;
    int32_t abs_x = abs(accum_x);
    int32_t abs_y = abs(accum_y);
    if (abs_x < GESTURE_THRESHOLD && abs_y < GESTURE_THRESHOLD) return;
    if (abs_x >= abs_y)
        fire_gesture(accum_x > 0 ? LC(RIGHT_ARROW) : LC(LEFT_ARROW));
    else
        fire_gesture(accum_y < 0 ? LC(UP_ARROW) : LC(DOWN_ARROW));
}

INPUT_CALLBACK_DEFINE(NULL, gesture_input_cb, NULL);
