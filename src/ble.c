/**
 * Bluetooth LE interface. Two services:
 *
 * EOM service 0x6969, used by the EOM UI:
 *   0x696A status  (read/notify) 17 bytes, see status_value()
 *   0x696B control (write)       0x01 mode,trigger,reset[,pleasure]
 *                                0x02 "key:value" sets a config option
 *                                0x03 requests a config dump on 0x696C
 *   0x696C config  (notify)      config as JSON fragments without braces
 *
 * Monsterpub emulation 0x6000, used by XToys:
 *   0x6031 output (read/notify) pleasure as a little endian 0-1530 "pressure"
 *   0x6003 input  (write)       vibration 0-100, sets pleasure in manual mode
 */
#include "ble.h"
#include "config.h"
#include "eom_hal.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "orgasm_control.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <stdio.h>
#include <string.h>

#define EOM_SVC_UUID 0x6969
#define EOM_STATUS_UUID 0x696A
#define EOM_CONTROL_UUID 0x696B
#define EOM_CONFIG_UUID 0x696C
#define MONSTERPUB_SVC_UUID 0x6000
#define MONSTERPUB_OUTPUT_UUID 0x6031
#define MONSTERPUB_INPUT_UUID 0x6003

#define STATUS_INTERVAL_MS 50
#define OUTPUT_INTERVAL_MS 100
#define CONFIG_DUMP_SIZE 2048

void ble_store_config_init(void);

static const char* TAG = "ble";

static uint8_t own_addr_type;
static volatile uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;

static uint16_t status_handle;
static uint16_t control_handle;
static uint16_t config_handle;
static uint16_t output_handle;
static uint16_t input_handle;

static volatile bool status_subscribed = false;
static volatile bool config_subscribed = false;
static volatile bool output_subscribed = false;

static volatile bool config_dump_requested = false;
static char* config_dump = NULL;
static size_t config_dump_offset = 0;

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void status_value(uint8_t out[17]) {
    uint16_t permit = orgasm_control_get_permit_orgasm_remaining_seconds();
    uint32_t ms = now_ms();
    RGBColor color = eom_hal_get_rgb();

    // 12 bit values are sent as 8 bit.
    out[0] = orgasm_control_get_last_pressure() >> 4;
    out[1] = orgasm_control_get_arousal() >> 4;
    out[2] = Config.sensitivity_threshold >> 4;
    out[3] = orgasm_control_get_pleasure();
    out[4] = orgasm_control_get_cooldown();
    out[5] = orgasm_control_get_denied_count();
    out[6] = orgasm_control_get_output_mode();
    out[7] = Config.vibration_mode;
    out[8] = permit >> 8;
    out[9] = permit & 0xFF;
    out[10] = color.r;
    out[11] = color.g;
    out[12] = color.b;
    out[13] = ms >> 24;
    out[14] = ms >> 16;
    out[15] = ms >> 8;
    out[16] = ms;
}

static uint16_t output_value(void) {
    // Monsterpub pressure runs 0-1530.
    return orgasm_control_get_pleasure() * 1530 / 255;
}

// Copies a write into buf, NUL terminated. Returns the length or -1.
static int read_write(struct ble_gatt_access_ctxt* ctxt, uint8_t* buf, size_t size) {
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len == 0 || len >= size) return -1;
    if (ble_hs_mbuf_to_flat(ctxt->om, buf, len, NULL) != 0) return -1;
    buf[len] = '\0';
    return len;
}

static void handle_control(const uint8_t* buf, int len) {
    switch (buf[0]) {
    case 0x01: // mode, trigger arousal, reset denied[, pleasure]
        if (len < 4) return;
        if (buf[1] != orgasm_control_get_output_mode()) orgasm_control_set_output_mode(buf[1]);
        if (buf[2]) orgasm_control_trigger_arousal();
        if (buf[3]) orgasm_control_reset_denied();
        if (len >= 5) orgasm_control_set_manual_pleasure(buf[4]);
        break;

    case 0x02: { // key:value
        char* text = (char*)buf + 1;
        char* colon = strchr(text, ':');
        if (colon == NULL) return;
        *colon = '\0';
        if (config_set_value(text, colon + 1)) {
            config_enqueue_save();
        } else {
            ESP_LOGW(TAG, "Unknown setting %s", text);
        }
        break;
    }

    case 0x03: // config dump
        config_dump_requested = true;
        break;
    }
}

