#include "config.h"

#if PRESSURE_SENSOR == PRESSURE_SENSOR_MPR

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mpr_pressure_sensor.h"

#define MPR_READ_WAIT_MS 10
#define MPR_TIMEOUT_MS 200
#define MPR_READ_RETRIES 7
#define MPR_STATUS_POWER_ON 0x40
#define MPR_STATUS_BUSY 0x20
#define MPR_STATUS_INTEGRITY_FAILED 0x04
#define MPR_STATUS_MATH_SATURATION 0x01

static const char* TAG = "mpr";
static const uint8_t MPR_PRESSURE_REQ[3] = { 0xAA, 0x00, 0x00 };
static i2c_master_dev_handle_t dev_handle = NULL;

esp_err_t mpr_init(int sda_gpio, int scl_gpio) {
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = scl_gpio,
        .sda_io_num = sda_gpio,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus);
    if (err != ESP_OK) return err;

    if (i2c_master_probe(bus, MPR_I2C_ADDRESS, MPR_TIMEOUT_MS) != ESP_OK) {
        ESP_LOGW(TAG, "No sensor answered at address 0x%02X, check wiring", MPR_I2C_ADDRESS);
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MPR_I2C_ADDRESS,
        .scl_speed_hz = 100000,
    };
    return i2c_master_bus_add_device(bus, &dev_cfg, &dev_handle);
}

esp_err_t mpr_read_pressure(int32_t* pressure) {
    uint8_t data[4];
    esp_err_t err = ESP_FAIL;

    for (int attempt = 0; attempt < MPR_READ_RETRIES; attempt++) {
        err = i2c_master_transmit(
            dev_handle, MPR_PRESSURE_REQ, sizeof(MPR_PRESSURE_REQ), MPR_TIMEOUT_MS
        );
        if (err == ESP_ERR_INVALID_STATE) {
            // The bus may be stuck, give it time to recover.
            vTaskDelay(pdMS_TO_TICKS(MPR_READ_WAIT_MS));
            continue;
        }
        if (err != ESP_OK) return err;

        // Conversion takes about 5 ms.
        vTaskDelay(pdMS_TO_TICKS(MPR_READ_WAIT_MS));
        err = i2c_master_receive(dev_handle, data, sizeof(data), MPR_TIMEOUT_MS);
        if (err != ESP_OK) continue;

        uint8_t status = data[0];
        if (!(status & MPR_STATUS_POWER_ON)) return ESP_ERR_INVALID_STATE;
        if (status & (MPR_STATUS_INTEGRITY_FAILED | MPR_STATUS_MATH_SATURATION)) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        if (status & MPR_STATUS_BUSY) {
            err = ESP_ERR_TIMEOUT;
            vTaskDelay(pdMS_TO_TICKS(MPR_READ_WAIT_MS));
            continue;
        }

        *pressure = ((int32_t)data[1] << 16) | ((int32_t)data[2] << 8) | data[3];
        return ESP_OK;
    }

    return err == ESP_OK ? ESP_FAIL : err;
}

#endif
