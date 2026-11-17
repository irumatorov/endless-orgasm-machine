idf_component_register(
    SRCS "main.c" "config.c" "eom_hal.c" "mpr_pressure_sensor.c" "orgasm_control.c"
         "running_average.c" "vibration_modes.c" "ble.c" "wifi.c" "web.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES nvs_flash spiffs esp_adc esp_driver_gpio esp_driver_i2c esp_timer
                  esp_wifi esp_netif esp_http_server json bt led_strip mdns
)
