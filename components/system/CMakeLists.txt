file(GLOB_RECURSE SRCS "src/*.c")

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_timer nvs_flash spi_flash esp_system 
             media_lib_sal esp_capture console
    PRIV_REQUIRES codec_board
)