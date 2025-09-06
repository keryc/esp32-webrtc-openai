#ifndef THREAD_SCHEDULER_H
#define THREAD_SCHEDULER_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the global thread scheduler
 * 
 * This function sets up the optimized thread scheduler for all system tasks
 * including WebRTC, audio, and other media tasks.
 * 
 * @return ESP_OK on success
 */
esp_err_t thread_scheduler_init(void);

#ifdef __cplusplus
}
#endif

#endif // THREAD_SCHEDULER_H