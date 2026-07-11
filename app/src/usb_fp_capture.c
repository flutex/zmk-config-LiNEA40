/*
 * USB Host Fingerprint Capture (measurement build)
 *
 * Observes USB control-transfer SETUP packets during enumeration by wrapping
 * usb_dc_ep_read via the linker (-Wl,--wrap=usb_dc_ep_read). In
 * zephyr v3.5.0+zmk-fixes, subsys/usb/device/usb_device.c reads the 8-byte
 * SETUP with usb_dc_ep_read(USB_CONTROL_EP_OUT, &setup_raw, sizeof(setup_raw),
 * NULL). The filter (ep==0x00 && max_data_len==8 && read_bytes==NULL) matches
 * that read uniquely (data-stage reads pass a non-NULL read_bytes).
 *
 * This is a pure observer: it records standard-request SETUP packets into a
 * ring buffer inside a tiny irq_lock critical section. No logging, no events,
 * no work items -- the wrap runs in USB workqueue context and must return in a
 * few microseconds.
 */

#include <zephyr/kernel.h>
#include <zephyr/irq.h>

#include "usb_fp_capture.h"

/* USB_CONTROL_EP_OUT from Zephyr's usb device stack. */
#define USB_FP_CONTROL_EP_OUT 0x00

/* bmRequestType type field mask/value for standard requests (bits 6:5 == 00). */
#define USB_FP_REQTYPE_TYPE_MASK 0x60
#define USB_FP_REQTYPE_TYPE_STANDARD 0x00

extern int __real_usb_dc_ep_read(uint8_t ep, uint8_t *data, uint32_t max_data_len,
                                 uint32_t *read_bytes);

static struct fp_entry fp_buf[USB_FP_CAPTURE_CAPACITY];
static uint32_t fp_head;   /* next write index */
static uint32_t fp_total;  /* monotonic total captured (overflow observable) */

int __wrap_usb_dc_ep_read(uint8_t ep, uint8_t *data, uint32_t max_data_len,
                          uint32_t *read_bytes) {
    int ret = __real_usb_dc_ep_read(ep, data, max_data_len, read_bytes);

    /* Only the SETUP-stage read on the control OUT endpoint. */
    if (ret == 0 && ep == USB_FP_CONTROL_EP_OUT && max_data_len == 8 &&
        read_bytes == NULL && data != NULL) {
        uint8_t bmRequestType = data[0];

        /* Standard requests only; drop class/vendor (HID SET_IDLE, etc.). */
        if ((bmRequestType & USB_FP_REQTYPE_TYPE_MASK) == USB_FP_REQTYPE_TYPE_STANDARD) {
            /* Build the entry outside the lock to keep the section short. */
            struct fp_entry e;
            e.ms = k_uptime_get_32();
            e.bmRequestType = bmRequestType;
            e.bRequest = data[1];
            e.wValue = (uint16_t)(data[2] | ((uint16_t)data[3] << 8));
            e.wIndex = (uint16_t)(data[4] | ((uint16_t)data[5] << 8));
            e.wLength = (uint16_t)(data[6] | ((uint16_t)data[7] << 8));

            unsigned int key = irq_lock();
            fp_buf[fp_head] = e;
            fp_head = (fp_head + 1) % USB_FP_CAPTURE_CAPACITY;
            fp_total++;
            irq_unlock(key);
        }
    }

    return ret;
}

int usb_fp_capture_snapshot(struct fp_entry *out, uint32_t *out_total) {
    if (out == NULL) {
        return 0;
    }

    unsigned int key = irq_lock();
    uint32_t total = fp_total;
    uint32_t n = (total < USB_FP_CAPTURE_CAPACITY) ? total : USB_FP_CAPTURE_CAPACITY;
    /* When wrapped, the oldest entry sits at fp_head; otherwise start at 0. */
    uint32_t start = (total <= USB_FP_CAPTURE_CAPACITY) ? 0 : fp_head;
    for (uint32_t i = 0; i < n; i++) {
        out[i] = fp_buf[(start + i) % USB_FP_CAPTURE_CAPACITY];
    }
    irq_unlock(key);

    if (out_total != NULL) {
        *out_total = total;
    }
    return (int)n;
}
