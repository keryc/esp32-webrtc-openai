#include "recorder_module.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <time.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"
#include "sdspi_module.h"
#include "audio_feedback.h"
#include "audio_module.h"

static const char *TAG = "recorder";

// Global instance
static recorder_handle_t g_recorder = NULL;

typedef struct __attribute__((packed)) {
    char     riff[4];
    uint32_t file_size;
    char     wave[4];
} wav_riff_header_t;

typedef struct __attribute__((packed)) {
    char     fmt[4];
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} wav_fmt_chunk_t;

typedef struct __attribute__((packed)) {
    char     data[4];
    uint32_t data_size;
} wav_data_chunk_t;

struct recorder_handle_s {
    recorder_config_t config;
    recorder_state_t state;
    FILE *file;
    char current_filename[RECORDER_MAX_FILENAME_LEN];
    size_t bytes_written;
    size_t data_size;
    RingbufHandle_t ring_buffer;
    TaskHandle_t write_task;
    SemaphoreHandle_t mutex;
    bool stop_requested;
};

static void recorder_write_task(void *arg);
static esp_err_t create_wav_file(recorder_handle_t handle);
static esp_err_t finalize_wav_file(recorder_handle_t handle);
static void generate_filename(char *buffer, size_t buffer_size, const char *dir);

esp_err_t recorder_init(const recorder_config_t *config, recorder_handle_t *handle)
{
    if (!config || !handle) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!sdspi_module_is_mounted()) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    recorder_handle_t rec = calloc(1, sizeof(struct recorder_handle_s));
    if (!rec) {
        ESP_LOGE(TAG, "Failed to allocate memory for recorder");
        return ESP_ERR_NO_MEM;
    }
    
    memcpy(&rec->config, config, sizeof(recorder_config_t));
    rec->state = RECORDER_STATE_IDLE;
    
    // Verificar que el directorio de salida existe (debe ser /sdcard que ya está montado)
    struct stat st;
    if (stat(config->output_dir, &st) != 0) {
        ESP_LOGE(TAG, "Output directory does not exist: %s", config->output_dir);
        free(rec);
        return ESP_FAIL;
    }
    
    rec->ring_buffer = xRingbufferCreate(config->buffer_size * 16, RINGBUF_TYPE_BYTEBUF);  // Aumentado de *4 a *16
    if (!rec->ring_buffer) {
        ESP_LOGE(TAG, "Failed to create ring buffer");
        free(rec);
        return ESP_ERR_NO_MEM;
    }
    
    rec->mutex = xSemaphoreCreateMutex();
    if (!rec->mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        vRingbufferDelete(rec->ring_buffer);
        free(rec);
        return ESP_ERR_NO_MEM;
    }
    
    *handle = rec;
    g_recorder = rec;  // Store globally
    
    // Automatically configure audio modules to use recorder
    audio_module_set_recorder_handle(rec);
    audio_feedback_set_recorder_handle(rec);
    
    ESP_LOGI(TAG, "Recorder initialized successfully and configured for audio modules");
    return ESP_OK;
}

esp_err_t recorder_deinit(recorder_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (handle->state == RECORDER_STATE_RECORDING) {
        recorder_stop(handle);
    }
    
    if (handle->ring_buffer) {
        vRingbufferDelete(handle->ring_buffer);
    }
    
    if (handle->mutex) {
        vSemaphoreDelete(handle->mutex);
    }
    
    if (handle == g_recorder) {
        g_recorder = NULL;
        // Clear recorder from audio modules
        audio_module_set_recorder_handle(NULL);
        audio_feedback_set_recorder_handle(NULL);
    }
    free(handle);
    ESP_LOGI(TAG, "Recorder deinitialized");
    return ESP_OK;
}

