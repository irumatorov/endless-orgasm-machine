#ifndef __config_h
#define __config_h

#include "cJSON.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EOM_VERSION "2.1.0"

//=== Board options. Set these per board with -D flags in platformio.ini.

// Pressure sensor type
#define PRESSURE_SENSOR_ANALOG 0 // analog sensor (MPX5100 / MPXV5100 family) on an ADC1 channel
#define PRESSURE_SENSOR_MPR 1    // Honeywell MPR series over I2C

#ifndef PRESSURE_SENSOR
#define PRESSURE_SENSOR PRESSURE_SENSOR_ANALOG
#endif

// ADC1 channel of the analog sensor. This is a channel, not a GPIO number:
// on the ESP32-S3 ADC1 channel N is GPIO N+1, on the ESP32-C6 it is GPIO N.
#ifndef PRESSURE_ADC_CHANNEL
#define PRESSURE_ADC_CHANNEL ADC_CHANNEL_0
#endif

// I2C pins for the MPR sensor
#ifndef PRESSURE_SDA
#define PRESSURE_SDA -1
#endif
#ifndef PRESSURE_SCL
#define PRESSURE_SCL -1
#endif

// Status LED
#define LED_TYPE_MONO 0
#define LED_TYPE_WS2812 1

#ifndef LED_GPIO
#define LED_GPIO -1 // no LED
#endif
#ifndef LED_TYPE
#define LED_TYPE LED_TYPE_MONO
#endif
#ifndef LED_POWER
#define LED_POWER -1 // GPIO that switches power to the WS2812, if the board has one
#endif
#ifndef LED_FORMAT
#define LED_FORMAT 2 // WS2812 colour order: 1 = RGB, 2 = GRB
#endif

// GPIO of a button that puts the device into light sleep (active low), -1 to disable
#ifndef SLEEP_GPIO
#define SLEEP_GPIO -1
#endif

// All pressure and arousal values are 12 bit.
#define EOM_PRESSURE_MAX 0x0FFF

//=== Runtime configuration, stored as JSON in /spiffs/config.json

#define CONFIG_FILENAME "/spiffs/config.json"

enum vibration_mode { RampStop = 1, Depletion = 2, Enhancement = 3 };

typedef struct config {
    //= Networking
    char wifi_ssid[33];
    char wifi_key[65];
    // Enable WiFi, the web UI and the WebSocket server.
    bool wifi_on;
    // Start an access point instead of joining wifi_ssid. Also the fallback if joining fails.
    bool wifi_ap_mode;
    char ap_ssid[33];
    // Leave empty for an open network, otherwise at least 8 characters.
    char ap_password[65];
    uint8_t ap_channel;
    uint8_t ap_max_connections;
    // mDNS name, the device answers on http://<hostname>.local
    char hostname[33];
    // Port of the web UI and WebSocket API.
    uint16_t websocket_port;

    char bt_display_name[32];
    bool bt_on;

    //= UI
    uint8_t chart_window_s;
    // Print "pressure,arousal,pleasure,threshold,..." CSV lines on the serial console.
    bool classic_serial;

    //= Edging
    // Number of denials before an orgasm is allowed in orgasm mode. 0 or 100 = no limit.
    uint8_t max_denied;
    uint8_t max_pleasure;
    uint8_t initial_pleasure;
    // Seconds to wait after an edge before pleasure resumes.
    uint8_t edge_delay;
    // Up to this many random extra seconds are added to edge_delay. 0 to disable.
    uint8_t max_additional_delay;
    // Seconds after pleasure resumes before edge detection is active again.
    uint8_t minimum_on_time;
    // Number of samples in the pressure running average.
    uint8_t pressure_smoothing;
    // Arousal level that counts as an edge. Lower = sooner cutoff.
    uint16_t sensitivity_threshold;
    // Start of the warning zone, only changes the LED colour.
    uint16_t mid_threshold;
    // Seconds to ramp from initial_pleasure to max_pleasure.
    uint8_t motor_ramp_time_s;
    uint8_t update_frequency_hz;
    // Sensor gain in percent. For analog sensors this selects the ADC attenuation.
    uint8_t sensor_sensitivity;
    bool use_average_values;
    uint8_t vibration_mode;

    //= Clench detection and orgasm mode
    uint16_t clench_pressure_sensitivity;
    uint16_t max_clench_duration_ms;
    uint16_t clench_time_to_orgasm_ms;
    uint16_t clench_time_threshold_ms;
    bool clench_detector_in_edging;
    uint8_t auto_edging_duration_minutes;
    uint16_t post_orgasm_duration_seconds;
} config_t;

extern config_t Config;

// Loads CONFIG_FILENAME, falling back to its backup and then to defaults.
void config_init(void);

// Serialises the config. Passwords are only included when include_secrets is set.
void config_to_json(cJSON* root, bool include_secrets);

// Applies every known key present in a JSON object. Returns the number of keys applied.
int config_merge_json(const cJSON* root);

// Sets one option from its string form. Returns false for an unknown key.
bool config_set_value(const char* key, const char* value);

// Schedules a save a short while from now, so bursts of changes cause one write.
void config_enqueue_save(void);

// Performs a scheduled save once it is due. Returns true if the config was written.
bool config_tick(void);

#endif
