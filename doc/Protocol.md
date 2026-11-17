# Protocols

## WebSocket

Connect to `ws://<device>/` (the same address that serves the UI). Every message is a JSON
object of `{"command": argument}` pairs, and several commands can share one message.
A command that returns something replies under its own name, e.g. sending
`{"configList": null}` returns `{"configList": {...}}`.

|Command|Argument|Effect / reply|
|---|---|---|
|`configList`|any|Replies with every option except passwords|
|`configSet`|`{"key": value, ...}`|Changes options and saves them a second later|
|`setMode`|`"MANUAL"`, `"ENDLESS"` or `"ORGASM"`|Changes the control mode|
|`setMotor`|0-255|Switches to manual mode and sets pleasure. Replies `{"mode": "MANUAL"}` if the mode changed|
|`resetDenied`|any|Resets the denial count|
|`triggerArousal`|any|Simulates an edge|
|`streamReadings`|`false` to stop|Starts sending `readings` about 15 times a second|
|`info`|any|Replies `{"device", "fwVersion"}`|
|`restart`|any|Reboots the device|

Unknown commands reply `{"error": "unknown command"}`.

`readings` messages look like
`{"readings": {"pressure", "pavg", "arousal", "threshold", "motor", "denied", "cooldown", "permit", "runMode", "millis"}}`.
Pressure, arousal and threshold are 0-4095, `motor` is pleasure 0-255, `cooldown` and `permit`
are seconds left before pleasure resumes and before an orgasm is allowed.

## Bluetooth LE

The device advertises as `bt_display_name` (default `Libotoy`).

### EOM service `0x6969`, used by the UI

|Characteristic|Access|Content|
|---|---|---|
|`0x696A` status|read, notify (20/s)|17 bytes: pressure>>4, arousal>>4, threshold>>4, pleasure, cooldown s, denied, mode (0 manual, 1 endless, 2 orgasm), vibration mode, permit s (2 bytes, big endian), LED r, g, b, uptime ms (4 bytes, big endian)|
|`0x696B` control|write|`01 mode trigger reset [pleasure]`: set mode, simulate an edge if trigger is non-zero, reset denials if reset is non-zero, set manual pleasure<br>`02` + `key:value` text: set one option<br>`03`: send the config on `0x696C`|
|`0x696C` config|notify|Options as JSON fragments without braces, `"key":value,...`, split over as many notifications as needed|

### Monsterpub emulation `0x6000`, used by XToys

|Characteristic|Access|Content|
|---|---|---|
|`0x6031` output|read, notify (10/s)|Pleasure as a 16 bit little endian value, 0-1530|
|`0x6003` input|write|Vibration 0-100. Sets pleasure in manual mode, ignored otherwise|
