/*
 * Wired USB host OS detection — public interface.
 *
 * See src/usb_host_os.c for the enumeration-fingerprint classifier. The
 * classifier owns the single linker --wrap on usb_dc_ep_read and, after a
 * settle delay, resolves the attached wired USB host to one of the values
 * below. Consumers (behavior_os_layer.c, layer_status_gatt.c) read the current
 * value and/or subscribe to zmk_usb_host_os_changed.
 */

#ifndef ZMK_USB_HOST_OS_H_
#define ZMK_USB_HOST_OS_H_

/*
 * Detected wired USB host OS.
 * Values are stable and semantic; do NOT assume the GATT wire byte equals the
 * enum value (layer_status_gatt.c maps MACOS->1, everything else->0).
 */
enum zmk_usb_host_os {
    ZMK_USB_HOST_OS_UNKNOWN = 0,
    ZMK_USB_HOST_OS_WINDOWS = 1,
    ZMK_USB_HOST_OS_MACOS = 2,
};

/*
 * Current detected host OS. Returns ZMK_USB_HOST_OS_UNKNOWN until the
 * enumeration fingerprint settles, and again after USB disconnect (host stops
 * enumerating us). Callers must treat UNKNOWN as "fall back to the safe
 * Windows default".
 */
enum zmk_usb_host_os zmk_usb_host_os_current(void);

#endif /* ZMK_USB_HOST_OS_H_ */
