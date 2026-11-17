#ifndef __mpr_pressure_sensor_h
#define __mpr_pressure_sensor_h

#include "esp_err.h"
#include <stdint.h>

// Honeywell MPR series I2C pressure sensor. The address depends on the part number.
#ifndef MPR_I2C_ADDRESS
#define MPR_I2C_ADDRESS 0x08
#endif

esp_err_t mpr_init(int sda_gpio, int scl_gpio);
// Reads the 24 bit pressure count.
esp_err_t mpr_read_pressure(int32_t* pressure);

#endif
