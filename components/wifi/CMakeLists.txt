file(GLOB_RECURSE SRCS "src/*.c")

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi nvs_flash esp_netif esp_event
    PRIV_REQUIRES system
)