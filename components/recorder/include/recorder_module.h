#ifndef _RECORDER_H_
#define _RECORDER_H_

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RECORDER_SAMPLE_RATE 24000
#define RECORDER_CHANNELS 2
#define RECORDER_BITS_PER_SAMPLE 16
#define RECORDER_BUFFER_SIZE (4096)
#define RECORDER_MAX_FILENAME_LEN 256

typedef enum {
    RECORDER_STATE_IDLE,
    RECORDER_STATE_RECORDING,
    RECORDER_STATE_STOPPING,
    RECORDER_STATE_ERROR
} recorder_state_t;

typedef struct {
    bool enabled;
    char output_dir[128];
    size_t max_file_size_bytes;
    uint32_t buffer_size;
} recorder_config_t;

typedef struct recorder_handle_s* recorder_handle_t;

#define RECORDER_DEFAULT_CONFIG() { \
    .enabled = true, \
    .output_dir = "/sdcard", \
    .max_file_size_bytes = 50 * 1024 * 1024, \
    .buffer_size = RECORDER_BUFFER_SIZE \
}

esp_err_t recorder_init(const recorder_config_t *config, recorder_handle_t *handle);

esp_err_t recorder_deinit(recorder_handle_t handle);

esp_err_t recorder_start(recorder_handle_t handle);

esp_err_t recorder_stop(recorder_handle_t handle);

esp_err_t recorder_feed_audio(recorder_handle_t handle, const uint8_t *data, size_t size);

recorder_state_t recorder_get_state(recorder_handle_t handle);

const char* recorder_get_current_filename(recorder_handle_t handle);

size_t recorder_get_bytes_written(recorder_handle_t handle);

int recorder_audio_callback(uint8_t *data, int size, void *ctx);

// Helper functions for global instance
void* recorder_get_handle(void);
bool recorder_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif