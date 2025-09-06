# Base WebRTC sources (always included)
file(GLOB SRCS "src/*.c")

# Provider selection based on Kconfig
if(CONFIG_WEBRTC_PROVIDER_OPENAI)
    file(GLOB PROVIDER_SRCS "src/providers/openai/*.c")
endif()

# Combine sources
list(APPEND SRCS ${PROVIDER_SRCS})

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "." "include" "include/providers/openai"
    REQUIRES driver nvs_flash esp_timer esp_websocket_client esp_http_client
             json esp_libsrtp audio vision esp_webrtc esp_peer
             esp_capture av_render
    PRIV_REQUIRES system wifi
)
