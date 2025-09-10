idf_component_register(
    SRCS "src/recorder_module.c" "src/recorder_commands.c"
    INCLUDE_DIRS "include"
    REQUIRES "esp_common" "fatfs" "sdspi" "audio" "console" "esp_peer"
    PRIV_REQUIRES "driver"
)