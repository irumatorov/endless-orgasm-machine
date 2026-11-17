#include "running_average.h"
#include <stdlib.h>

running_average_t* running_average_create(size_t window) {
    if (window == 0) window = 1;

    running_average_t* ra = calloc(1, sizeof(running_average_t));
    if (ra == NULL) return NULL;

    ra->buffer = calloc(window, sizeof(uint16_t));
    if (ra->buffer == NULL) {
        free(ra);
        return NULL;
    }

    ra->window = window;
    return ra;
}

void running_average_add_value(running_average_t* ra, uint16_t value) {
    if (ra == NULL) return;

    ra->sum -= ra->buffer[ra->index];
    ra->buffer[ra->index] = value;
    ra->sum += value;
    ra->index = (ra->index + 1) % ra->window;
}

uint16_t running_average_get_average(const running_average_t* ra) {
    if (ra == NULL) return 0;
    return ra->sum / ra->window;
}

void running_average_dispose(running_average_t* ra) {
    if (ra == NULL) return;

    free(ra->buffer);
    free(ra);
}
