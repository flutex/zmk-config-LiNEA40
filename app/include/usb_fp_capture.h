/*
 * USB Host Fingerprint Capture (measurement build)
 *
 * Public interface for reading captured USB control-transfer SETUP packets.
 * See src/usb_fp_capture.c for the capture path (linker --wrap on
 * usb_dc_ep_read).
 */

#ifndef ZMK_USB_FP_CAPTURE_H_
#define ZMK_USB_FP_CAPTURE_H_

#include <zephyr/kernel.h>

/* Ring buffer capacity. Older entries are overwritten past this count. */
#define USB_FP_CAPTURE_CAPACITY 48

/* One recorded standard SETUP packet (8 bytes decoded, little-endian). */
struct fp_entry {
    uint32_t ms;            /* k_uptime_get_32() at capture time */
    uint8_t bmRequestType;  /* data[0] */
    uint8_t bRequest;       /* data[1] */
    uint16_t wValue;        /* data[2] | data[3] << 8 */
    uint16_t wIndex;        /* data[4] | data[5] << 8 */
    uint16_t wLength;       /* data[6] | data[7] << 8 */
};

/*
 * Copy the buffered entries (oldest -> newest) into `out`, which must hold at
 * least USB_FP_CAPTURE_CAPACITY elements. Returns the number of valid entries
 * copied (min(total, USB_FP_CAPTURE_CAPACITY)). If `out_total` is non-NULL it
 * receives the monotonic total count of captured entries (may exceed capacity,
 * and may wrap on overflow). The copy runs under a short irq_lock.
 */
int usb_fp_capture_snapshot(struct fp_entry *out, uint32_t *out_total);

#endif /* ZMK_USB_FP_CAPTURE_H_ */
