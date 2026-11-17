#ifndef __wifi_h
#define __wifi_h

// Joins Config.wifi_ssid, or starts the access point if that is configured or fails.
void wifi_init(void);
const char* wifi_get_ip(void);

#endif
