/*
 * System Commands
 * Console commands for memory and system monitoring
 */

#ifndef SYSTEM_COMMANDS_H
#define SYSTEM_COMMANDS_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

// Register all system commands with console
esp_err_t system_commands_register(void);

#ifdef __cplusplus
}
#endif

#endif // SYSTEM_COMMANDS_H