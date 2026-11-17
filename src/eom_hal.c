#include "eom_hal.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if PRESSURE_SENSOR == PRESSURE_SENSOR_MPR
#include "mpr_pressure_sensor.h"
#else
#include "esp_adc/adc_oneshot.h"
#endif

#if LED_TYPE == LED_TYPE_WS2812
#include "led_strip.h"
#endif

static const char* TAG = "hal";

const RGBColor rgb_off = { 0, 0, 0 };
const RGBColor rgb_red = { 255, 0, 0 };
const RGBColor rgb_green = { 0, 255, 0 };
const RGBColor rgb_blue = { 0, 0, 255 };
const RGBColor rgb_orange = { 255, 130, 0 };
const RGBColor rgb_purple = { 80, 0, 150 };

//=== Pressure

#define AMBIENT_SAMPLES 16

// Sensor reading, in raw sensor units, that counts as zero pressure.
static int32_t pressure_ambient = 0;

#if PRESSURE_SENSOR == PRESSURE_SENSOR_MPR

// The MPR reports 24 bit counts. Scale the range of interest up before reducing to 12 bits.
#define MPR_GAIN 10

static int32_t read_raw(void) {
    int32_t counts = 0;
    esp_err_t err = mpr_read_pressure(&counts);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MPR read failed: %s", esp_err_to_name(err));
        return -1;
    }
    return counts;
}

static uint16_t raw_to_pressure(int32_t above_ambient) {
    int64_t scaled = ((int64_t)above_ambient * Config.sensor_sensitivity * MPR_GAIN / 100) >> 12;
    return scaled > EOM_PRESSURE_MAX ? EOM_PRESSURE_MAX : (uint16_t)scaled;
}

void eom_hal_set_sensor_sensitivity(uint8_t sensitivity) {
    // Applied in raw_to_pressure().
}

static void init_sensor(void) {
    ESP_ERROR_CHECK(mpr_init(PRESSURE_SDA, PRESSURE_SCL));
    ESP_LOGI(TAG, "MPR pressure sensor on SDA %d, SCL %d", PRESSURE_SDA, PRESSURE_SCL);
}

#else // analog sensor

// The ADC is noisy, so every reading is the mean of a short burst.
#define ADC_OVERSAMPLE 8

static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_atten_t adc_atten = ADC_ATTEN_DB_12;
// Requested from other tasks, applied before the next read.
static volatile adc_atten_t wanted_atten = ADC_ATTEN_DB_12;

// Approximate full scale input voltage for each attenuation, indexed by adc_atten_t.
static const uint16_t atten_full_scale_mv[] = { 950, 1250, 1750, 3100 };

// Higher sensitivity selects less attenuation, so the same pressure change spans more counts.
static adc_atten_t sensitivity_to_atten(uint8_t sensitivity) {
    if (sensitivity >= 75) return ADC_ATTEN_DB_0;
    if (sensitivity >= 50) return ADC_ATTEN_DB_2_5;
    if (sensitivity >= 25) return ADC_ATTEN_DB_6;
    return ADC_ATTEN_DB_12;
}

static void set_atten(adc_atten_t atten);

static int32_t read_raw(void) {
    int32_t sum = 0;
    int count = 0;

    if (wanted_atten != adc_atten) set_atten(wanted_atten);

    for (int i = 0; i < ADC_OVERSAMPLE; i++) {
        int raw = 0;
        if (adc_oneshot_read(adc_handle, PRESSURE_ADC_CHANNEL, &raw) == ESP_OK) {
            sum += raw;
            count++;
        }
    }

    if (count == 0) {
        ESP_LOGE(TAG, "ADC read failed");
        return -1;
    }
    return sum / count;
}

static uint16_t raw_to_pressure(int32_t above_ambient) {
    return above_ambient > EOM_PRESSURE_MAX ? EOM_PRESSURE_MAX : (uint16_t)above_ambient;
}

static void set_atten(adc_atten_t atten) {
    adc_oneshot_chan_cfg_t cfg = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = atten,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, PRESSURE_ADC_CHANNEL, &cfg));

    // Keep the zero point at the same voltage on the new scale.
    pressure_ambient =
        pressure_ambient * atten_full_scale_mv[adc_atten] / atten_full_scale_mv[atten];
    adc_atten = atten;
}

void eom_hal_set_sensor_sensitivity(uint8_t sensitivity) {
    wanted_atten = sensitivity_to_atten(sensitivity);
}

