/**
 * HTTP and WebSocket server. GET / serves the UI, a WebSocket upgrade on / opens the API.
 * Static files come from /spiffs/ui, preferring a gzipped copy (name.gz) when present.
 *
 * API: the client sends a JSON object of {"command": argument, ...}. Each command's result,
 * if any, is returned under the same key. See doc/WebSocket.md.
 */
#include "web.h"
#include "cJSON.h"
#include "config.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "orgasm_control.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define UI_DIR "/spiffs/ui"
#define READINGS_INTERVAL_MS 66
#define MAX_FRAME_LEN 4096
#define MAX_CLIENTS 7

static const char* TAG = "web";

static httpd_handle_t server = NULL;

// Session context of a WebSocket client.
typedef struct {
    bool streaming;
} ws_client_t;

//=== Static files

static const char* mime_type(const char* path) {
    const char* ext = strrchr(path, '.');
    if (ext == NULL) return "application/octet-stream";
    if (!strcmp(ext, ".html")) return "text/html";
    if (!strcmp(ext, ".js")) return "text/javascript";
    if (!strcmp(ext, ".css")) return "text/css";
    if (!strcmp(ext, ".svg")) return "image/svg+xml";
    if (!strcmp(ext, ".png")) return "image/png";
    if (!strcmp(ext, ".webmanifest")) return "application/manifest+json";
    if (!strcmp(ext, ".txt")) return "text/plain";
    return "application/octet-stream";
}

static esp_err_t send_missing_ui(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(
        req,
        "<h1>Endless Orgasm Machine</h1><p>The UI is not installed. Build it with "
        "<code>npm run build</code> in eomui, then run PlatformIO's "
        "<i>Upload Filesystem Image</i>.</p>"
    );
}

static esp_err_t serve_file(httpd_req_t* req) {
    char uri[64];
    strlcpy(uri, req->uri, sizeof(uri));
    char* query = strchr(uri, '?');
    if (query) *query = '\0';
    if (strstr(uri, "..")) return httpd_resp_send_404(req);
    if (!strcmp(uri, "/")) strcpy(uri, "/index.html");

    char path[96];
    struct stat st;
    bool gzipped = true;
    snprintf(path, sizeof(path), UI_DIR "%s.gz", uri);
    if (stat(path, &st) != 0) {
        gzipped = false;
        snprintf(path, sizeof(path), UI_DIR "%s", uri);
        if (stat(path, &st) != 0) {
            if (!strcmp(uri, "/index.html")) return send_missing_ui(req);
            return httpd_resp_send_404(req);
        }
    }

    FILE* f = fopen(path, "r");
    if (f == NULL) return httpd_resp_send_500(req);

    httpd_resp_set_type(req, mime_type(uri));
    if (gzipped) httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    char* chunk = malloc(4096);
    esp_err_t err = chunk ? ESP_OK : ESP_ERR_NO_MEM;
    size_t n;
    while (err == ESP_OK && (n = fread(chunk, 1, 4096, f)) > 0) {
        err = httpd_resp_send_chunk(req, chunk, n);
    }
    free(chunk);
    fclose(f);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Sending %s failed", path);
        return err;
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

//=== API

static void add_readings(cJSON* root) {
    cJSON_AddNumberToObject(root, "arousal", orgasm_control_get_arousal());
    cJSON_AddNumberToObject(root, "cooldown", orgasm_control_get_cooldown());
    cJSON_AddNumberToObject(root, "denied", orgasm_control_get_denied_count());
    cJSON_AddNumberToObject(root, "millis", esp_timer_get_time() / 1000);
    cJSON_AddNumberToObject(root, "motor", orgasm_control_get_pleasure());
    cJSON_AddNumberToObject(root, "pavg", orgasm_control_get_average_pressure());
    cJSON_AddNumberToObject(root, "permit", orgasm_control_get_permit_orgasm_remaining_seconds());
    cJSON_AddNumberToObject(root, "pressure", orgasm_control_get_last_pressure());
    cJSON_AddStringToObject(root, "runMode", orgasm_control_get_output_mode_str());
    cJSON_AddNumberToObject(root, "threshold", Config.sensitivity_threshold);
}

// Runs one command. Anything added to response is sent back under the command's name.
static void run_command(const cJSON* cmd, cJSON* response, httpd_req_t* req) {
    const char* name = cmd->string;

    if (!strcmp(name, "configList")) {
        config_to_json(response, false);

    } else if (!strcmp(name, "configSet")) {
        if (config_merge_json(cmd) > 0) config_enqueue_save();

    } else if (!strcmp(name, "setMode")) {
        orgasm_output_mode_t mode = orgasm_control_str_to_output_mode(cJSON_GetStringValue(cmd));
        if (mode == _OC_MODE_ERROR) {
            cJSON_AddStringToObject(response, "error", "unknown mode");
        } else {
            orgasm_control_set_output_mode(mode);
        }

    } else if (!strcmp(name, "setMotor")) {
        if (orgasm_control_get_output_mode() != OC_MANUAL) {
            orgasm_control_set_output_mode(OC_MANUAL);
            cJSON_AddStringToObject(response, "mode", orgasm_control_get_output_mode_str());
        }
        orgasm_control_set_manual_pleasure(cJSON_IsNumber(cmd) ? cmd->valueint : 0);

    } else if (!strcmp(name, "resetDenied")) {
        orgasm_control_reset_denied();

    } else if (!strcmp(name, "triggerArousal")) {
        orgasm_control_trigger_arousal();

    } else if (!strcmp(name, "streamReadings")) {
        if (req->sess_ctx == NULL) {
            req->sess_ctx = calloc(1, sizeof(ws_client_t));
            req->free_ctx = free;
        }
        if (req->sess_ctx) ((ws_client_t*)req->sess_ctx)->streaming = !cJSON_IsFalse(cmd);

    } else if (!strcmp(name, "info")) {
        cJSON_AddStringToObject(response, "device", "Endless Orgasm Machine");
        cJSON_AddStringToObject(response, "fwVersion", EOM_VERSION);

    } else if (!strcmp(name, "restart")) {
        esp_restart();

    } else {
        cJSON_AddStringToObject(response, "error", "unknown command");
    }
}

static esp_err_t handle_frame(httpd_req_t* req) {
    httpd_ws_frame_t frame = { .type = HTTPD_WS_TYPE_TEXT };
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) return err;
    if (frame.type != HTTPD_WS_TYPE_TEXT || frame.len == 0) return ESP_OK;
    if (frame.len > MAX_FRAME_LEN) return ESP_ERR_INVALID_SIZE;

    char* text = calloc(1, frame.len + 1);
    if (text == NULL) return ESP_ERR_NO_MEM;
    frame.payload = (uint8_t*)text;
    err = httpd_ws_recv_frame(req, &frame, frame.len);

    cJSON* commands = err == ESP_OK ? cJSON_Parse(text) : NULL;
    free(text);
    if (err != ESP_OK) return err;

    cJSON* response = cJSON_CreateObject();
    if (cJSON_IsObject(commands)) {
        const cJSON* cmd = NULL;
        cJSON_ArrayForEach(cmd, commands) {
            cJSON* result = cJSON_CreateObject();
            run_command(cmd, result, req);
            if (cJSON_GetArraySize(result) > 0) {
                cJSON_AddItemToObject(response, cmd->string, result);
            } else {
                cJSON_Delete(result);
            }
        }
    } else {
        cJSON_AddStringToObject(response, "error", "expected a JSON object");
    }
    cJSON_Delete(commands);

    if (cJSON_GetArraySize(response) > 0) {
        char* json = cJSON_PrintUnformatted(response);
        if (json) {
            httpd_ws_frame_t reply = {
                .type = HTTPD_WS_TYPE_TEXT,
                .final = true,
                .payload = (uint8_t*)json,
                .len = strlen(json),
            };
            err = httpd_ws_send_frame(req, &reply);
            cJSON_free(json);
        }
    }
    cJSON_Delete(response);
    return err;
}

