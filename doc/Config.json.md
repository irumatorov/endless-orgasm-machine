# Configuration

Settings live in `/spiffs/config.json` on the device. Changes made from the UI are saved there
automatically. *Upload Filesystem Image* replaces it with your `data/config.json`, so copy
`data/config.json.example` to `data/config.json` and edit that before uploading. Missing keys
use the defaults below. Changes to networking and Bluetooth take effect after a restart.

|Key|Default|Description|
|---|---|---|
|`wifi_on`|false|Enable WiFi, the web UI and the WebSocket API|
|`wifi_ap_mode`|true|Start the device's own network instead of joining `wifi_ssid`. Also used if joining fails|
|`wifi_ssid`, `wifi_key`|""|Network to join when `wifi_ap_mode` is false|
|`ap_ssid`, `ap_password`|"EOM", ""|The device's own network. An empty password means open, otherwise use 8 or more characters|
|`ap_channel`, `ap_max_connections`|1, 4|Access point settings|
|`hostname`|"eom"|The device answers on `http://<hostname>.local`|
|`websocket_port`|80|Port for the UI and WebSocket API|
|`bt_on`|true|Enable Bluetooth|
|`bt_display_name`|"Libotoy"|Bluetooth name. The UI looks for names starting with Libotoy|
|`classic_serial`|false|Print `pressure,arousal,pleasure,threshold,clench threshold,clench ms,denied` lines on the serial port|
|`chart_window_s`|5|Seconds of history shown in the UI chart|
|`vibration_mode`|2|1 Ramp-Stop, 2 Depletion, 3 Enhancement (see README)|
|`initial_pleasure`|10|Pleasure (0-255) when stimulation starts|
|`max_pleasure`|255|Maximum pleasure|
|`motor_ramp_time_s`|120|Seconds to ramp from initial to maximum pleasure|
|`sensitivity_threshold`|1024|Arousal (0-4095) that counts as an edge. Lower stops sooner|
|`mid_threshold`|512|Arousal where the LED starts turning from orange to red|
|`edge_delay`|10|Seconds to wait after an edge before pleasure resumes|
|`max_additional_delay`|10|Up to this many random seconds are added to `edge_delay`|
|`minimum_on_time`|1|Seconds after pleasure resumes before edges are detected again|
|`max_denied`|20|In orgasm mode, allow an orgasm after this many edges. 0 or 100 = no limit|
|`auto_edging_duration_minutes`|30|In orgasm mode, allow an orgasm after this many minutes|
|`post_orgasm_duration_seconds`|10|Seconds of full pleasure after an orgasm is detected, before switching to manual|
|`update_frequency_hz`|50|Pressure readings per second|
|`pressure_smoothing`|10|Readings in the running average|
|`use_average_values`|true|Detect arousal from the running average instead of single readings|
|`sensor_sensitivity`|50|Analog sensors: ADC range, 0-24 about 3.1 V, 25-49 about 1.75 V, 50-74 about 1.25 V, 75-100 about 0.95 V full scale. Pick the smallest range your sensor's output stays under. MPR sensors: gain in percent|
|`clench_pressure_sensitivity`|300|Pressure rise that counts as a clench|
|`clench_time_threshold_ms`|900|A clench this long raises arousal, if `clench_detector_in_edging` is on|
|`max_clench_duration_ms`|3000|Clenches longer than this stop raising arousal|
|`clench_time_to_orgasm_ms`|1500|Once an orgasm is permitted, a clench this long counts as the orgasm|
|`clench_detector_in_edging`|false|Let clenches raise arousal while edging|
