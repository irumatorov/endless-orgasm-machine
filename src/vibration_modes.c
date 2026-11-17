#include "config.h"
#include "orgasm_control.h"
#include "vibration_mode_controller.h"
#include <math.h>

// Only one controller is active at a time, so they share their state.
static struct {
    float pleasure;
    uint16_t arousal;
    float base_speed;
    bool stopped;
} state;

// Pleasure change per update needed to go from start to target in time_s seconds.
static float increment_per_tick(float start, float target, float time_s) {
    if (time_s <= 0 || Config.update_frequency_hz == 0) return 0;
    return (target - start) / time_s / Config.update_frequency_hz;
}

static void tick(float pleasure, uint16_t arousal) {
    state.pleasure = pleasure;
    state.arousal = arousal;
}

static float arousal_ratio(void) {
    return (float)state.arousal / Config.sensitivity_threshold;
}

//=== Ramp-Stop: ramp up to max_pleasure, stop dead on an edge.

static float ramp_stop_start(void) {
    state.pleasure = Config.initial_pleasure;
    return state.pleasure;
}

static float ramp_stop_increment(void) {
    float step = increment_per_tick(
        Config.initial_pleasure, Config.max_pleasure, Config.motor_ramp_time_s
    );

    if (state.pleasure < Config.max_pleasure - step) {
        state.pleasure += step;
        return state.pleasure;
    }
    return Config.max_pleasure;
}

static float ramp_stop_stop(void) {
    state.pleasure = 0;
    return 0;
}

const vibration_mode_controller_t RampStopController = {
    .start = ramp_stop_start,
    .increment = ramp_stop_increment,
    .stop = ramp_stop_stop,
    .tick = tick,
};

//=== Depletion: ramp up, but back off in proportion to arousal.

static float depletion_start(void) {
    state.base_speed = Config.initial_pleasure;
    return state.base_speed;
}

static float depletion_increment(void) {
    if (state.base_speed < Config.max_pleasure) {
        state.base_speed += increment_per_tick(
            Config.initial_pleasure, Config.max_pleasure, Config.motor_ramp_time_s
        );
    }

    // Once an orgasm is permitted, arousal only takes away half as much.
    float reduction = fminf(1.0f, arousal_ratio());
    if (orgasm_control_is_permit_orgasm_reached()) reduction /= 2;
    float speed = state.base_speed * (1 - reduction);

    return fminf(fmaxf(speed, Config.initial_pleasure), Config.max_pleasure);
}

static float depletion_stop(void) {
    return 0;
}

const vibration_mode_controller_t DepletionController = {
    .start = depletion_start,
    .increment = depletion_increment,
    .stop = depletion_stop,
    .tick = tick,
};

//=== Enhancement: pleasure follows arousal up, peaks on an edge, then fades out.

static float enhancement_start(void) {
    state.stopped = false;
    return Config.initial_pleasure;
}

static float enhancement_increment(void) {
    if (state.stopped) {
        if (Config.edge_delay == 0) return 0;
        float step = increment_per_tick(Config.max_pleasure, 0, Config.edge_delay);
        return fmaxf(state.pleasure + step, 0.0f);
    }

    return Config.initial_pleasure +
           arousal_ratio() * (Config.max_pleasure - Config.initial_pleasure);
}

static float enhancement_stop(void) {
    state.stopped = true;
    return Config.max_pleasure;
}

const vibration_mode_controller_t EnhancementController = {
    .start = enhancement_start,
    .increment = enhancement_increment,
    .stop = enhancement_stop,
    .tick = tick,
};
