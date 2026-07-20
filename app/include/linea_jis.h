/*
 * Per-BLE-profile JIS layout flags shared between behavior_os_layer.c (owner)
 * and layer_status_gatt.c (BLE read/write surface for LineaStudio).
 *
 * Bit n (0..4) = BT profile n uses a JIS host layout (JIS_MODE layer engages
 * when that profile is active). Bits 5..7 are reserved and always 0.
 */

#pragma once

#include <stdint.h>

/** Current flags (bits 0..4). */
uint8_t linea_jis_flags_get(void);

/**
 * Replace flags (masked to bits 0..4). Persists to settings and re-applies
 * the JIS_MODE layer for the currently active endpoint/profile. Safe to call
 * from the BT RX thread: persistence + layer refresh run on the system
 * workqueue.
 */
void linea_jis_flags_set(uint8_t flags);
