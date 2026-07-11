/*
 * OS default-layer auto-switching.
 *
 * BLE path (always): the active BLE profile index selects the OS layer set.
 *   Profile 0/3/4 -> Windows, 1 -> MAC, 2 -> iOS. This numeric behavior is
 *   unchanged from the original implementation.
 *
 * USB path (CONFIG_ZMK_USB_HOST_OS_DETECTION): when the SELECTED endpoint is
 *   USB, the wired host OS detected in usb_host_os.c drives the layer set
 *   instead (macOS -> MAC, Windows/UNKNOWN -> safe Windows default). All USB
 *   additions are guarded; with the option off this file is byte-for-byte the
 *   original BLE-profile-driven behavior and links exactly as before.
 *
 * Arbitration keys off "USB is the SELECTED endpoint" (zmk_endpoints_selected).
 * BLIND SPOT (concern #6): if the user has forced BLE as the preferred/selected
 * endpoint (e.g. a sticky &out BLE), plugging USB does not change the selected
 * endpoint, endpoint_changed does not fire for USB, and the USB path never
 * engages even though usb_host_os.c still classifies correctly. Preferred =
 * USB by default, so this normally does not bite; recovery is settings_reset.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zmk/keymap.h>
#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/ble.h>

#if IS_ENABLED(CONFIG_ZMK_USB_HOST_OS_DETECTION)
#include <zmk/endpoints.h>
#include <zmk/endpoints_types.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk_usb_host_os.h>
#include <zmk/events/usb_host_os_changed.h>
#endif

// Profile → Layer mapping:
//   0: Windows → deactivate MAC(1) and IOS(2)
//   1: Mac     → activate MAC(1), deactivate IOS(2)
//   2: iOS     → activate IOS(2), deactivate MAC(1)
//   3,4: Windows

#define LAYER_MAC 1
#define LAYER_IOS 2
// Real node-order index in config/LiNEA40.keymap (NOT the stale #define SCROLL etc.)
#define LAYER_JIS_MODE 14

// OS class shared by the BLE-profile path and the USB-detection path.
enum os_class { OS_WIN = 0, OS_MAC = 1, OS_IOS = 2 };

// Per-profile host keyboard layout (US=false / JIS=true).
// Edit this to match each machine's OS keyboard layout setting, then reflash via A-7.
// Default all-US keeps current typing behavior unchanged (JIS_MODE stays inert).
//
// SINGLE SOURCE OF TRUTH for JIS (concern #5): the USB path does NOT keep its
// own is_jis table. It reuses this array via the representative BLE profile for
// each detected OS class (see usb_class_is_jis), so the two can never drift.
static const bool profile_is_jis[5] = {false, false, false, false, false};

static enum os_class os_class_for_profile(uint8_t profile) {
    switch (profile) {
    case 1:
        return OS_MAC;
    case 2:
        return OS_IOS;
    default: /* 0, 3, 4 and any out-of-range value */
        return OS_WIN;
    }
}

static void apply_os_class(enum os_class os) {
    switch (os) {
    case OS_MAC:
        zmk_keymap_layer_deactivate(LAYER_IOS);
        zmk_keymap_layer_activate(LAYER_MAC);
        break;
    case OS_IOS:
        zmk_keymap_layer_deactivate(LAYER_MAC);
        zmk_keymap_layer_activate(LAYER_IOS);
        break;
    case OS_WIN:
    default:
        zmk_keymap_layer_deactivate(LAYER_MAC);
        zmk_keymap_layer_deactivate(LAYER_IOS);
        break;
    }
}

static void apply_jis(bool is_jis) {
    if (is_jis) {
        zmk_keymap_layer_activate(LAYER_JIS_MODE);
    } else {
        zmk_keymap_layer_deactivate(LAYER_JIS_MODE);
    }
}

static void arbitrate_from_profile(uint8_t profile) {
    apply_os_class(os_class_for_profile(profile));
    apply_jis(profile < 5 && profile_is_jis[profile]);
}

#if IS_ENABLED(CONFIG_ZMK_USB_HOST_OS_DETECTION)

// Reuse profile_is_jis[] as the single JIS source (concern #5): each USB OS
// class borrows the layout flag of its representative BLE profile.
static bool usb_class_is_jis(enum os_class os) {
    switch (os) {
    case OS_MAC:
        return profile_is_jis[1];
    case OS_IOS:
        return profile_is_jis[2];
    case OS_WIN:
    default:
        return profile_is_jis[0];
    }
}

static enum os_class os_class_for_usb_host(enum zmk_usb_host_os host) {
    switch (host) {
    case ZMK_USB_HOST_OS_MACOS:
        return OS_MAC;
    case ZMK_USB_HOST_OS_WINDOWS:
    case ZMK_USB_HOST_OS_UNKNOWN:
    default:
        return OS_WIN; /* safe default: Windows / layer 0 */
    }
}

// Endpoint-aware arbitration. When USB is the selected endpoint the wired host
// OS wins; otherwise fall back to the BLE profile. A BLE profile change while
// USB is selected therefore does NOT move layers (the USB branch ignores it).
static void resolve_and_apply(void) {
    struct zmk_endpoint_instance ep = zmk_endpoints_selected();
    if (ep.transport == ZMK_TRANSPORT_USB) {
        enum os_class os = os_class_for_usb_host(zmk_usb_host_os_current());
        apply_os_class(os);
        apply_jis(usb_class_is_jis(os));
    } else {
        arbitrate_from_profile(zmk_ble_active_profile_index());
    }
}

static int os_layer_listener_cb(const zmk_event_t *eh) {
    // Any of the three subscribed events re-runs the same endpoint arbitration.
    (void)eh;
    resolve_and_apply();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(os_layer_listener, os_layer_listener_cb);
ZMK_SUBSCRIPTION(os_layer_listener, zmk_ble_active_profile_changed);
// USB⇔BLE switch / plug-unplug re-arbitrates the endpoint.
ZMK_SUBSCRIPTION(os_layer_listener, zmk_endpoint_changed);
// Detection confirmed: if USB is currently selected, re-apply with the new OS.
ZMK_SUBSCRIPTION(os_layer_listener, zmk_usb_host_os_changed);

#else /* !CONFIG_ZMK_USB_HOST_OS_DETECTION — original BLE-only behavior */

static int os_layer_listener_cb(const zmk_event_t *eh) {
    const struct zmk_ble_active_profile_changed *ev =
        as_zmk_ble_active_profile_changed(eh);
    if (ev) {
        arbitrate_from_profile(ev->index);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(os_layer_listener, os_layer_listener_cb);
ZMK_SUBSCRIPTION(os_layer_listener, zmk_ble_active_profile_changed);

#endif /* CONFIG_ZMK_USB_HOST_OS_DETECTION */

static int behavior_os_layer_init(void) {
    // Concern #7: at init do NOT depend on endpoint state (endpoints init order
    // is not guaranteed here). Apply the BLE default from the active profile;
    // the USB path is picked up later via endpoint_changed / usb_host_os_changed.
    arbitrate_from_profile(zmk_ble_active_profile_index());
    return 0;
}

SYS_INIT(behavior_os_layer_init, APPLICATION, 95);
