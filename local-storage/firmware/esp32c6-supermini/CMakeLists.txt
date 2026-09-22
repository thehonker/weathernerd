# ESP-IDF project for WeatherNerd ESP32-C6 SuperMini firmware
cmake_minimum_required(VERSION 3.22)

list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.defaults")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32c6_weathernerd)