// The same URI serves plain GETs (the UI) and WebSocket frames (the API).
static esp_err_t root_handler(httpd_req_t* req) {
    if (httpd_ws_get_fd_info(req->handle, httpd_req_to_sockfd(req)) == HTTPD_WS_CLIENT_WEBSOCKET) {
        return handle_frame(req);
    }
    return serve_file(req);
}

//=== Readings broadcast

// Runs on the server task, so the client list can't change underneath it.
static void broadcast_work(void* arg) {
    char* json = arg;
    size_t count = MAX_CLIENTS;
    int fds[MAX_CLIENTS];

    if (httpd_get_client_list(server, &count, fds) == ESP_OK) {
        httpd_ws_frame_t frame = {
            .type = HTTPD_WS_TYPE_TEXT,
            .final = true,
            .payload = (uint8_t*)json,
            .len = strlen(json),
        };
        for (size_t i = 0; i < count; i++) {
            if (httpd_ws_get_fd_info(server, fds[i]) != HTTPD_WS_CLIENT_WEBSOCKET) continue;
            ws_client_t* client = httpd_sess_get_ctx(server, fds[i]);
            if (client && client->streaming) httpd_ws_send_frame_async(server, fds[i], &frame);
        }
    }
    cJSON_free(json);
}

void web_tick(void) {
    static uint32_t last = 0;
    uint32_t now = esp_timer_get_time() / 1000;
    if (server == NULL || now - last < READINGS_INTERVAL_MS) return;
    last = now;

    cJSON* root = cJSON_CreateObject();
    add_readings(cJSON_AddObjectToObject(root, "readings"));
    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json && httpd_queue_work(server, broadcast_work, json) != ESP_OK) cJSON_free(json);
}

void web_init(void) {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = Config.websocket_port;
    cfg.max_open_sockets = MAX_CLIENTS;
    cfg.lru_purge_enable = true;
    cfg.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Server start failed: %s", esp_err_to_name(err));
        server = NULL;
        return;
    }

    static const httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .is_websocket = true,
    };
    static const httpd_uri_t files = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = serve_file,
    };
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &files);
    ESP_LOGI(TAG, "Listening on port %d", cfg.server_port);
}
