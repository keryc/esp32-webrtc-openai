#ifndef AUDIO_COMMANDS_H
#define AUDIO_COMMANDS_H

#include <esp_err.h>

/**
 * Register audio console commands
 * 
 * @return ESP_OK on success
 */
esp_err_t audio_register_commands(void);

#endif // AUDIO_COMMANDS_H