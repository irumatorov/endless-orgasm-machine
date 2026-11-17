#ifndef __ble_h
#define __ble_h

#include <stdbool.h>

// Starts the NimBLE stack and advertises as Config.bt_display_name.
void ble_init(void);
// Sends due notifications. Call regularly from the main loop.
void ble_tick(void);
bool ble_is_connected(void);

#endif
