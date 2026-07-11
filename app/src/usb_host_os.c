/*
 * Wired USB host OS detection.
 *
 * Sole owner of the linker --wrap on usb_dc_ep_read. usb_dc_ep_read can only be
 * --wrapped once, so ALL enumeration observation is funneled through this file:
 *
 *   (1) USB host OS classifier accumulator   (CONFIG_ZMK_USB_HOST_OS_DETECTION)
 *   (2) fingerprint ring buffer, debug only  (CONFIG_ZMK_USB_FP_CAPTURE)
 *
 * The CMake glue compiles this file when EITHER feature is enabled and adds the
 * single --wrap flag exactly once. usb_fp_capture.c is reduced to the ring +
 * snapshot + record helper; it no longer defines a wrap.
 *
 * Classification (confirmed on-device, LiNEA40 clean fingerprints):
 *   Windows (n=22): first GET_DESCRIPTOR(device) wLength=64, single-shot string
 *                   read (wLength=0xFF), DEVICE_QUALIFIER requested, no SET_FEATURE.
 *   macOS   (n=14): first GET_DESCRIPTOR(device) wLength=8, two-stage string
 *                   probe (wLength=2 then full), no DEVICE_QUALIFIER, SET_FEATURE present.
 * Primary signal is the first device-descriptor wLength (8=mac / 64=win). On
 * conflict the first-device wLength wins. SET_FEATURE is a macOS corroborator
 * only and is never used as a sole signal. iPad is out of scope (user does not
 * use a wired iPad); it would classify as UNKNOWN -> safe Windows default.
 *
 * NON-CONTACT RULE (past incident: BLE/HID co-tenant GATT twice broke key input
 * by touching connection/GATT timing): the usb_dc_ep_read wrap runs in USB
 * workqueue context and does ONLY a few SETUP-byte reads, an irq_lock-guarded
 * accumulator update, an optional ring record, and a k_work_reschedule. No
 * logging, no events, no layer/GATT ops, nothing blocking. Every heavy action
 * (classify, event raise) happens later in the system workqueue settle handler.
 *
 * BLIND SPOT (concern #6): default-layer switching keys off "USB is the SELECTED
 * endpoint" (resolve_and_apply in behavior_os_layer.c). If the user has forced
 * BLE as the preferred/selected endpoint (e.g. a sticky &out BLE), plugging USB
 * will not change the selected endpoint, endpoint_changed will not fire for USB,
 * and this detector's result stays inert even though it is computed correctly.
 * The user's preferred endpoint is USB by default, so this normally does not
 * bite. Recovery: settings_reset to restore the default preferred endpoint.
 */

#include <zephyr/kernel.h>
#include <zephyr/irq.h>

#if IS_ENABLED(CONFIG_ZMK_USB_FP_CAPTURE)
#include "usb_fp_capture.h"
#endif

#if IS_ENABLED(CONFIG_ZMK_USB_HOST_OS_DETECTION)
#include <zmk/event_manager.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>

#include <zmk_usb_host_os.h>
#include <zmk/events/usb_host_os_changed.h>
#endif

/* USB_CONTROL_EP_OUT from Zephyr's usb device stack. */
#define USB_HOST_OS_CONTROL_EP_OUT 0x00

/* bmRequestType type field mask/value for standard requests (bits 6:5 == 00). */
#define USB_HOST_OS_REQTYPE_TYPE_MASK 0x60
#define USB_HOST_OS_REQTYPE_TYPE_STANDARD 0x00

extern int __real_usb_dc_ep_read(uint8_t ep, uint8_t *data, uint32_t max_data_len,
                                 uint32_t *read_bytes);

#if IS_ENABLED(CONFIG_ZMK_USB_HOST_OS_DETECTION)

/*
 * Classifier accumulator. Written in the wrap (USB workqueue context) and read
 * in the settle handler / conn-state listener; every access is inside a short
 * irq_lock critical section, so no volatile is needed.
 */
static bool acc_first_dev_seen;   /* first GET_DESCRIPTOR(device) observed */
static uint8_t acc_first_dev_wlen; /* its wLength low byte (8=mac, 64=win) */
static bool acc_saw_qualifier;    /* DEVICE_QUALIFIER requested (win) */
static bool acc_saw_string_probe; /* 2-byte string length probe (mac) */
static bool acc_saw_set_feature;  /* SET_FEATURE seen (mac corroborator) */

/* Resolved state (thread context only). */
static enum zmk_usb_host_os detected_os = ZMK_USB_HOST_OS_UNKNOWN;
static bool os_latched; /* once resolved != UNKNOWN, stop reclassifying until disconnect */

static void settle_work_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(settle_work, settle_work_cb);

enum zmk_usb_host_os zmk_usb_host_os_current(void) {
    return detected_os;
}

static void acc_reset(void) {
    unsigned int key = irq_lock();
    acc_first_dev_seen = false;
    acc_first_dev_wlen = 0;
    acc_saw_qualifier = false;
    acc_saw_string_probe = false;
    acc_saw_set_feature = false;
    irq_unlock(key);
}

