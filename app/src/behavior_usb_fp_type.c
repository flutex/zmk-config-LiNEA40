/*
 * USB Host Fingerprint Typing Behavior for ZMK (measurement build)
 *
 * On key press, snapshots the captured USB SETUP-packet ring buffer
 * (usb_fp_capture) and types a compact decoded transcript as keystrokes, using
 * the same blocking zmk_hid_keyboard_press/release + zmk_endpoints_send_report
 * mechanism as behavior_battery_type.c. Assumes a US keyboard layout on host.
 *
 * Pure readout: no layer ops, no events, no classification -- observation only.
 */

#define DT_DRV_COMPAT zmk_behavior_usb_fp_type

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>

#include "usb_fp_capture.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* Delay between keystrokes (ms). Matches behavior_battery_type.c. */
#define KEYSTROKE_DELAY_MS 30

/* HID Usage IDs (USB HID Usage Tables, Keyboard/Keypad Page 0x07) */
#define HID_KEY_A          0x04
#define HID_KEY_B          0x05
#define HID_KEY_C          0x06
#define HID_KEY_D          0x07
#define HID_KEY_E          0x08
#define HID_KEY_F          0x09
#define HID_KEY_G          0x0A
#define HID_KEY_H          0x0B
#define HID_KEY_I          0x0C
#define HID_KEY_J          0x0D
#define HID_KEY_K          0x0E
#define HID_KEY_L          0x0F
#define HID_KEY_M          0x10
#define HID_KEY_N          0x11
#define HID_KEY_O          0x12
#define HID_KEY_P          0x13
#define HID_KEY_Q          0x14
#define HID_KEY_R          0x15
#define HID_KEY_S          0x16
#define HID_KEY_T          0x17
#define HID_KEY_U          0x18
#define HID_KEY_V          0x19
#define HID_KEY_W          0x1A
#define HID_KEY_X          0x1B
#define HID_KEY_Y          0x1C
#define HID_KEY_Z          0x1D
#define HID_KEY_1          0x1E
#define HID_KEY_2          0x1F
#define HID_KEY_3          0x20
#define HID_KEY_4          0x21
#define HID_KEY_5          0x22
#define HID_KEY_6          0x23
#define HID_KEY_7          0x24
#define HID_KEY_8          0x25
#define HID_KEY_9          0x26
#define HID_KEY_0          0x27
#define HID_KEY_SPACE      0x2C
#define HID_KEY_MINUS      0x2D
#define HID_KEY_EQUAL      0x2E
#define HID_KEY_BACKSLASH  0x31
#define HID_KEY_SEMICOLON  0x33
#define HID_KEY_PERIOD     0x37
#define HID_KEY_LSHIFT     0xE1

struct char_keycode {
    uint32_t keycode;
    bool shift;
};

