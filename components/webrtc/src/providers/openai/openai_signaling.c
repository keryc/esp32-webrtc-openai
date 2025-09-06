/* OpenAI signaling module
   Integrated signaling implementation for OpenAI WebRTC
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "https_client.h"
#include "esp_log.h"
#include <cJSON.h>
#include "openai_signaling.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "memory_manager.h"

#define TAG                   "OPENAI_SIGNALING"

#define SAFE_FREE(p) if (p) {   \
    mem_free(p);                \
    p = NULL;                   \
}

#define GET_KEY_END(str, key) get_key_end(str, key, sizeof(key) - 1)

typedef struct {
    esp_peer_signaling_cfg_t cfg;
    uint8_t                 *remote_sdp;
    int                      remote_sdp_size;
    char                    *ephemeral_token;
    char                    *api_token;
    char                    *voice;
    TaskHandle_t             token_task_handle;
    TaskHandle_t             sdp_task_handle;
    bool                     token_ready;
    bool                     sdp_ready;
    char                    *local_sdp;
    int                      local_sdp_size;
} openai_signaling_t;

// Forward declarations
static void openai_sdp_answer(http_resp_t *resp, void *ctx);

static char *get_key_end(char *str, char *key, int len)
{
    char *p = strstr(str, key);
    if (p == NULL) {
        return NULL;
    }
    return p + len;
}

static void session_answer(http_resp_t *resp, void *ctx)
{
    openai_signaling_t *sig = (openai_signaling_t *)ctx;
    char *ephemeral_token = GET_KEY_END((char *)resp->data, "\"value\"");
    if (ephemeral_token == NULL) {
        return;
    }
    char *s = strchr(ephemeral_token, '"');
    if (s == NULL) {
        return;
    }
    s++;
    char *e = strchr(s, '"');
    *e = 0;
    sig->ephemeral_token = strdup(s);
    *e = '"';
}

// Async task to get ephemeral token without blocking
static void get_ephemeral_token_task(void *pvParameters)
{
    openai_signaling_t *sig = (openai_signaling_t *)pvParameters;
    
    ESP_LOGI(TAG, "Starting async ephemeral token request...");
    
    char content_type[32] = "Content-Type: application/json";
    int len = strlen("Authorization: Bearer ") + strlen(sig->api_token) + 1;
    char auth[len];
    snprintf(auth, len, "Authorization: Bearer %s", sig->api_token);
    char *header[] = {
        content_type,
        auth,
        NULL,
    };

    cJSON *root = cJSON_CreateObject();
    cJSON *session = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "session", session);
    cJSON_AddStringToObject(session, "type", "realtime");
    cJSON_AddStringToObject(session, "model", CONFIG_AG_OPENAI_REALTIME_MODEL);

    cJSON *audio = cJSON_CreateObject();
    cJSON_AddItemToObject(session, "audio", audio);

    cJSON *input_audio = cJSON_CreateObject();
    cJSON_AddItemToObject(audio, "input", input_audio);
    cJSON *input_format = cJSON_CreateObject();
    cJSON_AddItemToObject(input_audio, "format", input_format);
    cJSON_AddStringToObject(input_format, "type", "audio/pcm");
    cJSON_AddNumberToObject(input_format, "rate", 24000);
    
    cJSON *output_audio = cJSON_CreateObject();
    cJSON_AddItemToObject(audio, "output", output_audio);
    cJSON *output_format = cJSON_CreateObject();
    cJSON_AddItemToObject(output_audio, "format", output_format);
    cJSON_AddStringToObject(output_format, "type", "audio/pcm");
    cJSON_AddNumberToObject(output_format, "rate", 24000);
    cJSON_AddStringToObject(output_audio, "voice", sig->voice);

    char *json_string = cJSON_Print(root);
    if (json_string) {
        https_post("https://api.openai.com/v1/realtime/client_secrets", header, json_string, NULL, session_answer, sig);
        mem_free(json_string);
    }
    cJSON_Delete(root);
    
    sig->token_ready = true;
    ESP_LOGI(TAG, "Ephemeral token request completed");
    
    // Task auto-delete
    vTaskDelete(NULL);
}

// Async task to send SDP without blocking
static void send_sdp_task(void *pvParameters)
{
    openai_signaling_t *sig = (openai_signaling_t *)pvParameters;
    
    ESP_LOGI(TAG, "Starting async SDP send to OpenAI...");
    
    char content_type[32] = "Content-Type: application/sdp";
    char *token = sig->ephemeral_token;
    int len = strlen("Authorization: Bearer ") + strlen(token) + 1;
    char auth[len];
    snprintf(auth, len, "Authorization: Bearer %s", token);
    char *header[] = {
        content_type,
        auth,
        NULL,
    };

    int ret = https_post( "https://api.openai.com/v1/realtime/calls?model=" CONFIG_AG_OPENAI_REALTIME_MODEL, header, sig->local_sdp, NULL, openai_sdp_answer, sig);
    if (ret != 0 || sig->remote_sdp == NULL) {
        ESP_LOGD(TAG, "Failed to post SDP to OpenAI");
        sig->sdp_ready = false;
    } else {
        esp_peer_signaling_msg_t sdp_msg = {
            .type = ESP_PEER_SIGNALING_MSG_SDP,
            .data = sig->remote_sdp,
            .size = sig->remote_sdp_size,
        };
        sig->cfg.on_msg(&sdp_msg, sig->cfg.ctx);
        sig->sdp_ready = true;
        ESP_LOGI(TAG, "SDP exchange completed successfully");
    }
    
    // Cleanup
    SAFE_FREE(sig->local_sdp);
    
    // Task auto-delete
    vTaskDelete(NULL);
}

static void get_ephemeral_token(openai_signaling_t *sig, char *token, char *voice)
{
    // Store parameters for async task
    sig->api_token = strdup(token);
    sig->voice = strdup(voice ? voice : CONFIG_AG_OPENAI_VOICE);
    sig->token_ready = false;
    
    // Create async task that won't block audio
    BaseType_t ret = xTaskCreate(
        get_ephemeral_token_task,
        "get_token_task",
        8192,  // Larger stack for HTTPS
        sig,
        3,     // Lower priority than audio feedback
        &sig->token_task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGD(TAG, "Failed to create ephemeral token task");
        // Fallback to blocking call
        get_ephemeral_token_task(sig);
    } else {
        ESP_LOGI(TAG, "Ephemeral token task created - non-blocking");
    }
}

static int openai_signaling_start(esp_peer_signaling_cfg_t *cfg, esp_peer_signaling_handle_t *h)
{
    openai_signaling_t *sig = (openai_signaling_t *)mem_calloc(1, sizeof(openai_signaling_t), MEM_POLICY_REQUIRE_INTERNAL, "openai_sig");
    if (sig == NULL) {
        return ESP_PEER_ERR_NO_MEM;
    }
    openai_signaling_cfg_t *openai_cfg = (openai_signaling_cfg_t *)cfg->extra_cfg;
    sig->cfg = *cfg;
    
    // Start ephemeral token request asynchronously (non-blocking)
    get_ephemeral_token(sig, openai_cfg->token, openai_cfg->voice ? openai_cfg->voice : CONFIG_AG_OPENAI_VOICE);
    
    // Don't wait for token - continue immediately to avoid blocking audio
    *h = sig;
    esp_peer_signaling_ice_info_t ice_info = {
        .is_initiator = true,
    };
    sig->cfg.on_ice_info(&ice_info, sig->cfg.ctx);
    sig->cfg.on_connected(sig->cfg.ctx);
    
    ESP_LOGI(TAG, "OpenAI signaling started (token request in background)");
    return ESP_PEER_ERR_NONE;
}

static void openai_sdp_answer(http_resp_t *resp, void *ctx)
{
    openai_signaling_t *sig = (openai_signaling_t *)ctx;
    ESP_LOGI(TAG, "Got remote SDP (%d bytes)", resp->size);
    SAFE_FREE(sig->remote_sdp);
    sig->remote_sdp = (uint8_t *)mem_alloc(resp->size, MEM_POLICY_PREFER_PSRAM, "remote_sdp");
    if (sig->remote_sdp == NULL) {
        ESP_LOGE(TAG, "No enough memory for remote sdp");
        return;
    }
    memcpy(sig->remote_sdp, resp->data, resp->size);
    sig->remote_sdp_size = resp->size;
}

static int openai_signaling_send_msg(esp_peer_signaling_handle_t h, esp_peer_signaling_msg_t *msg)
{
    openai_signaling_t *sig = (openai_signaling_t *)h;
    if (msg->type == ESP_PEER_SIGNALING_MSG_BYE) {
        ESP_LOGI(TAG, "Received BYE message");
    } else if (msg->type == ESP_PEER_SIGNALING_MSG_SDP) {
        ESP_LOGI(TAG, "Sending local SDP to OpenAI");
        
        // Wait for ephemeral token to be ready (non-blocking check with timeout)
        int timeout_ms = 10000; // 10 second timeout
        while (!sig->token_ready && timeout_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(500));
            timeout_ms -= 500;
        }
        
        if (!sig->token_ready || sig->ephemeral_token == NULL) {
            ESP_LOGD(TAG, "Ephemeral token not ready after timeout");
            return -1;
        }
        
        // Store SDP data for async task
        sig->local_sdp_size = msg->size;
        sig->local_sdp = mem_alloc(msg->size + 1, MEM_POLICY_PREFER_PSRAM, "local_sdp");
        if (!sig->local_sdp) {
            ESP_LOGD(TAG, "Failed to allocate memory for local SDP");
            return -1;
        }
        memcpy(sig->local_sdp, msg->data, msg->size);
        sig->local_sdp[msg->size] = '\0';
        
        // Create async SDP send task to avoid blocking audio
        BaseType_t ret = xTaskCreate(
            send_sdp_task,
            "send_sdp_task",
            8192,  // Larger stack for HTTPS
            sig,
            3,     // Lower priority than audio feedback
            &sig->sdp_task_handle
        );
        
        if (ret != pdPASS) {
            ESP_LOGD(TAG, "Failed to create SDP send task");
            SAFE_FREE(sig->local_sdp);
            return -1;
        }
        
        ESP_LOGI(TAG, "SDP send task created - non-blocking");
    }
    return 0;
}

static int openai_signaling_stop(esp_peer_signaling_handle_t h)
{
    openai_signaling_t *sig = (openai_signaling_t *)h;
    sig->cfg.on_close(sig->cfg.ctx);
    
    // Stop async token task if still running
    if (sig->token_task_handle) {
        vTaskDelete(sig->token_task_handle);
        sig->token_task_handle = NULL;
    }
    
    // Stop async SDP task if still running
    if (sig->sdp_task_handle) {
        vTaskDelete(sig->sdp_task_handle);
        sig->sdp_task_handle = NULL;
    }
    
    SAFE_FREE(sig->remote_sdp);
    SAFE_FREE(sig->ephemeral_token);
    SAFE_FREE(sig->api_token);
    SAFE_FREE(sig->voice);
    SAFE_FREE(sig->local_sdp);
    SAFE_FREE(sig);
    return 0;
}

const esp_peer_signaling_impl_t *openai_signaling_get_impl(void)
{
    static const esp_peer_signaling_impl_t impl = {
        .start = openai_signaling_start,
        .send_msg = openai_signaling_send_msg,
        .stop = openai_signaling_stop,
    };
    return &impl;
}