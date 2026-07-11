/*
 * zmk_usb_host_os_changed — raised when the detected wired USB host OS changes.
 *
 * Defined in-module (not a ZMK core fork), same approach as the custom GATT
 * service. Raised only from thread context (settle workqueue handler and the
 * USB-conn-state listener) — never from the usb_dc_ep_read wrap.
 */

#ifndef ZMK_EVENTS_USB_HOST_OS_CHANGED_H_
#define ZMK_EVENTS_USB_HOST_OS_CHANGED_H_

#include <zephyr/kernel.h>
#include <zmk/event_manager.h>

#include <zmk_usb_host_os.h>

struct zmk_usb_host_os_changed {
    enum zmk_usb_host_os os;
};

ZMK_EVENT_DECLARE(zmk_usb_host_os_changed);

#endif /* ZMK_EVENTS_USB_HOST_OS_CHANGED_H_ */
