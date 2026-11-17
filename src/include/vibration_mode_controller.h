#ifndef __vibration_mode_controller_h
#define __vibration_mode_controller_h

#include <stdint.h>

// A vibration mode decides how pleasure follows arousal while the device is in control.
typedef struct vibration_mode_controller {
    // Called when stimulation (re)starts. Returns the starting pleasure.
    float (*start)(void);
    // Called on every update while stimulating. Returns the new pleasure.
    float (*increment)(void);
    // Called when an edge is detected. Returns the pleasure to hold during the cooldown.
    float (*stop)(void);
    // Called before the above with the current state.
    void (*tick)(float pleasure, uint16_t arousal);
} vibration_mode_controller_t;

extern const vibration_mode_controller_t RampStopController;
extern const vibration_mode_controller_t DepletionController;
extern const vibration_mode_controller_t EnhancementController;

#endif
