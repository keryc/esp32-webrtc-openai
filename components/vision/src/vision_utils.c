#include "vision_utils.h"
#include <esp_log.h>
#include <stdlib.h>
#include <string.h>
#include "mbedtls/base64.h"
#include "memory_manager.h"

static const char *TAG = "vision_utils";

char* vision_utils_encode_base64(const uint8_t *jpeg_data, size_t jpeg_size)
{
    if (!jpeg_data || jpeg_size == 0) {
        ESP_LOGE(TAG, "Invalid input data for base64 encoding");
        return NULL;
    }
    
    // Calculate base64 encoded size
    size_t encoded_size = 0;
    mbedtls_base64_encode(NULL, 0, &encoded_size, jpeg_data, jpeg_size);
    
    // Allocate buffer for base64 encoded data
    char *encoded_data = mem_alloc(encoded_size + 1, MEM_POLICY_PREFER_PSRAM, "vision_base64");
    if (!encoded_data) {
        ESP_LOGE(TAG, "Failed to allocate base64 buffer (%zu bytes)", encoded_size + 1);
        return NULL;
    }
    
    // Encode the JPEG data
    size_t actual_size = 0;
    int ret = mbedtls_base64_encode((unsigned char*)encoded_data, encoded_size, 
                                    &actual_size, jpeg_data, jpeg_size);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to encode base64: %d", ret);
        mem_free(encoded_data);
        return NULL;
    }
    
    encoded_data[actual_size] = '\0';
    ESP_LOGD(TAG, "Encoded %zu bytes to base64 (%zu bytes)", jpeg_size, actual_size);
    
    return encoded_data;
}
