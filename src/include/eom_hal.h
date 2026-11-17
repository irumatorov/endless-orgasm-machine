#ifndef __eom_hal_h
#define __eom_hal_h

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} RGBColor;

extern const RGBColor rgb_off;
extern const RGBColor rgb_red;
extern const RGBColor rgb_green;
extern const RGBColor rgb_blue;
extern const RGBColor rgb_orange;
extern const RGBColor rgb_purple;

//=== Pressure
void eom_hal_init_pressure_sensor(void);
// Pressure above the ambient level measured at startup, 0 to EOM_PRESSURE_MAX.
uint16_t eom_hal_get_pressure_reading(void);
void eom_hal_set_sensor_sensitivity(uint8_t sensitivity);

//=== Status LED
// The setters only record the requested state and may be called from any task.
// eom_hal_led_tick() drives the hardware and must be called regularly from one task.
void eom_hal_led_init(void);
void eom_hal_led_tick(void);
void eom_hal_set_rgb(RGBColor color);
RGBColor eom_hal_get_rgb(void);
void eom_hal_set_led_flashing(bool flashing);
void eom_hal_set_led_flash_interval(uint16_t interval_ms);
// Blends from start (progress 0.0) to end (progress 1.0).
RGBColor eom_hal_fade_color(RGBColor start, RGBColor end, float progress);

#endif
