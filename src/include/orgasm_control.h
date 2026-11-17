#ifndef __orgasm_control_h
#define __orgasm_control_h

#include <stdbool.h>
#include <stdint.h>

// The names sent over WebSocket are in orgasm_control.c, in this order.
typedef enum orgasm_output_mode {
    OC_MANUAL,    // pleasure is set by the user
    OC_AUTOMATIC, // "ENDLESS": edge forever
    OC_ORGASM,    // edge for a while, then allow an orgasm
    _OC_MODE_MAX,
    _OC_MODE_ERROR = -1
} orgasm_output_mode_t;

void orgasm_control_init(void);
// Call as often as possible, it paces itself to Config.update_frequency_hz.
void orgasm_control_tick(void);

//=== Readings
uint16_t orgasm_control_get_arousal(void);
uint8_t orgasm_control_get_pleasure(void);
uint16_t orgasm_control_get_last_pressure(void);
uint16_t orgasm_control_get_average_pressure(void);
uint8_t orgasm_control_get_denied_count(void);
// Seconds left before pleasure resumes after an edge.
uint8_t orgasm_control_get_cooldown(void);
// Seconds left before an orgasm is permitted in orgasm mode.
uint16_t orgasm_control_get_permit_orgasm_remaining_seconds(void);
bool orgasm_control_is_permit_orgasm_reached(void);

//=== Controls
orgasm_output_mode_t orgasm_control_get_output_mode(void);
void orgasm_control_set_output_mode(orgasm_output_mode_t mode);
const char* orgasm_control_get_output_mode_str(void);
orgasm_output_mode_t orgasm_control_str_to_output_mode(const char* str);
// Sets the pleasure level directly. Only has an effect in manual mode.
void orgasm_control_set_manual_pleasure(uint8_t pleasure);
void orgasm_control_reset_denied(void);
// Simulates an edge: stops pleasure and starts the cooldown.
void orgasm_control_trigger_arousal(void);

#endif
