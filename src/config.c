#include "config.h"
#include "eom_hal.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define CONFIG_BACKUP CONFIG_FILENAME ".bak"
#define SAVE_DELAY_MS 1000

static const char* TAG = "config";

config_t Config;

typedef enum { CFG_BOOL, CFG_NUM, CFG_STR, CFG_SECRET } cfg_type_t;

typedef struct {
    const char* key;
    cfg_type_t type;
    uint16_t offset;
    uint16_t size;
    int32_t def;
    int32_t min;
    int32_t max;
    const char* def_str;
} cfg_def_t;

#define FIELD(name) offsetof(config_t, name), sizeof(((config_t*)0)->name)
#define BOOL(name, def) { #name, CFG_BOOL, FIELD(name), def, 0, 1, NULL }
#define NUM(name, def, min, max) { #name, CFG_NUM, FIELD(name), def, min, max, NULL }
#define STR(name, def) { #name, CFG_STR, FIELD(name), 0, 0, 0, def }
#define SECRET(name, def) { #name, CFG_SECRET, FIELD(name), 0, 0, 0, def }

// Every persistent option: JSON key, default and allowed range.
static const cfg_def_t defs[] = {
    STR(wifi_ssid, ""),
    SECRET(wifi_key, ""),
    BOOL(wifi_on, false),
    BOOL(wifi_ap_mode, true),
    STR(ap_ssid, "EOM"),
    SECRET(ap_password, ""),
    NUM(ap_channel, 1, 1, 13),
    NUM(ap_max_connections, 4, 1, 10),
    STR(hostname, "eom"),
    NUM(websocket_port, 80, 1, 65535),

    STR(bt_display_name, "Libotoy"),
    BOOL(bt_on, true),

    NUM(chart_window_s, 5, 1, 120),
    BOOL(classic_serial, false),

    NUM(max_denied, 20, 0, 100),
    NUM(max_pleasure, 255, 0, 255),
    NUM(initial_pleasure, 10, 0, 255),
    NUM(edge_delay, 10, 0, 255),
    NUM(max_additional_delay, 10, 0, 60),
    NUM(minimum_on_time, 1, 0, 60),
    NUM(pressure_smoothing, 10, 1, 100),
    NUM(sensitivity_threshold, 1024, 10, EOM_PRESSURE_MAX),
    NUM(mid_threshold, 512, 0, EOM_PRESSURE_MAX),
    NUM(motor_ramp_time_s, 120, 1, 255),
    NUM(update_frequency_hz, 50, 1, 100),
    NUM(sensor_sensitivity, 50, 0, 100),
    BOOL(use_average_values, true),
    NUM(vibration_mode, Depletion, RampStop, Enhancement),

    NUM(clench_pressure_sensitivity, 300, 0, EOM_PRESSURE_MAX),
    NUM(max_clench_duration_ms, 3000, 0, 60000),
    NUM(clench_time_to_orgasm_ms, 1500, 0, 60000),
    NUM(clench_time_threshold_ms, 900, 0, 60000),
    BOOL(clench_detector_in_edging, false),
    NUM(auto_edging_duration_minutes, 30, 0, 255),
    NUM(post_orgasm_duration_seconds, 10, 0, 3600),
};

#define DEF_COUNT (sizeof(defs) / sizeof(defs[0]))

static volatile bool save_pending = false;
static volatile uint32_t save_due_ms = 0;

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void* field_ptr(const cfg_def_t* def) {
    return (uint8_t*)&Config + def->offset;
}

static int32_t get_num(const cfg_def_t* def) {
    void* p = field_ptr(def);
    if (def->type == CFG_BOOL) return *(bool*)p;
    if (def->size == 1) return *(uint8_t*)p;
    return *(uint16_t*)p;
}

static void set_num(const cfg_def_t* def, int32_t value) {
    void* p = field_ptr(def);
    if (value < def->min) value = def->min;
    if (value > def->max) value = def->max;

    if (def->type == CFG_BOOL)
        *(bool*)p = value != 0;
    else if (def->size == 1)
        *(uint8_t*)p = value;
    else
        *(uint16_t*)p = value;
}

static void set_str(const cfg_def_t* def, const char* value) {
    strlcpy(field_ptr(def), value, def->size);
}

static bool is_str(const cfg_def_t* def) {
    return def->type == CFG_STR || def->type == CFG_SECRET;
}

// "false", "no", "off" and "0" are false, anything else is true.
static bool str_to_bool(const char* s) {
    return !(
        !strcasecmp(s, "false") || !strcasecmp(s, "no") || !strcasecmp(s, "off") || !strcmp(s, "0")
    );
}