/* Only the characters this behavior emits: 0-9 A-Z a-z space : | + = . - */
static const struct char_keycode CHAR_MAP[] = {
    ['0'] = { .keycode = HID_KEY_0, .shift = false },
    ['1'] = { .keycode = HID_KEY_1, .shift = false },
    ['2'] = { .keycode = HID_KEY_2, .shift = false },
    ['3'] = { .keycode = HID_KEY_3, .shift = false },
    ['4'] = { .keycode = HID_KEY_4, .shift = false },
    ['5'] = { .keycode = HID_KEY_5, .shift = false },
    ['6'] = { .keycode = HID_KEY_6, .shift = false },
    ['7'] = { .keycode = HID_KEY_7, .shift = false },
    ['8'] = { .keycode = HID_KEY_8, .shift = false },
    ['9'] = { .keycode = HID_KEY_9, .shift = false },
    ['A'] = { .keycode = HID_KEY_A, .shift = true },
    ['B'] = { .keycode = HID_KEY_B, .shift = true },
    ['C'] = { .keycode = HID_KEY_C, .shift = true },
    ['D'] = { .keycode = HID_KEY_D, .shift = true },
    ['E'] = { .keycode = HID_KEY_E, .shift = true },
    ['F'] = { .keycode = HID_KEY_F, .shift = true },
    ['G'] = { .keycode = HID_KEY_G, .shift = true },
    ['H'] = { .keycode = HID_KEY_H, .shift = true },
    ['I'] = { .keycode = HID_KEY_I, .shift = true },
    ['J'] = { .keycode = HID_KEY_J, .shift = true },
    ['K'] = { .keycode = HID_KEY_K, .shift = true },
    ['L'] = { .keycode = HID_KEY_L, .shift = true },
    ['M'] = { .keycode = HID_KEY_M, .shift = true },
    ['N'] = { .keycode = HID_KEY_N, .shift = true },
    ['O'] = { .keycode = HID_KEY_O, .shift = true },
    ['P'] = { .keycode = HID_KEY_P, .shift = true },
    ['Q'] = { .keycode = HID_KEY_Q, .shift = true },
    ['R'] = { .keycode = HID_KEY_R, .shift = true },
    ['S'] = { .keycode = HID_KEY_S, .shift = true },
    ['T'] = { .keycode = HID_KEY_T, .shift = true },
    ['U'] = { .keycode = HID_KEY_U, .shift = true },
    ['V'] = { .keycode = HID_KEY_V, .shift = true },
    ['W'] = { .keycode = HID_KEY_W, .shift = true },
    ['X'] = { .keycode = HID_KEY_X, .shift = true },
    ['Y'] = { .keycode = HID_KEY_Y, .shift = true },
    ['Z'] = { .keycode = HID_KEY_Z, .shift = true },
    ['a'] = { .keycode = HID_KEY_A, .shift = false },
    ['b'] = { .keycode = HID_KEY_B, .shift = false },
    ['c'] = { .keycode = HID_KEY_C, .shift = false },
    ['d'] = { .keycode = HID_KEY_D, .shift = false },
    ['e'] = { .keycode = HID_KEY_E, .shift = false },
    ['f'] = { .keycode = HID_KEY_F, .shift = false },
    ['g'] = { .keycode = HID_KEY_G, .shift = false },
    ['h'] = { .keycode = HID_KEY_H, .shift = false },
    ['i'] = { .keycode = HID_KEY_I, .shift = false },
    ['j'] = { .keycode = HID_KEY_J, .shift = false },
    ['k'] = { .keycode = HID_KEY_K, .shift = false },
    ['l'] = { .keycode = HID_KEY_L, .shift = false },
    ['m'] = { .keycode = HID_KEY_M, .shift = false },
    ['n'] = { .keycode = HID_KEY_N, .shift = false },
    ['o'] = { .keycode = HID_KEY_O, .shift = false },
    ['p'] = { .keycode = HID_KEY_P, .shift = false },
    ['q'] = { .keycode = HID_KEY_Q, .shift = false },
    ['r'] = { .keycode = HID_KEY_R, .shift = false },
    ['s'] = { .keycode = HID_KEY_S, .shift = false },
    ['t'] = { .keycode = HID_KEY_T, .shift = false },
    ['u'] = { .keycode = HID_KEY_U, .shift = false },
    ['v'] = { .keycode = HID_KEY_V, .shift = false },
    ['w'] = { .keycode = HID_KEY_W, .shift = false },
    ['x'] = { .keycode = HID_KEY_X, .shift = false },
    ['y'] = { .keycode = HID_KEY_Y, .shift = false },
    ['z'] = { .keycode = HID_KEY_Z, .shift = false },
    [' '] = { .keycode = HID_KEY_SPACE, .shift = false },
    [':'] = { .keycode = HID_KEY_SEMICOLON, .shift = true },   /* US: Shift+; = : */
    ['|'] = { .keycode = HID_KEY_BACKSLASH, .shift = true },   /* US: Shift+\ = | */
    ['+'] = { .keycode = HID_KEY_EQUAL, .shift = true },       /* US: Shift+= = + */
    ['='] = { .keycode = HID_KEY_EQUAL, .shift = false },
    ['.'] = { .keycode = HID_KEY_PERIOD, .shift = false },
    ['-'] = { .keycode = HID_KEY_MINUS, .shift = false },
};

#define CHAR_MAP_SIZE (sizeof(CHAR_MAP) / sizeof(CHAR_MAP[0]))

static const uint32_t HID_LSHIFT = HID_KEY_LSHIFT;

static int send_char(char c) {
    if ((uint8_t)c >= CHAR_MAP_SIZE || CHAR_MAP[(uint8_t)c].keycode == 0) {
        LOG_WRN("No keycode mapping for char: 0x%02x", c);
        return -EINVAL;
    }

    const struct char_keycode *mapping = &CHAR_MAP[(uint8_t)c];

    if (mapping->shift) {
        zmk_hid_keyboard_press(HID_LSHIFT);
        zmk_endpoints_send_report(HID_USAGE_KEY);
        k_msleep(KEYSTROKE_DELAY_MS);
    }

    zmk_hid_keyboard_press(mapping->keycode);
    zmk_endpoints_send_report(HID_USAGE_KEY);
    k_msleep(KEYSTROKE_DELAY_MS);

    zmk_hid_keyboard_release(mapping->keycode);
    if (mapping->shift) {
        zmk_hid_keyboard_release(HID_LSHIFT);
    }
    zmk_endpoints_send_report(HID_USAGE_KEY);
    k_msleep(KEYSTROKE_DELAY_MS);

    return 0;
}

static int send_string(const char *str) {
    for (int i = 0; str[i] != '\0'; i++) {
        int ret = send_char(str[i]);
        if (ret < 0) {
            return ret;
        }
    }
    return 0;
}

/*
 * Transcript assembly. We build the whole string into a static buffer first,
 * then type it. Typical enumeration transcripts are well under 512 bytes, but
 * the theoretical worst case is larger: 48 entries in the generic
 * "R<type>.<req>:<len> " form are ~12 chars each (576), plus up to 47 gap
 * markers "|+<seconds>s " (~11 chars each, 517), plus the "FP n=... last48 "
 * prefix (~23) and " END" (~4) -> ~1120 bytes. Size the buffer above that so
 * append_str/append_char never silently truncate and drop the trailing " END".
 * All emitted characters are restricted to the CHAR_MAP set above (no '?'),
 * so send_string never aborts mid-transcript on an unmapped character.
 */
static char out_buf[1280];
static int out_len;

static void buf_reset(void) {
    out_len = 0;
    out_buf[0] = '\0';
}

static void append_str(const char *s) {
    while (*s != '\0' && out_len < (int)sizeof(out_buf) - 1) {
        out_buf[out_len++] = *s++;
    }
    out_buf[out_len] = '\0';
}

static void append_char(char c) {
    if (out_len < (int)sizeof(out_buf) - 1) {
        out_buf[out_len++] = c;
        out_buf[out_len] = '\0';
    }
}

static void append_dec(uint32_t v) {
    char tmp[12];
    snprintf(tmp, sizeof(tmp), "%u", (unsigned int)v);
    append_str(tmp);
}

/* Uppercase hex, no leading zeros (matches spec examples "S3:FF"). */
static void append_hex(uint32_t v) {
    char tmp[12];
    snprintf(tmp, sizeof(tmp), "%X", (unsigned int)v);
    append_str(tmp);
}

/* Standard GET_DESCRIPTOR descriptor-type -> single-letter tag, or 0. */
static char desc_type_char(uint8_t type) {
    switch (type) {
    case 0x01: return 'D'; /* device */
    case 0x02: return 'C'; /* configuration */
    case 0x03: return 'S'; /* string */
    case 0x06: return 'Q'; /* device_qualifier */
    case 0x07: return 'O'; /* other_speed_configuration */
    case 0x0F: return 'B'; /* BOS */
    default:   return 0;
    }
}

static void append_entry(const struct fp_entry *e) {
    uint8_t bmrt = e->bmRequestType;
    uint8_t brq = e->bRequest;
    uint8_t desc_type = (uint8_t)((e->wValue >> 8) & 0xFF);
    uint8_t desc_index = (uint8_t)(e->wValue & 0xFF);
    char dt = 0;

    if (bmrt == 0x80 && brq == 0x06 && (dt = desc_type_char(desc_type)) != 0) {
        /* GET_DESCRIPTOR (device-to-host, standard, device recipient) */
        append_char(dt);
        append_dec(desc_index);
        append_char(':');
        append_hex(e->wLength);
    } else if (bmrt == 0x81 && brq == 0x06 && desc_type == 0x22) {
        /* GET_DESCRIPTOR HID report descriptor (interface recipient) */
        append_char('H');
        append_dec(e->wIndex);
        append_char(':');
        append_hex(e->wLength);
    } else if (bmrt == 0x00 && brq == 0x05) {
        /* SET_ADDRESS */
        append_str("SA");
    } else if (bmrt == 0x00 && brq == 0x09) {
        /* SET_CONFIGURATION */
        append_str("SC");
        append_dec(e->wValue);
    } else if (bmrt == 0x00 && brq == 0x03) {
        /* SET_FEATURE (REMOTE_WAKEUP=1 significant for iOS) */
        append_str("SF");
        append_dec(e->wValue);
    } else if (bmrt == 0x00 && brq == 0x01) {
        /* CLEAR_FEATURE */
        append_str("CF");
        append_dec(e->wValue);
    } else {
        /* Any other standard request: R<bmRequestType>.<bRequest>:<wLength> */
        append_char('R');
        append_hex(bmrt);
        append_char('.');
        append_hex(brq);
        append_char(':');
        append_hex(e->wLength);
    }
}

static struct fp_entry snapshot[USB_FP_CAPTURE_CAPACITY];

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    uint32_t total = 0;
    int n = usb_fp_capture_snapshot(snapshot, &total);

    buf_reset();

    /* Prefix: "FP n=<total> " (+ "last48" flag when the buffer wrapped). */
    append_str("FP n=");
    append_dec(total);
    if (total > USB_FP_CAPTURE_CAPACITY) {
        append_str(" last48");
    }
    append_char(' ');

    uint32_t prev_ms = 0;
    for (int i = 0; i < n; i++) {
        const struct fp_entry *e = &snapshot[i];
        if (i > 0 && (e->ms - prev_ms) > 1000) {
            /* Session gap marker before this entry: "|+<seconds>s ". */
            append_str("|+");
            append_dec((e->ms - prev_ms) / 1000);
            append_str("s ");
        }
        append_entry(e);
        append_char(' ');
        prev_ms = e->ms;
    }

    append_str("END");

    send_string(out_buf);

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static int behavior_usb_fp_type_init(const struct device *dev) {
    return 0;
}

static const struct behavior_driver_api behavior_usb_fp_type_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
};

#define USB_FP_TYPE_INST(n)                                                     \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_usb_fp_type_init, NULL, NULL, NULL,     \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,   \
                            &behavior_usb_fp_type_driver_api);

DT_INST_FOREACH_STATUS_OKAY(USB_FP_TYPE_INST)
