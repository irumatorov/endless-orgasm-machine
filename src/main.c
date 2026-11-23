#include "ble.h"
#include "config.h"
#include "eom_hal.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "orgasm_control.h"
#include "web.h"
#include "wifi.h"
#include <dirent.h>

static const char* TAG = "main";

static void spiffs_init(void) {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "spiffs",
        .max_files = 8,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
        return;
    }

    size_t total = 0, used = 0;
    if (esp_spiffs_info(conf.partition_label, &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS mounted, %u of %u KiB used", (unsigned)(used / 1024), (unsigned)(total / 1024));
    }

    // List the files so a missing config.json or UI is obvious in the boot log.
    DIR* dir = opendir(conf.base_path);
    int count = 0;
    if (dir != NULL) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            ESP_LOGI(TAG, "  /spiffs/%s", entry->d_name);
            count++;
        }
        closedir(dir);
    }
    if (count == 0) {
        ESP_LOGW(TAG, "SPIFFS is empty, run Upload Filesystem Image to add config.json and the UI");
    }
}

static void nvs_init(void) {
    // NVS holds the Bluetooth bonding data and WiFi calibration.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void main_task(void* args) {
    for (;;) {
        orgasm_control_tick();
        eom_hal_led_tick();
        ble_tick();
        web_tick();
        config_tick();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void) {
    printf("Endless Orgasm Machine %s\n", EOM_VERSION);

    nvs_init();
    spiffs_init();
    config_init();
    eom_hal_led_init();
    orgasm_control_init();

    if (Config.bt_on) ble_init();
    if (Config.wifi_on) {
        wifi_init();
        web_init();
    } else {
        ESP_LOGI(TAG, "WiFi is off (wifi_on is false)");
    }

    xTaskCreate(main_task, "main", 6 * 1024, NULL, tskIDLE_PRIORITY + 2, NULL);
}
