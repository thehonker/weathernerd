# WeatherNerd ESP32-C6 main component
idf_component_register(SRCS "main.c" "storage.c" "i2c_bus.c" "ds3231.c" "bme280.c" "wifi.c" "oled.c" "input.c" "ui.c"
                       INCLUDE_DIRS "."
                       REQUIRES ulp fatfs sdmmc esp_driver_i2c esp_wifi esp_http_server nvs_flash esp_timer)

# LP Core coprocessor binary
set(ulp_app_name lp_core_${COMPONENT_NAME})
set(ulp_lp_core_sources "lp_core/main.c")
set(ulp_exp_dep_srcs "main.c")
ulp_embed_binary(${ulp_app_name} "${ulp_lp_core_sources}" "${ulp_exp_dep_srcs}")
