#ifndef OPENAI_SIGNALING_H
#define OPENAI_SIGNALING_H

#include "esp_webrtc.h"
#include "esp_webrtc_defaults.h"
#include "esp_peer_default.h"
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OpenAI signaling configuration
 */
typedef struct {
    char *token; /*!< OpenAI API token */
    char *voice; /*!< Voice to select (optional) */
} openai_signaling_cfg_t;

/**
 * @brief Get OpenAI signaling implementation
 * @return Pointer to signaling implementation
 */
const esp_peer_signaling_impl_t *openai_signaling_get_impl(void);

#ifdef __cplusplus
}
#endif

#endif // OPENAI_SIGNALING_H