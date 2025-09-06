file(GLOB_RECURSE SRCS "src/*.c")

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "include" "include/media"
    REQUIRES driver esp_timer nvs_flash esp_codec_dev esp_audio_effects esp_audio_codec
             gmf_audio gmf_core esp-sr esp-dsp dl_fft codec_board 
             esp_capture av_render media_lib_sal esp_webrtc
    PRIV_REQUIRES system webrtc
)