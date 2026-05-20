/*
 * Battery Level Typing Behavior for ZMK
 *
 * Sends battery levels as keystrokes: "L:50% R:80% BT0"
 * Assumes US keyboard layout on the host OS.
 */

#define DT_DRV_COMPAT zmk_behavior_battery_type

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
#include <zmk/split/central.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* Delay between keystrokes (ms). Increase if characters are dropped over BLE. */
#define KEYSTROKE_DELAY_MS 30

/* Output format: "L:50% R:80% BT0" */
#define FORMAT_PREFIX   "L:"
#define FORMAT_MIDDLE   "% R:"
#define FORMAT_SUFFIX   "%"
#define FORMAT_NO_DATA  "--"

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
#define HID_KEY_SEMICOLON  0x33
#define HID_KEY_PERIOD     0x37
#define HID_KEY_LSHIFT     0xE1

struct char_keycode {
    uint32_t keycode;
    bool shift;
};

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
    ['%'] = { .keycode = HID_KEY_5, .shift = true },            /* US: Shift+5 = % */
    ['-'] = { .keycode = HID_KEY_MINUS, .shift = false },
    ['>'] = { .keycode = HID_KEY_PERIOD, .shift = true },       /* US: Shift+. = > */
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

static int send_number(int value) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", value);
    return send_string(buf);
}

static int get_central_battery(void) {
    int soc = zmk_battery_state_of_charge();
    return (soc >= 0) ? soc : -1;
}

static int get_peripheral_battery(void) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
    uint8_t level = 0;
    int rc = zmk_split_central_get_peripheral_battery_level(0, &level);
    return (rc == 0) ? (int)level : -1;
#else
    return -1;
#endif
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    int left_bat = get_central_battery();
    int right_bat = get_peripheral_battery();

    send_string(FORMAT_PREFIX);
    if (left_bat >= 0) {
        send_number(left_bat);
    } else {
        send_string(FORMAT_NO_DATA);
    }

    send_string(FORMAT_MIDDLE);

    if (right_bat >= 0) {
        send_number(right_bat);
    } else {
        send_string(FORMAT_NO_DATA);
    }

    send_string(FORMAT_SUFFIX);

    /* Append active BT profile: " BT0" */
    int active = zmk_ble_active_profile_index();
    char bt_str[] = { ' ', 'B', 'T', '0' + active, '\0' };
    send_string(bt_str);

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static int behavior_battery_type_init(const struct device *dev) {
    return 0;
}

static const struct behavior_driver_api behavior_battery_type_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
};

#define BATTERY_TYPE_INST(n)                                                    \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_battery_type_init, NULL, NULL, NULL,    \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,   \
                            &behavior_battery_type_driver_api);

DT_INST_FOREACH_STATUS_OKAY(BATTERY_TYPE_INST)
