#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zmk/keymap.h>
#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/ble.h>

// Profile → Layer mapping:
//   0: Windows → deactivate MAC(1) and IOS(2)
//   1: Mac     → activate MAC(1), deactivate IOS(2)
//   2: iOS     → activate IOS(2), deactivate MAC(1)
//   3,4: Windows

#define LAYER_MAC 1
#define LAYER_IOS 2
// Real node-order index in config/LiNEA40.keymap (NOT the stale #define SCROLL etc.)
#define LAYER_JIS_MODE 14

// Per-profile host keyboard layout (US=false / JIS=true).
// Edit this to match each machine's OS keyboard layout setting, then reflash via A-7.
// Default all-US keeps current typing behavior unchanged (JIS_MODE stays inert).
static const bool profile_is_jis[5] = {false, false, false, false, false};

static void update_jis_layer(uint8_t profile) {
    if (profile < 5 && profile_is_jis[profile]) {
        zmk_keymap_layer_activate(LAYER_JIS_MODE);
    } else {
        zmk_keymap_layer_deactivate(LAYER_JIS_MODE);
    }
}

static void update_os_layers(uint8_t profile) {
    switch (profile) {
    case 1:
        zmk_keymap_layer_deactivate(LAYER_IOS);
        zmk_keymap_layer_activate(LAYER_MAC);
        break;
    case 2:
        zmk_keymap_layer_deactivate(LAYER_MAC);
        zmk_keymap_layer_activate(LAYER_IOS);
        break;
    default:
        zmk_keymap_layer_deactivate(LAYER_MAC);
        zmk_keymap_layer_deactivate(LAYER_IOS);
        break;
    }
}

static int os_layer_listener_cb(const zmk_event_t *eh) {
    const struct zmk_ble_active_profile_changed *ev =
        as_zmk_ble_active_profile_changed(eh);
    if (ev) {
        update_os_layers(ev->index);
        update_jis_layer(ev->index);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(os_layer_listener, os_layer_listener_cb);
ZMK_SUBSCRIPTION(os_layer_listener, zmk_ble_active_profile_changed);

static int behavior_os_layer_init(void) {
    update_os_layers(zmk_ble_active_profile_index());
    update_jis_layer(zmk_ble_active_profile_index());
    return 0;
}

SYS_INIT(behavior_os_layer_init, APPLICATION, 95);
