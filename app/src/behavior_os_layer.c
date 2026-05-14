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
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(os_layer_listener, os_layer_listener_cb);
ZMK_SUBSCRIPTION(os_layer_listener, zmk_ble_active_profile_changed);

static int behavior_os_layer_init(void) {
    update_os_layers(zmk_ble_active_profile_index());
    return 0;
}

SYS_INIT(behavior_os_layer_init, APPLICATION, 95);
