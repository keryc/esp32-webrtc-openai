file(GLOB_RECURSE SRCS "src/*.c")

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "include"
    REQUIRES driver esp32-camera esp_http_server nvs_flash esp_timer
             esp_capture esp_jpeg esp_new_jpeg esp_image_effects console
             esp_wifi
    PRIV_REQUIRES webrtc system esp_websocket_client
)