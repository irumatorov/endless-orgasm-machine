#ifndef __web_h
#define __web_h

// Serves the UI from /spiffs/ui and the WebSocket API, both on Config.websocket_port.
void web_init(void);
// Streams readings to subscribed WebSocket clients. Call regularly from the main loop.
void web_tick(void);

#endif