static int access_cb(
    uint16_t conn, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg
) {
    uint8_t buf[128];

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        if (attr_handle == status_handle) {
            uint8_t value[17];
            status_value(value);
            return os_mbuf_append(ctxt->om, value, sizeof(value)) ? BLE_ATT_ERR_INSUFFICIENT_RES
                                                                   : 0;
        }
        if (attr_handle == output_handle) {
            uint16_t v = output_value();
            uint8_t value[2] = { v & 0xFF, v >> 8 };
            return os_mbuf_append(ctxt->om, value, sizeof(value)) ? BLE_ATT_ERR_INSUFFICIENT_RES
                                                                   : 0;
        }
        return 0; // config: only sent as notifications
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        int len = read_write(ctxt, buf, sizeof(buf));
        if (len < 0) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;

        if (attr_handle == control_handle) {
            handle_control(buf, len);
        } else if (attr_handle == input_handle) {
            // XToys sends 0-100 per motor, only the first is used.
            orgasm_control_set_manual_pleasure(buf[0] > 100 ? 255 : buf[0] * 255 / 100);
        }
        return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(EOM_SVC_UUID),
        .characteristics =
            (struct ble_gatt_chr_def[]) {
                {
                    .uuid = BLE_UUID16_DECLARE(EOM_STATUS_UUID),
                    .access_cb = access_cb,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                    .val_handle = &status_handle,
                },
                {
                    .uuid = BLE_UUID16_DECLARE(EOM_CONTROL_UUID),
                    .access_cb = access_cb,
                    .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                    .val_handle = &control_handle,
                },
                {
                    .uuid = BLE_UUID16_DECLARE(EOM_CONFIG_UUID),
                    .access_cb = access_cb,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                    .val_handle = &config_handle,
                },
                { 0 },
            },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(MONSTERPUB_SVC_UUID),
        .characteristics =
            (struct ble_gatt_chr_def[]) {
                {
                    .uuid = BLE_UUID16_DECLARE(MONSTERPUB_OUTPUT_UUID),
                    .access_cb = access_cb,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                    .val_handle = &output_handle,
                },
                {
                    .uuid = BLE_UUID16_DECLARE(MONSTERPUB_INPUT_UUID),
                    .access_cb = access_cb,
                    .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                    .val_handle = &input_handle,
                },
                { 0 },
            },
    },
    { 0 },
};

//=== Config dump

// Builds "key":value,"key":value,... from the non-secret options.
static char* build_config_dump(void) {
    cJSON* root = cJSON_CreateObject();
    config_to_json(root, false);

    char* out = calloc(1, CONFIG_DUMP_SIZE);
    size_t used = 0;
    const cJSON* item = NULL;

    cJSON_ArrayForEach(item, root) {
        char* value = cJSON_PrintUnformatted(item);
        if (value == NULL || out == NULL) break;
        int n = snprintf(
            out + used,
            CONFIG_DUMP_SIZE - used,
            "%s\"%s\":%s",
            used ? "," : "",
            item->string,
            value
        );
        cJSON_free(value);
        if (n < 0 || used + n >= CONFIG_DUMP_SIZE) break;
        used += n;
    }

    cJSON_Delete(root);
    return out;
}

// Each notification must hold complete "key":value pairs, so split on commas between pairs.
static void send_config_chunk(void) {
    size_t max = ble_att_mtu(conn_handle) - 3;
    const char* start = config_dump + config_dump_offset;
    size_t remaining = strlen(start);
    size_t len = remaining;

    if (len > max) {
        len = 0;
        for (size_t i = 0; i < max; i++) {
            if (start[i] == ',' && start[i + 1] == '"') len = i;
        }
        if (len == 0) len = max; // a single pair longer than the MTU
    }

    struct os_mbuf* om = ble_hs_mbuf_from_flat(start, len);
    if (om == NULL || ble_gatts_notify_custom(conn_handle, config_handle, om) != 0) return;

    config_dump_offset += len;
    if (start[len] == ',') config_dump_offset++;

    if (config_dump[config_dump_offset] == '\0') {
        free(config_dump);
        config_dump = NULL;
    }
}

