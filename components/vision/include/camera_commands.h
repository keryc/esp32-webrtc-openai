#pragma once

#include <esp_err.h>
#include <esp_console.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register all camera console commands
 * 
 * @return ESP_OK on success
 */
esp_err_t camera_commands_register(void);

#ifdef __cplusplus
}
#endif
