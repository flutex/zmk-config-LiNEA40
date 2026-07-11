/*
 * USB Host Fingerprint Capture (measurement / debug build)
 *
 * Ring buffer + snapshot for observed USB control-transfer SETUP packets. The
 * SETUP observation point (the linker --wrap on usb_dc_ep_read) now lives in
 * src/usb_host_os.c, which owns the single allowed wrap and calls
 * usb_fp_capture_record() here when CONFIG_ZMK_USB_FP_CAPTURE is enabled. This
 * file no longer defines a wrap, so the wrap symbol is never duplicated when
 * both the OS detector and the FP capture are built together.
 *
 * usb_fp_capture_record() runs in USB workqueue context (called from the wrap)
 * and does only a tiny irq_lock-guarded ring insert -- no logging, no events.
 */

#include <zephyr/kernel.h>
#include <zephyr/irq.h>

#include "usb_fp_capture.h"

static struct fp_entry fp_buf[USB_FP_CAPTURE_CAPACITY];
static uint32_t fp_head;   /* next write index */
static uint32_t fp_total;  /* monotonic total captured (overflow observable) */

void usb_fp_capture_record(const struct fp_entry *e) {
    if (e == NULL) {
        return;
    }
    unsigned int key = irq_lock();
    fp_buf[fp_head] = *e;
    fp_head = (fp_head + 1) % USB_FP_CAPTURE_CAPACITY;
    fp_total++;
    irq_unlock(key);
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