static void set_from_string(const cfg_def_t* def, const char* value) {
    if (is_str(def))
        set_str(def, value);
    else if (def->type == CFG_BOOL)
        set_num(def, str_to_bool(value));
    else
        set_num(def, atoi(value));
}

static void load_defaults(void) {
    memset(&Config, 0, sizeof(Config));
    for (size_t i = 0; i < DEF_COUNT; i++) {
        if (is_str(&defs[i]))
            set_str(&defs[i], defs[i].def_str);
        else
            set_num(&defs[i], defs[i].def);
    }
}

static const cfg_def_t* find_def(const char* key) {
    for (size_t i = 0; i < DEF_COUNT; i++) {
        if (!strcasecmp(key, defs[i].key)) return &defs[i];
    }
    return NULL;
}

// Pushes settings that live in hardware out to it.
static void apply_config(void) {
    eom_hal_set_sensor_sensitivity(Config.sensor_sensitivity);
}

void config_to_json(cJSON* root, bool include_secrets) {
    for (size_t i = 0; i < DEF_COUNT; i++) {
        const cfg_def_t* def = &defs[i];
        if (def->type == CFG_SECRET && !include_secrets) continue;

        if (is_str(def))
            cJSON_AddStringToObject(root, def->key, field_ptr(def));
        else if (def->type == CFG_BOOL)
            cJSON_AddBoolToObject(root, def->key, get_num(def));
        else
            cJSON_AddNumberToObject(root, def->key, get_num(def));
    }
}

int config_merge_json(const cJSON* root) {
    int applied = 0;
    const cJSON* item = NULL;

    cJSON_ArrayForEach(item, root) {
        const cfg_def_t* def = item->string ? find_def(item->string) : NULL;
        if (def == NULL) continue;

        if (cJSON_IsString(item)) {
            set_from_string(def, item->valuestring);
        } else if (is_str(def)) {
            continue;
        } else if (cJSON_IsBool(item)) {
            set_num(def, cJSON_IsTrue(item));
        } else if (cJSON_IsNumber(item)) {
            set_num(def, (int32_t)item->valuedouble);
        } else {
            continue;
        }
        applied++;
    }

    if (applied > 0) apply_config();
    return applied;
}

bool config_set_value(const char* key, const char* value) {
    const cfg_def_t* def = find_def(key);
    if (def == NULL || value == NULL) return false;

    set_from_string(def, value);
    apply_config();
    return true;
}

static bool load_file(const char* path) {
    FILE* f = fopen(path, "r");
    if (f == NULL) return false;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    char* buffer = size > 0 ? malloc(size + 1) : NULL;
    cJSON* root = NULL;

    if (buffer != NULL) {
        buffer[fread(buffer, 1, size, f)] = '\0';
        root = cJSON_Parse(buffer);
        free(buffer);
    }
    fclose(f);

    if (!cJSON_IsObject(root)) {
        ESP_LOGW(TAG, "%s is not valid JSON", path);
        cJSON_Delete(root);
        return false;
    }

    int applied = config_merge_json(root);
    cJSON_Delete(root);
    ESP_LOGI(TAG, "Loaded %d options from %s", applied, path);
    return true;
}

void config_init(void) {
    load_defaults();

    if (!load_file(CONFIG_FILENAME) && !load_file(CONFIG_BACKUP)) {
        ESP_LOGW(TAG, "No usable config file, using defaults");
    }
}

static bool save_file(void) {
    cJSON* root = cJSON_CreateObject();
    config_to_json(root, true);
    char* json = cJSON_Print(root);
    cJSON_Delete(root);
    if (json == NULL) return false;

    // Keep the previous file until the new one is written.
    struct stat st;
    if (stat(CONFIG_FILENAME, &st) == 0) {
        unlink(CONFIG_BACKUP);
        rename(CONFIG_FILENAME, CONFIG_BACKUP);
    }

    bool ok = false;
    FILE* f = fopen(CONFIG_FILENAME, "w");
    if (f != NULL) {
        ok = fputs(json, f) >= 0;
        ok = fclose(f) == 0 && ok;
    }
    cJSON_free(json);

    if (ok)
        ESP_LOGI(TAG, "Saved %s", CONFIG_FILENAME);
    else
        ESP_LOGE(TAG, "Failed to write %s", CONFIG_FILENAME);
    return ok;
}

void config_enqueue_save(void) {
    save_due_ms = now_ms() + SAVE_DELAY_MS;
    save_pending = true;
}

bool config_tick(void) {
    if (!save_pending || (int32_t)(now_ms() - save_due_ms) < 0) return false;

    save_pending = false;
    return save_file();
}
