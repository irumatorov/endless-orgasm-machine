#include "orgasm_control.h"
#include "ble.h"
#include "config.h"
#include "eom_hal.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "running_average.h"
#include "vibration_mode_controller.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

// Fastest LED flash, used while aroused past the threshold.
#define MINIMUM_FLASH_MS 40

static const char* TAG = "orgasm_control";

static const char* output_mode_str[] = {
    "MANUAL",
    "ENDLESS",
    "ORGASM",
};

static struct {
    uint32_t last_update_ms;
    running_average_t* average;
    uint16_t last_value;
    uint16_t pressure_value;
    uint16_t peak_start;
    uint16_t arousal;
    uint32_t cooldown_ms;
} arousal_state;

static struct {
    orgasm_output_mode_t output_mode;
    uint32_t motor_stop_time;
    uint32_t motor_start_time;
    uint32_t random_additional_delay;
    float pleasure;
    uint8_t denied_count;
} output_state;

static struct {
    long clench_pressure_threshold;
    long clench_duration_millis;
    uint32_t clench_start_millis;
    bool clench_counted;
    uint32_t auto_edging_start_millis;
    uint32_t post_orgasm_start_millis;
    bool detected_orgasm;
} post_orgasm_state;

static uint32_t millis(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static uint32_t edge_delay_ms(void) {
    return Config.edge_delay * 1000 + output_state.random_additional_delay;
}

static uint32_t auto_edging_duration_ms(void) {
    return Config.auto_edging_duration_minutes * 60 * 1000;
}

static const vibration_mode_controller_t* get_vibration_mode_controller(void) {
    switch (Config.vibration_mode) {
    case RampStop: return &RampStopController;
    case Enhancement: return &EnhancementController;
    default: return &DepletionController;
    }
}

static bool is_post_orgasm_reached(void) {
    return post_orgasm_state.post_orgasm_start_millis > 0;
}

void orgasm_control_init(void) {
    eom_hal_init_pressure_sensor();

    output_state.output_mode = OC_AUTOMATIC;
    post_orgasm_state.clench_pressure_threshold = EOM_PRESSURE_MAX + 1;
    arousal_state.average = running_average_create(Config.pressure_smoothing);
}

/**
 * Detects pelvic muscle clenches, which raise arousal while edging (if enabled) and
 * signal the start of an orgasm once one is permitted.
 * @return how long the current clench has lasted, in ms
 */
static long clench_detect(long p_check) {
    uint32_t now = millis();

    // Raise the clench threshold to pressure - 1/2 sensitivity
    if (p_check >=
        post_orgasm_state.clench_pressure_threshold + Config.clench_pressure_sensitivity) {
        post_orgasm_state.clench_pressure_threshold =
            p_check - Config.clench_pressure_sensitivity / 2;
    }

    if (p_check >= post_orgasm_state.clench_pressure_threshold) {
        post_orgasm_state.clench_duration_millis = now - post_orgasm_state.clench_start_millis;

        // A long enough clench is an orgasm
        if (post_orgasm_state.clench_duration_millis >= Config.clench_time_to_orgasm_ms &&
            !post_orgasm_state.clench_counted) {
            post_orgasm_state.clench_counted = true;
            if (orgasm_control_is_permit_orgasm_reached()) {
                post_orgasm_state.detected_orgasm = true;
            }
        }
        return post_orgasm_state.clench_duration_millis;
    }

    post_orgasm_state.clench_counted = false;
    post_orgasm_state.clench_start_millis = now;
    post_orgasm_state.clench_duration_millis -= 150;
    if (post_orgasm_state.clench_duration_millis <= 0) {
        post_orgasm_state.clench_duration_millis = 0;
        // The threshold decays over time to a minimum of pressure + 1/2 sensitivity
        if (p_check + Config.clench_pressure_sensitivity / 2 <
            post_orgasm_state.clench_pressure_threshold) {
            post_orgasm_state.clench_pressure_threshold *= 0.99;
        }
    }
    return 0;
}

/**
 * The edge detection algorithm: arousal is the decaying sum of pressure peaks.
 */
static void update_arousal(void) {
    uint32_t arousal = arousal_state.arousal * 0.99;

    arousal_state.pressure_value = eom_hal_get_pressure_reading();
    running_average_add_value(arousal_state.average, arousal_state.pressure_value);
    uint16_t p_check = Config.use_average_values
                           ? running_average_get_average(arousal_state.average)
                           : arousal_state.pressure_value;

    if (p_check < arousal_state.last_value) { // falling edge of a peak
        uint16_t peak = arousal_state.last_value;
        if (peak > arousal_state.peak_start &&
            peak - arousal_state.peak_start >= Config.sensitivity_threshold / 10) {
            arousal += peak - arousal_state.peak_start;
            arousal_state.peak_start = p_check;
        }

        // Run this value down to a new minimum after a peak.
        if (p_check < arousal_state.peak_start) arousal_state.peak_start = p_check;
    }
    arousal_state.last_value = p_check;

    long clench_duration = clench_detect(p_check);
    if (Config.clench_detector_in_edging && clench_duration > Config.clench_time_threshold_ms &&
        clench_duration < Config.max_clench_duration_ms) {
        arousal += 5;
    }

    arousal_state.arousal = arousal > EOM_PRESSURE_MAX ? EOM_PRESSURE_MAX : arousal;
}

/**
 * While cooling down after an edge: restart the cooldown for as long as arousal stays
 * over the threshold, otherwise fade the LED out with the time remaining.
 */
static void twitch_detect(void) {
    if (arousal_state.arousal > Config.sensitivity_threshold) {
        output_state.motor_stop_time = millis();
        arousal_state.cooldown_ms = edge_delay_ms();
        eom_hal_set_led_flash_interval(MINIMUM_FLASH_MS);
        eom_hal_set_led_flashing(true);
        eom_hal_set_rgb(rgb_red);
    } else {
        float remaining = (float)arousal_state.cooldown_ms / edge_delay_ms();
        eom_hal_set_led_flash_interval(1000 - remaining * 1000);
        eom_hal_set_rgb(eom_hal_fade_color(rgb_off, rgb_red, remaining));
    }
}

static void show_arousal_on_led(void) {
    uint16_t arousal = arousal_state.arousal;
    uint16_t mid = Config.mid_threshold;
    uint16_t threshold = Config.sensitivity_threshold;

    eom_hal_set_led_flash_interval(
        fmaxf(MINIMUM_FLASH_MS, 1000.0f - (float)arousal / threshold * 1000.0f)
    );

    RGBColor calm = ble_is_connected() ? rgb_green : rgb_purple;
    if (arousal < mid) {
        eom_hal_set_rgb(eom_hal_fade_color(calm, rgb_orange, (float)arousal / mid));
    } else if (threshold > mid) {
        eom_hal_set_rgb(
            eom_hal_fade_color(rgb_orange, rgb_red, (float)(arousal - mid) / (threshold - mid))
        );
    } else {
        eom_hal_set_rgb(rgb_red);
    }
}

static void update_pleasure(void) {
    const vibration_mode_controller_t* controller = get_vibration_mode_controller();
    controller->tick(output_state.pleasure, arousal_state.arousal);

    uint32_t now = millis();
    uint32_t on_time = now - output_state.motor_start_time;
    uint32_t since_stop = now - output_state.motor_stop_time;
    arousal_state.cooldown_ms = since_stop > edge_delay_ms() ? 0 : edge_delay_ms() - since_stop;

    if (arousal_state.cooldown_ms > 0) {
        // It hasn't been long enough since the pleasure stopped
        twitch_detect();

    } else if (arousal_state.arousal > Config.sensitivity_threshold && output_state.pleasure > 0 &&
               on_time > Config.minimum_on_time * 1000) {
        // Edge detected
        if (!orgasm_control_is_permit_orgasm_reached()) {
            output_state.pleasure = controller->stop();
            output_state.motor_stop_time = now;
            output_state.motor_start_time = 0;
            arousal_state.cooldown_ms = edge_delay_ms();
            if (output_state.denied_count < UINT8_MAX) output_state.denied_count++;
            ESP_LOGI(TAG, "Orgasm denied, total: %d", output_state.denied_count);

            // Pick the random extra delay for this cooldown
            if (Config.max_additional_delay != 0) {
                output_state.random_additional_delay =
                    random() % (Config.max_additional_delay * 1000);
            }
        }

        eom_hal_set_rgb(rgb_red);
        eom_hal_set_led_flashing(true);

    } else if (output_state.pleasure == 0 && output_state.motor_start_time == 0) {
        // The cooldown is over and pleasure is stopped: start again
        output_state.pleasure = controller->start();
        output_state.motor_start_time = now;
        output_state.random_additional_delay = 0;
        ESP_LOGI(TAG, "Starting cycle %d", output_state.denied_count + 1);

        if (LED_TYPE == LED_TYPE_MONO) {
            eom_hal_set_led_flash_interval(2000);
        } else {
            eom_hal_set_led_flashing(false);
        }
        eom_hal_set_rgb(ble_is_connected() ? rgb_green : rgb_purple);

    } else {
        output_state.pleasure = controller->increment();
        show_arousal_on_led();
    }
}

// Orgasm mode: once edging time is up let pleasure run to the maximum, keep it there for a
// while after the orgasm is detected, then ramp down and hand control back to the user.
static void update_edging_time(void) {
    if (output_state.output_mode == OC_MANUAL) return;

    uint32_t now = millis();

    // The edging timer only runs in orgasm mode
    if (output_state.output_mode != OC_ORGASM) {
        post_orgasm_state.auto_edging_start_millis = now;
        post_orgasm_state.post_orgasm_start_millis = 0;
    }

    if (orgasm_control_is_permit_orgasm_reached() && !is_post_orgasm_reached()) {
        if (post_orgasm_state.detected_orgasm) {
            post_orgasm_state.post_orgasm_start_millis = now;
        }

        if (output_state.pleasure < Config.max_pleasure) {
            output_state.pleasure = get_vibration_mode_controller()->increment();
        } else {
            output_state.pleasure = Config.max_pleasure;
        }
    }

    if (is_post_orgasm_reached()) {
        uint32_t since_orgasm = now - post_orgasm_state.post_orgasm_start_millis;

        if (since_orgasm < Config.post_orgasm_duration_seconds * 1000) {
            output_state.pleasure = Config.max_pleasure;
        } else if (output_state.pleasure > 1) {
            output_state.pleasure -= 1;
        } else {
            output_state.pleasure = 0;
            arousal_state.cooldown_ms = 0;
            orgasm_control_set_output_mode(OC_MANUAL);
        }
    }
}

void orgasm_control_tick(void) {
    uint32_t now = millis();
    if (now - arousal_state.last_update_ms < 1000 / Config.update_frequency_hz) return;
    arousal_state.last_update_ms = now;

    // Apply a changed smoothing window
    if (arousal_state.average == NULL ||
        arousal_state.average->window != Config.pressure_smoothing) {
        running_average_t* resized = running_average_create(Config.pressure_smoothing);
        if (resized != NULL) {
            running_average_dispose(arousal_state.average);
            arousal_state.average = resized;
        }
    }

    update_arousal();
    update_edging_time();

    if (output_state.output_mode == OC_MANUAL) {
        eom_hal_set_led_flashing(LED_TYPE == LED_TYPE_MONO);
        eom_hal_set_rgb(ble_is_connected() ? rgb_green : rgb_purple);
    } else {
        update_pleasure();
    }

    if (Config.classic_serial) {
        printf(
            "%d,%d,%d,%d,%ld,%ld,%d\n",
            orgasm_control_get_average_pressure(),
            arousal_state.arousal,
            orgasm_control_get_pleasure(),
            Config.sensitivity_threshold,
            post_orgasm_state.clench_pressure_threshold,
            post_orgasm_state.clench_duration_millis,
            output_state.denied_count
        );
    }
}

//=== Readings

uint16_t orgasm_control_get_arousal(void) {
    return arousal_state.arousal;
}

uint8_t orgasm_control_get_pleasure(void) {
    float pleasure = output_state.pleasure;
    if (pleasure > Config.max_pleasure) return Config.max_pleasure;
    if (pleasure < 0) return 0;
    return (uint8_t)pleasure;
}

uint16_t orgasm_control_get_last_pressure(void) {
    return arousal_state.pressure_value;
}

uint16_t orgasm_control_get_average_pressure(void) {
    return running_average_get_average(arousal_state.average);
}

uint8_t orgasm_control_get_denied_count(void) {
    return output_state.denied_count;
}

uint8_t orgasm_control_get_cooldown(void) {
    uint32_t seconds = arousal_state.cooldown_ms / 1000;
    return seconds > UINT8_MAX ? UINT8_MAX : seconds;
}

bool orgasm_control_is_permit_orgasm_reached(void) {
    if (output_state.output_mode != OC_ORGASM) return false;

    // Either the edging time has passed or the maximum number of denials was reached
    bool time_up =
        millis() - post_orgasm_state.auto_edging_start_millis > auto_edging_duration_ms();
    bool enough_denials = Config.max_denied > 0 && Config.max_denied < 100 &&
                          output_state.denied_count >= Config.max_denied;
    return time_up || enough_denials;
}

uint16_t orgasm_control_get_permit_orgasm_remaining_seconds(void) {
    if (output_state.output_mode != OC_ORGASM || orgasm_control_is_permit_orgasm_reached()) {
        return 0;
    }

    uint32_t elapsed = millis() - post_orgasm_state.auto_edging_start_millis;
    return (auto_edging_duration_ms() - elapsed) / 1000;
}

//=== Controls

orgasm_output_mode_t orgasm_control_get_output_mode(void) {
    return output_state.output_mode;
}

const char* orgasm_control_get_output_mode_str(void) {
    return output_mode_str[output_state.output_mode];
}

orgasm_output_mode_t orgasm_control_str_to_output_mode(const char* str) {
    for (int i = 0; str != NULL && i < _OC_MODE_MAX; i++) {
        if (!strcasecmp(str, output_mode_str[i])) return (orgasm_output_mode_t)i;
    }
    return _OC_MODE_ERROR;
}

void orgasm_control_set_output_mode(orgasm_output_mode_t mode) {
    if (mode < 0 || mode >= _OC_MODE_MAX) return;

    output_state.output_mode = mode;
    output_state.motor_stop_time = 0;
    post_orgasm_state.auto_edging_start_millis = millis();
    post_orgasm_state.post_orgasm_start_millis = 0;
    post_orgasm_state.detected_orgasm = false;
    if (mode == OC_ORGASM) output_state.denied_count = 0;
    ESP_LOGI(TAG, "Mode set to %s", output_mode_str[mode]);
}

void orgasm_control_set_manual_pleasure(uint8_t pleasure) {
    if (output_state.output_mode != OC_MANUAL) return;
    output_state.pleasure = pleasure > Config.max_pleasure ? Config.max_pleasure : pleasure;
}

void orgasm_control_reset_denied(void) {
    output_state.denied_count = 0;
}

void orgasm_control_trigger_arousal(void) {
    arousal_state.arousal = Config.sensitivity_threshold + 1;
    output_state.pleasure = 0;
    output_state.motor_stop_time = millis();
    output_state.motor_start_time = 0;
}
