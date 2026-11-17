#ifndef __running_average_h
#define __running_average_h

#include <stddef.h>
#include <stdint.h>

typedef struct running_average {
    uint16_t* buffer;
    size_t window;
    size_t index;
    uint32_t sum;
} running_average_t;

// Returns NULL if out of memory. A window of 0 is treated as 1.
running_average_t* running_average_create(size_t window);
void running_average_add_value(running_average_t* ra, uint16_t value);
uint16_t running_average_get_average(const running_average_t* ra);
void running_average_dispose(running_average_t* ra);

#endif
