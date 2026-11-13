cmake_minimum_required(VERSION 3.16)

# Gemeinsame Defaults + optionale lokale (geheime) Einstellungen
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.local")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.local")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(photobooth_ipp)
