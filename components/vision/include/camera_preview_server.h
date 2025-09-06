#pragma once

#include <esp_err.h>
#include <esp_http_server.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize camera preview HTTP server for laptop viewing
 * 
 * @param port HTTP server port
 * @return ESP_OK on success
 */
esp_err_t camera_preview_server_init(uint16_t port);

/**
 * @brief Start camera preview server
 * 
 * @return ESP_OK on success
 */
esp_err_t camera_preview_server_start(void);

/**
 * @brief Stop camera preview server
 * 
 * @return ESP_OK on success
 */
esp_err_t camera_preview_server_stop(void);

/**
 * @brief Deinitialize camera preview server and free resources
 * 
 * @return ESP_OK on success
 */
esp_err_t camera_preview_server_deinit(void);

/**
 * @brief Send camera frame to connected clients
 * 
 * @param frame_data JPEG frame data
 * @param frame_size Frame size in bytes
 * @return ESP_OK on success
 */
esp_err_t camera_preview_server_send_frame(uint8_t *frame_data, size_t frame_size);

/**
 * @brief Check if server is running
 * 
 * @return true if running, false otherwise
 */
bool camera_preview_server_is_running(void);

/**
 * @brief Get server URL for laptop viewing
 * 
 * @param url_buffer Buffer to store URL
 * @param buffer_size Buffer size
 * @return ESP_OK on success
 */
esp_err_t camera_preview_server_get_url(char *url_buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif