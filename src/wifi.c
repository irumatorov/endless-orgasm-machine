#include "wifi.h"
#include "config.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "mdns.h"
#include <string.h>

#define CONNECT_TIMEOUT_MS 20000
#define MAX_RETRIES 5
#define CONNECTED_BIT BIT0
#define FAILED_BIT BIT1

static const char* TAG = "wifi";

static EventGroupHandle_t events;
static esp_netif_t* ap_netif = NULL;
static int retries = 0;
static bool giving_up = false;
static char ip[16] = "";

static void on_event(void* arg, esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();

    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ip[0] = '\0';
        if (giving_up) return;

        // Keep reconnecting forever once a connection has worked, otherwise fall back to the AP.
        if (retries++ < MAX_RETRIES || (xEventGroupGetBits(events) & CONNECTED_BIT)) {
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(events, FAILED_BIT);
        }

    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = data;
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&event->ip_info.ip));
        retries = 0;
        xEventGroupSetBits(events, CONNECTED_BIT);
        ESP_LOGI(TAG, "Connected, open http://%s/ or http://%s.local/", ip, Config.hostname);
    }
}

static bool join_network(void) {
    esp_netif_create_default_wifi_sta();

    wifi_config_t cfg = {
        .sta = {
            .threshold.authmode = Config.wifi_key[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN,
            .pmf_cfg.capable = true,
        },
    };
    strlcpy((char*)cfg.sta.ssid, Config.wifi_ssid, sizeof(cfg.sta.ssid));
    strlcpy((char*)cfg.sta.password, Config.wifi_key, sizeof(cfg.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Joining \"%s\"", Config.wifi_ssid);

    EventBits_t bits = xEventGroupWaitBits(
        events, CONNECTED_BIT | FAILED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(CONNECT_TIMEOUT_MS)
    );
    if (bits & CONNECTED_BIT) return true;

    ESP_LOGW(TAG, "Could not join \"%s\"", Config.wifi_ssid);
    giving_up = true;
    esp_wifi_stop();
    return false;
}

static void start_access_point(void) {
    if (ap_netif == NULL) ap_netif = esp_netif_create_default_wifi_ap();

    wifi_config_t cfg = {
        .ap = {
            .channel = Config.ap_channel,
            .max_connection = Config.ap_max_connections,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    strlcpy((char*)cfg.ap.ssid, Config.ap_ssid, sizeof(cfg.ap.ssid));
    cfg.ap.ssid_len = strlen((char*)cfg.ap.ssid);

    if (strlen(Config.ap_password) >= 8) {
        strlcpy((char*)cfg.ap.password, Config.ap_password, sizeof(cfg.ap.password));
        cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else if (Config.ap_password[0]) {
        ESP_LOGW(TAG, "ap_password is shorter than 8 characters, starting an open network");
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_netif_ip_info_t info;
    esp_netif_get_ip_info(ap_netif, &info);
    snprintf(ip, sizeof(ip), IPSTR, IP2STR(&info.ip));
    ESP_LOGI(TAG, "Access point \"%s\" started, open http://%s/", Config.ap_ssid, ip);
}

static void start_mdns(void) {
    if (Config.hostname[0] == '\0' || mdns_init() != ESP_OK) return;

    mdns_hostname_set(Config.hostname);
    mdns_instance_name_set(Config.bt_display_name);
    mdns_service_add(NULL, "_http", "_tcp", Config.websocket_port, NULL, 0);
}

void wifi_init(void) {
    events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));

    bool joined = !Config.wifi_ap_mode && Config.wifi_ssid[0] && join_network();
    if (!joined) start_access_point();

    start_mdns();
}

const char* wifi_get_ip(void) {
    return ip;
}