static void settle_work_cb(struct k_work *work) {
    /* Defensive latch (concern #3): once resolved, don't reclassify until a
     * disconnect resets us. Real hosts don't re-enumerate mid-session, but the
     * latch is cheap insurance against a spurious late SETUP flipping the OS. */
    if (os_latched) {
        return;
    }

    unsigned int key = irq_lock();
    bool seen = acc_first_dev_seen;
    uint8_t wlen = acc_first_dev_wlen;
    bool qualifier = acc_saw_qualifier;
    bool string_probe = acc_saw_string_probe;
    /* acc_saw_set_feature is a corroborator only; not used as a sole signal. */
    irq_unlock(key);

    /* Primary signal is the first device-descriptor wLength; it wins on
     * conflict. Secondary signals only decide when the primary is absent. */
    enum zmk_usb_host_os os;
    if (seen && wlen == 8) {
        os = ZMK_USB_HOST_OS_MACOS;
    } else if (seen && wlen == 64) {
        os = ZMK_USB_HOST_OS_WINDOWS;
    } else if (string_probe) {
        os = ZMK_USB_HOST_OS_MACOS;
    } else if (qualifier) {
        os = ZMK_USB_HOST_OS_WINDOWS;
    } else {
        os = ZMK_USB_HOST_OS_UNKNOWN;
    }

    if (os == ZMK_USB_HOST_OS_UNKNOWN) {
        /* Not enough signal yet; a later SETUP will reschedule us. */
        return;
    }

    os_latched = true;
    if (os != detected_os) {
        detected_os = os;
        raise_zmk_usb_host_os_changed((struct zmk_usb_host_os_changed){.os = os});
    }
}

/*
 * Disconnect reset (concern #1, critical). When the host stops enumerating us
 * (conn_state leaves ZMK_USB_CONN_HID -> NONE/POWERED), wipe the accumulator
 * AND the resolved enum back to UNKNOWN and drop the latch, idempotently. This
 * is what prevents "Mac wired -> unplug -> Windows wired" from leaving a stale
 * MACOS verdict (and MAC layer) active on the Windows host.
 */
static int usb_host_os_conn_listener(const zmk_event_t *eh) {
    const struct zmk_usb_conn_state_changed *ev = as_zmk_usb_conn_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (ev->conn_state != ZMK_USB_CONN_HID) {
        k_work_cancel_delayable(&settle_work);
        acc_reset();
        os_latched = false;
        if (detected_os != ZMK_USB_HOST_OS_UNKNOWN) {
            detected_os = ZMK_USB_HOST_OS_UNKNOWN;
            raise_zmk_usb_host_os_changed(
                (struct zmk_usb_host_os_changed){.os = ZMK_USB_HOST_OS_UNKNOWN});
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(usb_host_os, usb_host_os_conn_listener);
ZMK_SUBSCRIPTION(usb_host_os, zmk_usb_conn_state_changed);

#endif /* CONFIG_ZMK_USB_HOST_OS_DETECTION */

int __wrap_usb_dc_ep_read(uint8_t ep, uint8_t *data, uint32_t max_data_len,
                          uint32_t *read_bytes) {
    int ret = __real_usb_dc_ep_read(ep, data, max_data_len, read_bytes);

    /* Only the 8-byte SETUP-stage read on the control OUT endpoint. Data-stage
     * reads pass a non-NULL read_bytes, so this filter is unique. */
    if (ret == 0 && ep == USB_HOST_OS_CONTROL_EP_OUT && max_data_len == 8 &&
        read_bytes == NULL && data != NULL) {
        uint8_t bmRequestType = data[0];

        /* Standard requests only; drop class/vendor (HID SET_IDLE, etc.). */
        if ((bmRequestType & USB_HOST_OS_REQTYPE_TYPE_MASK) ==
            USB_HOST_OS_REQTYPE_TYPE_STANDARD) {
            uint8_t bRequest = data[1];
            uint16_t wValue = (uint16_t)(data[2] | ((uint16_t)data[3] << 8));
            uint16_t wLength = (uint16_t)(data[6] | ((uint16_t)data[7] << 8));
            uint8_t desc_type = (uint8_t)((wValue >> 8) & 0xFF);

#if IS_ENABLED(CONFIG_ZMK_USB_FP_CAPTURE)
            /* Debug ring (future iPad measurement). Own short irq_lock inside. */
            {
                struct fp_entry e;
                e.ms = k_uptime_get_32();
                e.bmRequestType = bmRequestType;
                e.bRequest = bRequest;
                e.wValue = wValue;
                e.wIndex = (uint16_t)(data[4] | ((uint16_t)data[5] << 8));
                e.wLength = wLength;
                usb_fp_capture_record(&e);
            }
#endif

#if IS_ENABLED(CONFIG_ZMK_USB_HOST_OS_DETECTION)
            {
                bool is_get_descriptor = (bmRequestType == 0x80 && bRequest == 0x06);
                bool first_dev = is_get_descriptor && desc_type == 0x01;
                bool qualifier = is_get_descriptor && desc_type == 0x06;
                bool string_probe =
                    is_get_descriptor && desc_type == 0x03 && wLength == 2;
                bool set_feature = (bmRequestType == 0x00 && bRequest == 0x03);

                unsigned int key = irq_lock();
                if (first_dev && !acc_first_dev_seen) {
                    acc_first_dev_seen = true;
                    acc_first_dev_wlen = (uint8_t)(wLength & 0xFF);
                }
                if (qualifier) {
                    acc_saw_qualifier = true;
                }
                if (string_probe) {
                    acc_saw_string_probe = true;
                }
                if (set_feature) {
                    acc_saw_set_feature = true;
                }
                irq_unlock(key);

                /* Defer classification until enumeration goes quiet. */
                k_work_reschedule(
                    &settle_work,
                    K_MSEC(CONFIG_ZMK_USB_HOST_OS_DETECTION_SETTLE_MS));
            }
#endif
        }
    }

    return ret;
}
