/*
 * layer_status_gatt — アクティブレイヤー状態を自作BLE GATTサービスでMacへ通知する
 *
 * LineaStudio（Mac設定アプリ）のリアルタイムキーマップ表示（オーバーレイ）用。
 * ZMK Studio の特性とは独立した自前サービスなので、公式RPCと衝突しない。
 *
 * Service:        4C4E4541-3430-4B42-0001-000000000001  ("LNEA"-"40"-"KB")
 * Characteristic: 4C4E4541-3430-4B42-0001-000000000002  (Read + Notify, 要暗号化)
 *
 * ペイロード(8バイト・旧5/7バイトの上位互換):
 *   [0..3] レイヤー状態ビットマスク (uint32 LE, bit N = レイヤーN有効)
 *   [4]    最上位アクティブレイヤー index (uint8)
 *   [5]    アクティブBTプロファイル index (uint8, 0..4。USB選択中も選択中プロファイルを保持)
 *   [6]    アクティブエンドポイント (uint8, 0=USB / 1=BLE)
 *   [7]    検出USBホストOS (uint8, 0=UNKNOWN/Windows / 1=macOS)。
 *          CONFIG_ZMK_USB_HOST_OS_DETECTION 有効時のみ存在。[6]==0(USB)のとき意味を持つ。
 * Mac側はペイロード長で新旧を判別する（>=7で[5][6]、>=8で[7]を読む前方互換）。
 * 無効時は従来どおり7バイト。
 *
 * レイヤー/プロファイル/エンドポイント変更は低頻度なのでBLE帯域・電池への影響は無視できる。
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/byteorder.h>

#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/keymap.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>

#if IS_ENABLED(CONFIG_ZMK_USB_HOST_OS_DETECTION)
#include <zmk_usb_host_os.h>
#include <zmk/events/usb_host_os_changed.h>
#define LAYER_PAYLOAD_LEN 8
#else
#define LAYER_PAYLOAD_LEN 7
#endif

static uint8_t layer_payload[LAYER_PAYLOAD_LEN];
static bool notify_enabled;

static void update_payload(void) {
    sys_put_le32(zmk_keymap_layer_state(), layer_payload);
    layer_payload[4] = (uint8_t)zmk_keymap_highest_layer_active();
    /* 選択中BTプロファイル（USB選択中も有効）。ep.ble.profile_index はBLE時しか設定されないため使わない */
    layer_payload[5] = (uint8_t)zmk_ble_active_profile_index();
    struct zmk_endpoint_instance ep = zmk_endpoints_selected();
    layer_payload[6] = (uint8_t)ep.transport; /* 0=USB / 1=BLE */
#if IS_ENABLED(CONFIG_ZMK_USB_HOST_OS_DETECTION)
    /* [7]: 0=UNKNOWN/Windows, 1=macOS. concern#1整合: USB切断でenumがUNKNOWNへ戻り
     * [7]も自然に0へ戻る。 */
    layer_payload[7] =
        (zmk_usb_host_os_current() == ZMK_USB_HOST_OS_MACOS) ? 1 : 0;
#endif
}

static void layer_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    notify_enabled = (value == BT_GATT_CCC_NOTIFY);
}

static ssize_t read_layer_state(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                                uint16_t len, uint16_t offset) {
    update_payload();
    return bt_gatt_attr_read(conn, attr, buf, len, offset, layer_payload, sizeof(layer_payload));
}

static const struct bt_uuid_128 layer_svc_uuid =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x4C4E4541, 0x3430, 0x4B42, 0x0001, 0x000000000001));
static const struct bt_uuid_128 layer_chrc_uuid =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x4C4E4541, 0x3430, 0x4B42, 0x0001, 0x000000000002));

BT_GATT_SERVICE_DEFINE(
    layer_status_svc, BT_GATT_PRIMARY_SERVICE((void *)&layer_svc_uuid),
    BT_GATT_CHARACTERISTIC(&layer_chrc_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT, read_layer_state, NULL, NULL),
    BT_GATT_CCC(layer_ccc_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT), );

static int layer_status_listener(const zmk_event_t *eh) {
    update_payload();
    if (notify_enabled) {
        /* attrs[1] = characteristic 宣言。bt_gatt_notify が value attr を解決する */
        bt_gatt_notify(NULL, &layer_status_svc.attrs[1], layer_payload, sizeof(layer_payload));
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(layer_status_gatt, layer_status_listener);
ZMK_SUBSCRIPTION(layer_status_gatt, zmk_layer_state_changed);
/* エンドポイント切替（BLE選択中のプロファイル切替でも発火）でペイロード[5][6]を更新 */
ZMK_SUBSCRIPTION(layer_status_gatt, zmk_endpoint_changed);
/* USB選択中はendpoint_changedが発火しないため、プロファイル切替を直接購読して[5]を追随 */
ZMK_SUBSCRIPTION(layer_status_gatt, zmk_ble_active_profile_changed);
#if IS_ENABLED(CONFIG_ZMK_USB_HOST_OS_DETECTION)
/* USBホストOS検出確定/切断リセットで[7]を追随notify */
ZMK_SUBSCRIPTION(layer_status_gatt, zmk_usb_host_os_changed);
#endif
