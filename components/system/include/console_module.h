#ifndef CONSOLE_MODULE_H
#define CONSOLE_MODULE_H

#include <esp_err.h>

/**
 * Initialize the console module
 * Sets up UART, esp_console, and linenoise
 * 
 * @return ESP_OK on success
 */
esp_err_t console_module_init(void);

/**
 * Start the console task
 * This creates a FreeRTOS task that handles console input
 * 
 * @return ESP_OK on success
 */
esp_err_t console_module_start(void);

/**
 * Register all console commands
 * This should be called after modules are initialized
 * 
 * @return ESP_OK on success
 */
esp_err_t console_register_commands(void);

#endif // CONSOLE_MODULE_H