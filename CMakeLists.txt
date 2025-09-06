cmake_minimum_required(VERSION 3.16)

# Include ESP-IDF project configuration
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Set component directories
set(EXTRA_COMPONENT_DIRS 
    ${CMAKE_CURRENT_SOURCE_DIR}/components
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/esp-webrtc-solution/components
)

# Security: Get API key from environment variable only
if(DEFINED ENV{OPENAI_API_KEY})
    add_compile_definitions(OPENAI_API_KEY="$ENV{OPENAI_API_KEY}")
else()
    message(FATAL_ERROR "OPENAI_API_KEY environment variable not set. Export it before building.")
endif()

# Define the project
project(esp32_webrtc_openai)

# SPIFFS support for audio feedback files
spiffs_create_partition_image(spiffs spiffs FLASH_IN_PROJECT)