//=== Notifications

void ble_tick(void) {
    static uint32_t last_status = 0;
    static uint32_t last_output = 0;
    uint16_t conn = conn_handle;
    if (conn == BLE_HS_CONN_HANDLE_NONE) return;

    uint32_t now = now_ms();

    if (status_subscribed && now - last_status >= STATUS_INTERVAL_MS) {
        last_status = now;
        uint8_t value[17];
        status_value(value);
        ble_gatts_notify_custom(conn, status_handle, ble_hs_mbuf_from_flat(value, sizeof(value)));
    }

    if (output_subscribed && now - last_output >= OUTPUT_INTERVAL_MS) {
        last_output = now;
        uint16_t v = output_value();
        uint8_t value[2] = { v & 0xFF, v >> 8 };
        ble_gatts_notify_custom(conn, output_handle, ble_hs_mbuf_from_flat(value, sizeof(value)));
    }

    if (config_dump_requested) {
        config_dump_requested = false;
        free(config_dump);
        config_dump = build_config_dump();
        config_dump_offset = 0;
    }
    if (config_dump != NULL && config_subscribed) send_config_chunk();
}

bool ble_is_connected(void) {
    return conn_handle != BLE_HS_CONN_HANDLE_NONE;
}

//=== Connection handling

static void start_advertising(void);

static void reset_connection(void) {
    conn_handle = BLE_HS_CONN_HANDLE_NONE;
    status_subscribed = config_subscribed = output_subscribed = false;
    free(config_dump);
    config_dump = NULL;
}

static int gap_event(struct ble_gap_event* event, void* arg) {
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "Connected");
        } else {
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected, reason %d", event->disconnect.reason);
        reset_connection();
        start_advertising();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        break;

    case BLE_GAP_EVENT_SUBSCRIBE: {
        uint16_t attr = event->subscribe.attr_handle;
        bool on = event->subscribe.cur_notify;
        if (attr == status_handle) status_subscribed = on;
        if (attr == config_handle) config_subscribed = on;
        if (attr == output_handle) output_subscribed = on;
        break;
    }

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU %d", event->mtu.value);
        break;
    }
    return 0;
}

static void start_advertising(void) {
    const char* name = ble_svc_gap_device_name();

    struct ble_hs_adv_fields fields = {
        .flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,
        .name = (uint8_t*)name,
        .name_len = strlen(name),
        .name_is_complete = 1,
        .tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO,
        .tx_pwr_lvl_is_present = 1,
    };
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Advertising data error %d, is the name too long?", rc);
        return;
    }

    static const ble_uuid16_t uuids[] = {
        BLE_UUID16_INIT(EOM_SVC_UUID),
        BLE_UUID16_INIT(MONSTERPUB_SVC_UUID),
    };
    struct ble_hs_adv_fields rsp = {
        .uuids16 = uuids,
        .num_uuids16 = 2,
        .uuids16_is_complete = 1,
    };
    ble_gap_adv_rsp_set_fields(&rsp);

    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min = BLE_GAP_ADV_ITVL_MS(100),
        .itvl_max = BLE_GAP_ADV_ITVL_MS(150),
    };
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) ESP_LOGE(TAG, "Advertising start error %d", rc);
}

static void on_sync(void) {
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &own_addr_type);
    start_advertising();
    ESP_LOGI(TAG, "Advertising as \"%s\"", ble_svc_gap_device_name());
}

static void on_reset(int reason) {
    ESP_LOGW(TAG, "Stack reset, reason %d", reason);
}

static void host_task(void* param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_init(void) {
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE init failed: %s", esp_err_to_name(err));
        return;
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(Config.bt_display_name);
    ble_att_set_preferred_mtu(BLE_ATT_MTU_MAX);

    if (ble_gatts_count_cfg(services) != 0 || ble_gatts_add_svcs(services) != 0) {
        ESP_LOGE(TAG, "Failed to register GATT services");
        return;
    }

    ble_store_config_init();
    nimble_port_freertos_init(host_task);
}
