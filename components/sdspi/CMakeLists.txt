idf_component_register(
    SRCS "src/sdspi_module.c"
         "src/sdspi_commands.c"
    INCLUDE_DIRS "include"
    REQUIRES "driver" "fatfs" "vfs" "sdmmc" "esp_common" "console"
    PRIV_REQUIRES "esp_rom"
)