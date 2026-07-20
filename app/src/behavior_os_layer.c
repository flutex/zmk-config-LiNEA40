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

#include <linea_jis.h>

#if IS_ENABLED(CONFIG_SETTINGS)
#include <zephyr/settings/settings.h>
#endif

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

// Per-profile host keyboard layout bitmask (bit n = BT profile n is JIS).
// Runtime-configurable from LineaStudio via the layer_status GATT service
// (linea_jis_flags_set) and persisted in settings ("linea_os/jis"). The value
// below is only the factory default used until the first saved write; it keeps
// BT1 (Mac) on JIS to match the current machine fleet.
//
// SINGLE SOURCE OF TRUTH for JIS (concern #5): the USB path does NOT keep its
// own is_jis table. It reuses these flags via the representative BLE profile
// for each detected OS class (see usb_class_is_jis), so the two can never drift.
#define JIS_FLAGS_MASK 0x1F
static uint8_t jis_flags = BIT(1);

static bool profile_jis(uint8_t profile) {
    return profile < 5 && (jis_flags & BIT(profile));
}

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
    apply_jis(profile_jis(profile));
}

#if IS_ENABLED(CONFIG_ZMK_USB_HOST_OS_DETECTION)

// Reuse profile_is_jis[] as the single JIS source (concern #5): each USB OS
// class borrows the layout flag of its representative BLE profile.
static bool usb_class_is_jis(enum os_class os) {
    switch (os) {
    case OS_MAC:
        return profile_jis(1);
    case OS_IOS:
        return profile_jis(2);
    case OS_WIN:
    default:
        return profile_jis(0);
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

// ---- Runtime JIS flags: persistence + external API (LineaStudio GATT) ----

// Re-apply layers for the current endpoint via the same arbitration the event
// listeners use (USB-aware when the option is on).
static void linea_os_layer_refresh(void) {
#if IS_ENABLED(CONFIG_ZMK_USB_HOST_OS_DETECTION)
    resolve_and_apply();
#else
    arbitrate_from_profile(zmk_ble_active_profile_index());
#endif
}

static void jis_flags_work_cb(struct k_work *work) {
    ARG_UNUSED(work);
#if IS_ENABLED(CONFIG_SETTINGS)
    settings_save_one("linea_os/jis", &jis_flags, sizeof(jis_flags));
#endif
    linea_os_layer_refresh();
}
static K_WORK_DEFINE(jis_flags_work, jis_flags_work_cb);

uint8_t linea_jis_flags_get(void) { return jis_flags; }

void linea_jis_flags_set(uint8_t flags) {
    flags &= JIS_FLAGS_MASK;
    if (flags == jis_flags) {
        return;
    }
    jis_flags = flags;
    // Persist + layer refresh on the system workqueue (caller may be BT RX;
    // settings_save_one writes flash and must not run there).
    k_work_submit(&jis_flags_work);
}

#if IS_ENABLED(CONFIG_SETTINGS)
static int jis_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    const char *next;
    if (settings_name_steq(name, "jis", &next) && !next) {
        if (len != sizeof(uint8_t)) {
            return -EINVAL;
        }
        uint8_t v;
        ssize_t rc = read_cb(cb_arg, &v, sizeof(v));
        if (rc < 0) {
            return (int)rc;
        }
        jis_flags = v & JIS_FLAGS_MASK;
        return 0;
    }
    return -ENOENT;
}
SETTINGS_STATIC_HANDLER_DEFINE(linea_os, "linea_os", NULL, jis_settings_set, NULL, NULL);
#endif

static int behavior_os_layer_init(void) {
#if IS_ENABLED(CONFIG_SETTINGS)
    // Load persisted JIS flags BEFORE the first arbitration so boot comes up
    // with the saved layout. The static handler is registered at link time;
    // settings_subsys_init() is idempotent (ZMK core also calls it).
    settings_subsys_init();
    settings_load_subtree("linea_os");
#endif
    // Concern #7: at init do NOT depend on endpoint state (endpoints init order
    // is not guaranteed here). Apply the BLE default from the active profile;
    // the USB path is picked up later via endpoint_changed / usb_host_os_changed.
    arbitrate_from_profile(zmk_ble_active_profile_index());
    return 0;
}

SYS_INIT(behavior_os_layer_init, APPLICATION, 95);