static void init_sensor(void) {
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));
    wanted_atten = sensitivity_to_atten(Config.sensor_sensitivity);
    set_atten(wanted_atten);

    int gpio = -1;
    adc_oneshot_channel_to_io(ADC_UNIT_1, PRESSURE_ADC_CHANNEL, &gpio);
    ESP_LOGI(
        TAG,
        "Analog pressure sensor on GPIO %d (ADC1 channel %d), about %d mV full scale",
        gpio,
        PRESSURE_ADC_CHANNEL,
        atten_full_scale_mv[adc_atten]
    );
}

#endif

void eom_hal_init_pressure_sensor(void) {
    init_sensor();

    // Whatever the sensor reads at startup is treated as zero pressure.
    int32_t sum = 0;
    int count = 0;
    for (int i = 0; i < AMBIENT_SAMPLES; i++) {
        int32_t raw = read_raw();
        if (raw >= 0) {
            sum += raw;
            count++;
        }
    }
    pressure_ambient = count > 0 ? sum / count : 0;
    ESP_LOGI(TAG, "Ambient pressure reading: %d", (int)pressure_ambient);
}

uint16_t eom_hal_get_pressure_reading(void) {
    int32_t raw = read_raw();
    if (raw < 0) return 0;

    // Follow the sensor down if it drifts or the plug is deflated.
    if (raw < pressure_ambient) pressure_ambient = raw;

    return raw_to_pressure(raw - pressure_ambient);
}

//=== Status LED

static RGBColor led_color = { 0, 0, 0 };
static volatile bool led_dirty = true;
static volatile bool led_flashing = false;
static volatile uint16_t flash_interval_ms = 250;

#if LED_GPIO >= 0

static bool flash_on = false;
static uint32_t last_flash_ms = 0;

#if LED_TYPE == LED_TYPE_WS2812
static led_strip_handle_t led_strip = NULL;
#endif

void eom_hal_led_init(void) {
#if LED_TYPE == LED_TYPE_WS2812
    if (LED_POWER >= 0) {
        gpio_reset_pin(LED_POWER);
        gpio_set_direction(LED_POWER, GPIO_MODE_OUTPUT);
        gpio_set_level(LED_POWER, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
#if LED_FORMAT == 1
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB,
#else
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
#endif
    };
    led_strip_rmt_config_t rmt_config = { .clk_src = RMT_CLK_SRC_DEFAULT };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));

    eom_hal_set_rgb(rgb_blue); // blue = starting up
#else
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    eom_hal_set_led_flash_interval(2000);
    eom_hal_set_led_flashing(true);
#endif
    ESP_LOGI(TAG, "Status LED type %d on GPIO %d", LED_TYPE, LED_GPIO);
}

void eom_hal_led_tick(void) {
    uint32_t now = esp_timer_get_time() / 1000;

    if (led_flashing && now - last_flash_ms >= flash_interval_ms) {
        flash_on = !flash_on;
        last_flash_ms = now;
        led_dirty = true;
    }

    if (!led_dirty) return;
    led_dirty = false;

    bool lit = !led_flashing || flash_on;

#if LED_TYPE == LED_TYPE_WS2812
    // The off phase of a flash is a dim red rather than fully dark.
    RGBColor c = lit ? led_color : (RGBColor) { 1, 0, 0 };
    led_strip_set_pixel(led_strip, 0, c.r, c.g, c.b);
    led_strip_refresh(led_strip);
#else
    gpio_set_level(LED_GPIO, lit);
#endif
}

#else // no LED

void eom_hal_led_init(void) {
}

void eom_hal_led_tick(void) {
}

#endif

void eom_hal_set_rgb(RGBColor color) {
    if (color.r == led_color.r && color.g == led_color.g && color.b == led_color.b) return;

    led_color = color;
    led_dirty = true;
}

RGBColor eom_hal_get_rgb(void) {
    return led_color;
}

void eom_hal_set_led_flashing(bool flashing) {
    if (flashing == led_flashing) return;

    led_flashing = flashing;
    led_dirty = true;
}

void eom_hal_set_led_flash_interval(uint16_t interval_ms) {
    flash_interval_ms = interval_ms;
}

RGBColor eom_hal_fade_color(RGBColor start, RGBColor end, float progress) {
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;

    RGBColor c = {
        .r = (uint8_t)(start.r + (end.r - start.r) * progress),
        .g = (uint8_t)(start.g + (end.g - start.g) * progress),
        .b = (uint8_t)(start.b + (end.b - start.b) * progress),
    };
    return c;
}
