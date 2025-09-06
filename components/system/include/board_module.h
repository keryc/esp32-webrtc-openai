#ifndef BOARD_MODULE_H
#define BOARD_MODULE_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize board hardware peripherals
 * 
 * This function initializes all board-level peripherals including:
 * - I2C buses
 * - SPI interfaces  
 * - Audio codec
 * - Camera interface
 * - Microphone
 * - GPIO pins
 * - LEDs
 * - Buttons
 * - Power management
 * 
 * This function is idempotent - can be called multiple times safely.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t board_module_init(void);

#ifdef __cplusplus
}
#endif

#endif // BOARD_MODULE_H