esp_err_t recorder_start(recorder_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(handle->mutex, portMAX_DELAY);
    
    if (handle->state != RECORDER_STATE_IDLE) {
        ESP_LOGW(TAG, "Recorder not in idle state");
        xSemaphoreGive(handle->mutex);
        return ESP_ERR_INVALID_STATE;
    }
    
    generate_filename(handle->current_filename, sizeof(handle->current_filename), 
                     handle->config.output_dir);
    
    esp_err_t ret = create_wav_file(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create WAV file");
        xSemaphoreGive(handle->mutex);
        return ret;
    }
    
    handle->stop_requested = false;
    handle->state = RECORDER_STATE_RECORDING;
    
    BaseType_t task_ret = xTaskCreate(recorder_write_task, "recorder_write", 
                                      4096, handle, 10, &handle->write_task);  // Prioridad aumentada de 5 a 10
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create write task");
        fclose(handle->file);
        handle->file = NULL;
        handle->state = RECORDER_STATE_IDLE;
        xSemaphoreGive(handle->mutex);
        return ESP_FAIL;
    }
    
    xSemaphoreGive(handle->mutex);
    
    ESP_LOGI(TAG, "🔴 Recording started: %s (continuous mode)", handle->current_filename);
    return ESP_OK;
}

esp_err_t recorder_stop(recorder_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(handle->mutex, portMAX_DELAY);
    
    if (handle->state != RECORDER_STATE_RECORDING) {
        ESP_LOGW(TAG, "Recorder not recording");
        xSemaphoreGive(handle->mutex);
        return ESP_ERR_INVALID_STATE;
    }
    
    handle->stop_requested = true;
    handle->state = RECORDER_STATE_STOPPING;
    xSemaphoreGive(handle->mutex);
    
    if (handle->write_task) {
        xTaskNotifyGive(handle->write_task);
        vTaskDelay(pdMS_TO_TICKS(100));
        
        if (eTaskGetState(handle->write_task) != eDeleted) {
            vTaskDelete(handle->write_task);
        }
        handle->write_task = NULL;
    }
    
    xSemaphoreTake(handle->mutex, portMAX_DELAY);
    
    size_t remaining_size;
    void *remaining_data = xRingbufferReceiveUpTo(handle->ring_buffer, &remaining_size, 0, 
                                                  handle->config.buffer_size);
    if (remaining_data && remaining_size > 0 && handle->file) {
        fwrite(remaining_data, 1, remaining_size, handle->file);
        handle->data_size += remaining_size;
        vRingbufferReturnItem(handle->ring_buffer, remaining_data);
    }
    
    finalize_wav_file(handle);
    
    handle->state = RECORDER_STATE_IDLE;
    handle->bytes_written = 0;
    handle->data_size = 0;
    
    xSemaphoreGive(handle->mutex);
    
    ESP_LOGI(TAG, "Recording stopped: %s", handle->current_filename);
    return ESP_OK;
}

esp_err_t recorder_feed_audio(recorder_handle_t handle, const uint8_t *data, size_t size)
{
    if (!handle || !data || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (handle->state != RECORDER_STATE_RECORDING) {
        return ESP_OK;
    }
    
    // Solo verificar límite si está configurado (0 = sin límite)
    if (handle->config.max_file_size_bytes > 0 && 
        handle->data_size + size > handle->config.max_file_size_bytes) {
        ESP_LOGW(TAG, "Max file size reached, stopping recording");
        recorder_stop(handle);
        return ESP_OK;
    }
    
    BaseType_t ret = xRingbufferSend(handle->ring_buffer, data, size, pdMS_TO_TICKS(10));
    if (ret != pdTRUE) {
        ESP_LOGW(TAG, "Ring buffer full, dropping audio data");
        return ESP_FAIL;
    }
    
    if (handle->write_task) {
        xTaskNotifyGive(handle->write_task);
    }
    
    return ESP_OK;
}

recorder_state_t recorder_get_state(recorder_handle_t handle)
{
    if (!handle) {
        return RECORDER_STATE_ERROR;
    }
    return handle->state;
}

const char* recorder_get_current_filename(recorder_handle_t handle)
{
    if (!handle) {
        return NULL;
    }
    return handle->current_filename;
}

size_t recorder_get_bytes_written(recorder_handle_t handle)
{
    if (!handle) {
        return 0;
    }
    return handle->bytes_written;
}

int recorder_audio_callback(uint8_t *data, int size, void *ctx)
{
    recorder_handle_t handle = (recorder_handle_t)ctx;
    
    if (!handle || !data || size <= 0) {
        return 0;
    }
    
    if (handle->state == RECORDER_STATE_RECORDING) {
        recorder_feed_audio(handle, data, size);
    }
    
    return 0;
}

static void recorder_write_task(void *arg)
{
    recorder_handle_t handle = (recorder_handle_t)arg;
    size_t item_size;
    void *item;
    size_t last_header_update = 0;
    const size_t HEADER_UPDATE_INTERVAL = 32 * 1024;  // Update headers every 32KB for better protection
    
    ESP_LOGI(TAG, "Write task started");
    
    while (!handle->stop_requested) {
        // Process buffer data continuously with short timeout to check stop_requested
        item = xRingbufferReceiveUpTo(handle->ring_buffer, &item_size, pdMS_TO_TICKS(10), 
                                      handle->config.buffer_size);
        
        if (item != NULL && item_size > 0) {
            if (handle->file) {
                size_t written = fwrite(item, 1, item_size, handle->file);
                if (written != item_size) {
                    ESP_LOGE(TAG, "Failed to write audio data to file (written=%d, expected=%d)", 
                             written, item_size);
                }
                handle->data_size += written;
                handle->bytes_written += written;
                
                // Periodically update WAV headers to keep file valid in case of disconnection
                if (handle->data_size - last_header_update >= HEADER_UPDATE_INTERVAL) {
                    // Save current file position
                    long current_pos = ftell(handle->file);
                    
                    // Update RIFF header with file size
                    fseek(handle->file, 4, SEEK_SET);
                    uint32_t file_size = 36 + handle->data_size;
                    fwrite(&file_size, sizeof(uint32_t), 1, handle->file);
                    
                    // Update DATA header with data size
                    fseek(handle->file, 40, SEEK_SET);
                    fwrite(&handle->data_size, sizeof(uint32_t), 1, handle->file);
                    
                    // Return to current position to continue writing
                    fseek(handle->file, current_pos, SEEK_SET);
                    
                    // Force write to disk
                    fflush(handle->file);
                    fsync(fileno(handle->file));
                    
                    last_header_update = handle->data_size;
                    ESP_LOGD(TAG, "Updated WAV headers at %.1f MB", 
                             handle->data_size / (1024.0 * 1024.0));
                }
                
                // Flush more frequently to avoid accumulation
                if (handle->bytes_written % (1024 * 1024) == 0) {  // Every 1MB
                    fflush(handle->file);
                    ESP_LOGI(TAG, "📼 Recording: %.1f MB captured", 
                             handle->bytes_written / (1024.0 * 1024.0));
                }
            }
            vRingbufferReturnItem(handle->ring_buffer, item);
        }
        
        // Yield to avoid watchdog
        taskYIELD();
    }
    
    ESP_LOGI(TAG, "Write task stopped");
    vTaskDelete(NULL);
}

static esp_err_t create_wav_file(recorder_handle_t handle)
{
    ESP_LOGI(TAG, "Attempting to create file: %s", handle->current_filename);
    
    // Verificar que la SD card está montada antes de crear el archivo
    if (!sdspi_module_is_mounted()) {
        ESP_LOGE(TAG, "SD card is not mounted!");
        return ESP_FAIL;
    }
    
    // Verificar que el directorio existe y es accesible
    struct stat st;
    if (stat(handle->config.output_dir, &st) != 0) {
        ESP_LOGE(TAG, "Output directory does not exist: %s (errno: %d)", 
                 handle->config.output_dir, errno);
        return ESP_FAIL;
    }
    
    if (!S_ISDIR(st.st_mode)) {
        ESP_LOGE(TAG, "Output path is not a directory: %s", handle->config.output_dir);
        return ESP_FAIL;
    }
    
    // Verificar que la SD card está accesible
    const char *test_path = "/sdcard/test.tmp";
    FILE *test_file = fopen(test_path, "wb");
    if (test_file) {
        fclose(test_file);
        unlink(test_path);
        ESP_LOGI(TAG, "SD card is writable");
    } else {
        ESP_LOGE(TAG, "Cannot create test file on SD card! errno: %d - %s", errno, strerror(errno));
        ESP_LOGE(TAG, "SD card might be read-only or not properly initialized");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Opening file: %s", handle->current_filename);
    handle->file = fopen(handle->current_filename, "wb");
    if (!handle->file) {
        ESP_LOGE(TAG, "Failed to open file: %s (errno: %d - %s)", 
                 handle->current_filename, errno, strerror(errno));
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "File opened successfully");
    
    wav_riff_header_t riff_header = {
        .riff = {'R', 'I', 'F', 'F'},
        .file_size = 0,
        .wave = {'W', 'A', 'V', 'E'}
    };
    
    wav_fmt_chunk_t fmt_chunk = {
        .fmt = {'f', 'm', 't', ' '},
        .fmt_size = 16,
        .audio_format = 1,
        .num_channels = RECORDER_CHANNELS,
        .sample_rate = RECORDER_SAMPLE_RATE,
        .byte_rate = RECORDER_SAMPLE_RATE * RECORDER_CHANNELS * (RECORDER_BITS_PER_SAMPLE / 8),
        .block_align = RECORDER_CHANNELS * (RECORDER_BITS_PER_SAMPLE / 8),
        .bits_per_sample = RECORDER_BITS_PER_SAMPLE
    };
    
    wav_data_chunk_t data_chunk = {
        .data = {'d', 'a', 't', 'a'},
        .data_size = 0
    };
    
    fwrite(&riff_header, sizeof(riff_header), 1, handle->file);
    fwrite(&fmt_chunk, sizeof(fmt_chunk), 1, handle->file);
    fwrite(&data_chunk, sizeof(data_chunk), 1, handle->file);
    
    handle->data_size = 0;
    handle->bytes_written = sizeof(riff_header) + sizeof(fmt_chunk) + sizeof(data_chunk);
    
    return ESP_OK;
}

static esp_err_t finalize_wav_file(recorder_handle_t handle)
{
    if (!handle->file) {
        return ESP_OK;
    }
    
    // Update final headers
    fseek(handle->file, 4, SEEK_SET);
    uint32_t file_size = 36 + handle->data_size;
    fwrite(&file_size, sizeof(uint32_t), 1, handle->file);
    
    fseek(handle->file, 40, SEEK_SET);
    fwrite(&handle->data_size, sizeof(uint32_t), 1, handle->file);
    
    // Ensure everything is written to disk before closing
    fflush(handle->file);
    fsync(fileno(handle->file));
    
    fclose(handle->file);
    handle->file = NULL;
    
    ESP_LOGI(TAG, "WAV file finalized: %s (%.2f MB)", 
             handle->current_filename, handle->data_size / (1024.0 * 1024.0));
    
    return ESP_OK;
}

static void generate_filename(char *buffer, size_t buffer_size, const char *dir)
{
    // Use a simple counter for filenames
    static uint32_t session_counter = 0;
    struct stat st;
    int ret;
    
    // Find a filename that doesn't exist
    do {
        session_counter++;
        
        // Simple format: rec_1.wav, rec_2.wav, etc.
        ret = snprintf(buffer, buffer_size, "%s/rec_%u.wav", 
                       dir, (unsigned int)session_counter);
        
        // Check if snprintf failed
        if (ret < 0 || ret >= (int)buffer_size) {
            ESP_LOGE(TAG, "Failed to generate filename (ret=%d, buffer_size=%zu)", ret, buffer_size);
            // Ultra simple fallback
            snprintf(buffer, buffer_size, "%s/r%u.wav", dir, (unsigned int)session_counter);
        }
        
        // Check if file already exists
        if (stat(buffer, &st) != 0) {
            // File doesn't exist, we can use this name
            break;
        }
        
        ESP_LOGD(TAG, "File %s already exists, trying next counter", buffer);
        
    } while (1);  // Continue until we find an available name
    
    ESP_LOGI(TAG, "Generated filename: %s", buffer);
}

void* recorder_get_handle(void)
{
    return g_recorder;
}

bool recorder_is_initialized(void)
{
    return (g_recorder != NULL);
}