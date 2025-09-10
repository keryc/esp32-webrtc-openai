#include "openai_client.h"
#include <esp_log.h>
#include <string.h>
#include <stdlib.h>
#include "esp_capture.h"
#include "av_render.h"
#include "esp_webrtc.h"
#include "esp_webrtc_defaults.h"
#include "esp_peer_default.h"
#include "sdkconfig.h"
#include "audio_module.h"
#include "audio_feedback.h"
#include "providers/openai/openai_signaling.h"
#include "camera_module.h"
#include "memory_manager.h"
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "openai_signaling.h"
#include "prompts.h"
#include "mbedtls/base64.h"
#include "esp_camera.h"

static const char *TAG = "openai_webrtc";

// WebRTC handle
static esp_webrtc_handle_t webrtc = NULL;

// Audio state management
static struct {
    bool audio_paused;
} audio_state = {0};

// Response tracking to prevent concurrent responses
static struct {
    bool response_in_progress;
    char active_response_id[64];
    SemaphoreHandle_t mutex;
} response_state = {0};

// Function call system types (migrado de webrtc.c original)
typedef struct attribute_t attribute_t;
typedef struct class_t class_t;
typedef enum {
    ATTRIBUTE_TYPE_NONE,
    ATTRIBUTE_TYPE_BOOL,
    ATTRIBUTE_TYPE_INT,
    ATTRIBUTE_TYPE_STRING,
    ATTRIBUTE_TYPE_PARENT,
} attribute_type_t;

struct attribute_t {
    char *name;
    char *desc;
    attribute_type_t type;
    union {
        bool b_state;
        int i_value;
        char *s_value;
        attribute_t *attr_list;
    };
    int attr_num;
    bool required;
    char *call_id;
    int (*control)(attribute_t *attr);
};

struct class_t {
    char *name;
    char *desc;
    attribute_t *attr_list;
    int attr_num;
    class_t *next;
};

static class_t *classes = NULL;

// Forward declaration
static void send_vision_result_to_openai(const char *analysis_result, const char *call_id);

// New function for direct image sending via WebRTC Realtime API
static void send_images_to_realtime(char **base64_images, int image_count, const char *text_prompt);

// Simplified function to send vision analysis result
static void send_vision_result_to_openai(const char *analysis_result, const char *call_id)
{
    if (!analysis_result || !webrtc) {
        ESP_LOGE(TAG, "Visual analysis could not be obtained");
        return;
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "type", "conversation.item.create");
    
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "type", "function_call_output");
    cJSON_AddStringToObject(item, "call_id", call_id ? call_id : "unknown_call");
    cJSON_AddStringToObject(item, "output", analysis_result);
    cJSON_AddItemToObject(response, "item", item);
    
    char *json_string = cJSON_Print(response);
    if (json_string) {
        esp_webrtc_send_custom_data(webrtc, ESP_WEBRTC_CUSTOM_DATA_VIA_DATA_CHANNEL, 
                                   (uint8_t *)json_string, strlen(json_string));
        mem_free(json_string);
        
        // Trigger a response after sending function output
        cJSON *create_response = cJSON_CreateObject();
        cJSON_AddStringToObject(create_response, "type", "response.create");
        char *create_json = cJSON_Print(create_response);
        if (create_json) {
            esp_webrtc_send_custom_data(webrtc, ESP_WEBRTC_CUSTOM_DATA_VIA_DATA_CHANNEL, 
                                       (uint8_t *)create_json, strlen(create_json));
            mem_free(create_json);
        }
        cJSON_Delete(create_response);
    }
    cJSON_Delete(response);
}

// New implementation for sending multiple images directly via WebRTC Realtime API
static void send_images_to_realtime(char **base64_images, int image_count, const char *text_prompt)
{
    if (!webrtc || !base64_images || image_count <= 0) {
        ESP_LOGE(TAG, "Invalid parameters for realtime image sending");
        return;
    }
    
    ESP_LOGI(TAG, "📷 Sending %d images directly via WebRTC Realtime API", image_count);
    
    // Create the conversation.item.create message
    cJSON *message = cJSON_CreateObject();
    if (!message) {
        ESP_LOGE(TAG, "Failed to create JSON message");
        return;
    }
    
    cJSON_AddStringToObject(message, "type", "conversation.item.create");
    
    // Create the item
    cJSON *item = cJSON_CreateObject();
    if (!item) {
        cJSON_Delete(message);
        ESP_LOGE(TAG, "Failed to create JSON item");
        return;
    }
    
    cJSON_AddStringToObject(item, "type", "message");
    cJSON_AddStringToObject(item, "role", "user");
    
    // Create content array with text prompt and images
    cJSON *content = cJSON_CreateArray();
    if (!content) {
        cJSON_Delete(message);
        cJSON_Delete(item);
        ESP_LOGE(TAG, "Failed to create content array");
        return;
    }
    
    // Add text prompt if provided
    if (text_prompt && strlen(text_prompt) > 0) {
        cJSON *text_content = cJSON_CreateObject();
        cJSON_AddStringToObject(text_content, "type", "input_text");
        cJSON_AddStringToObject(text_content, "text", text_prompt);
        cJSON_AddItemToArray(content, text_content);
        ESP_LOGI(TAG, "Added text prompt: %.100s...", text_prompt);
    }
    
    // Add all images
    for (int i = 0; i < image_count; i++) {
        if (!base64_images[i]) {
            ESP_LOGW(TAG, "Skipping NULL image at index %d", i);
            continue;
        }
        
        cJSON *image_content = cJSON_CreateObject();
        cJSON_AddStringToObject(image_content, "type", "input_image");
        
        // Create data URL with proper format
        size_t url_size = strlen("data:image/jpeg;base64,") + strlen(base64_images[i]) + 1;
        char *image_url = mem_alloc(url_size, MEM_POLICY_PREFER_PSRAM, "image_url");
        if (!image_url) {
            ESP_LOGW(TAG, "Failed to allocate memory for image %d URL", i);
            cJSON_Delete(image_content);
            continue;
        }
        
        snprintf(image_url, url_size, "data:image/jpeg;base64,%s", base64_images[i]);
        cJSON_AddStringToObject(image_content, "image_url", image_url);
        mem_free(image_url);
        
        cJSON_AddItemToArray(content, image_content);
        ESP_LOGI(TAG, "✅ Added image %d/%d (size: %zu bytes)", i+1, image_count, strlen(base64_images[i]));
    }
    
    // Add content to item
    cJSON_AddItemToObject(item, "content", content);
    
    // Add item to message
    cJSON_AddItemToObject(message, "item", item);
    
    // Convert to string and send
    char *json_string = cJSON_PrintUnformatted(message);
    cJSON_Delete(message);
    
    if (json_string) {
        size_t json_len = strlen(json_string);
        ESP_LOGI(TAG, "📤 Sending message with %d images (total size: %zu bytes)", image_count, json_len);
        
        esp_err_t ret = esp_webrtc_send_custom_data(webrtc, ESP_WEBRTC_CUSTOM_DATA_VIA_DATA_CHANNEL,
                                                    (uint8_t *)json_string, json_len);
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send images: %s", esp_err_to_name(ret));
        }

        mem_free(json_string);
    } else {
        ESP_LOGE(TAG, "Failed to serialize JSON message");
    }
}

// Structure to pass data to async task
typedef struct {
    char *context;
    char *call_id;
    int max_frames;
} vision_task_params_t;

// Async task to handle vision analysis
static void vision_analysis_task(void *pvParameters)
{
    vision_task_params_t *params = (vision_task_params_t *)pvParameters;
    
    ESP_LOGI(TAG, "📸 Capturing %d frames on-demand...", params->max_frames);
    
    // Get frames on-demand (battery efficient)
    int frame_count = 0;
    char **base64_frames = cam_module_get_vision_frames(params->max_frames, &frame_count);
    
    if (!base64_frames || frame_count == 0) {
        ESP_LOGW(TAG, "No frames captured, trying single frame capture");
        
        // Fallback to single frame capture directly from camera
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Failed to get frame for analysis");
            send_vision_result_to_openai("Error: Could not capture image for analysis", params->call_id);
            goto cleanup;
        }
        
        // Encode single frame to base64
        size_t encoded_size = 0;
        mbedtls_base64_encode(NULL, 0, &encoded_size, fb->buf, fb->len);
        char *single_base64 = mem_alloc(encoded_size + 1, MEM_POLICY_PREFER_PSRAM, "single_base64");
        if (!single_base64) {
            esp_camera_fb_return(fb);
            ESP_LOGE(TAG, "Failed to allocate base64 buffer");
            send_vision_result_to_openai("Error: Could not capture image for analysis", params->call_id);
            goto cleanup;
        }
        
        size_t actual_size = 0;
        mbedtls_base64_encode((unsigned char*)single_base64, encoded_size, &actual_size, fb->buf, fb->len);
        single_base64[actual_size] = '\0';
        esp_camera_fb_return(fb);
        
        // Create array with single frame
        base64_frames = mem_alloc(sizeof(char*), MEM_POLICY_PREFER_PSRAM, "frame_array");
        if (base64_frames) {
            base64_frames[0] = single_base64;
            frame_count = 1;
        } else {
            mem_free(single_base64);
            send_vision_result_to_openai("Error: Could not capture image for analysis", params->call_id);
            goto cleanup;
        }
    }
    
    ESP_LOGI(TAG, "📷 Got %d/%d frames ready for Realtime API streaming", frame_count, params->max_frames);
    
    // Create a comprehensive prompt that combines the original request
    char *combined_prompt = mem_alloc(2048, MEM_POLICY_PREFER_PSRAM, "combined_prompt");
    if (!combined_prompt) {
        // Free frames
        for (int i = 0; i < frame_count; i++) {
            if (base64_frames[i]) mem_free(base64_frames[i]);
        }
        mem_free(base64_frames);
        send_vision_result_to_openai("Error: Could not capture image for analysis", params->call_id);
        goto cleanup;
    }
    
    snprintf(combined_prompt, 2048,
            "Analyze these %d images of the environment. %s\n"
            "Provide a clear and concise answer",
            frame_count, params->context);
    
    // Send images directly via WebRTC Realtime API
    ESP_LOGI(TAG, "🚀 Sending %d images directly to OpenAI Realtime API!", frame_count);
    send_images_to_realtime(base64_frames, frame_count, combined_prompt);
    
    // Clean up
    mem_free(combined_prompt);
    for (int i = 0; i < frame_count; i++) {
        if (base64_frames[i]) mem_free(base64_frames[i]);
    }
    mem_free(base64_frames);
    
    // Send immediate acknowledgment via function call output
    char ack_message[512];
    snprintf(ack_message, sizeof(ack_message),
            "Processing %d environment images. Analyzing: %s",
            frame_count, params->context);
    send_vision_result_to_openai(ack_message, params->call_id);
    
    ESP_LOGI(TAG, "✅ Vision analysis request completed");

cleanup:
    // Free parameters
    if (params->context) mem_free(params->context);
    if (params->call_id) mem_free(params->call_id);
    mem_free(params);
    
    // Delete this task
    vTaskDelete(NULL);
}

static int handle_visual_analysis(attribute_t *attr)
{
    const char *context = attr->s_value ? attr->s_value : "Analyze what you see!";
    const char *call_id = attr->call_id ? attr->call_id : "unknown_call";
    
    ESP_LOGI(TAG, "🎯 Vision analysis requested: %s", context);
    
    // Prepare parameters for async task
    vision_task_params_t *params = mem_alloc(sizeof(vision_task_params_t), MEM_POLICY_PREFER_PSRAM, "vision_params");
    if (!params) {
        ESP_LOGE(TAG, "Failed to allocate vision task parameters");
        send_vision_result_to_openai("Error: Memory allocation failed", call_id);
        return 0;
    }
    
    // Duplicate strings for the async task
    params->context = mem_alloc(strlen(context) + 1, MEM_POLICY_PREFER_PSRAM, "vision_context");
    if (params->context) strcpy(params->context, context);
    
    params->call_id = mem_alloc(strlen(call_id) + 1, MEM_POLICY_PREFER_PSRAM, "vision_callid");
    if (params->call_id) strcpy(params->call_id, call_id);
    params->max_frames = CONFIG_AG_VISION_REALTIME_FRAMES_COUNT;
    
    // Create async task with lower priority to avoid audio disruption
    BaseType_t ret = xTaskCreate(
        vision_analysis_task,           // Task function
        "vision_analysis",              // Task name
        8192,                           // Stack size (increased for image processing)
        params,                         // Parameters
        tskIDLE_PRIORITY + 1,           // Low priority to not interfere with audio
        NULL                            // Task handle (not needed)
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create vision analysis task");
        if (params->context) mem_free(params->context);
        if (params->call_id) mem_free(params->call_id);
        mem_free(params);
        send_vision_result_to_openai("Error: Failed to start vision analysis", call_id);
        return 0;
    }
    
    ESP_LOGI(TAG, "Vision analysis task started asynchronously");
    return 0;
}

// Build vision class (migrado de webrtc.c original)
static class_t *build_vision_class(void)
{
    class_t *vision = (class_t *)mem_calloc(1, sizeof(class_t), MEM_POLICY_PREFER_PSRAM, "openai_vision");
    if (vision == NULL) {
        return NULL;
    }
    static attribute_t vision_attrs[] = {
        {
            .name = VISION_PARAM_NAME,
            .desc = VISION_PARAM_DESCRIPTION,
            .type = ATTRIBUTE_TYPE_STRING,
            .control = handle_visual_analysis,
            .required = true,
        },
    };
    vision->name = VISION_FUNCTION_NAME;
    vision->desc = VISION_FUNCTION_DESCRIPTION;
    vision->attr_list = vision_attrs;
    vision->attr_num = 1; // sizeof(vision_attrs) / sizeof(vision_attrs[0])
    return vision;
}

static void add_class(class_t *cls)
{
    if (classes == NULL) {
        classes = cls;
    } else {
        classes->next = cls;
    }
}

static int build_classes(void)
{
    static bool build_once = false;
    if (build_once) {
        return 0;
    }
    add_class(build_vision_class());
    build_once = true;
    return 0;
}

// Helper functions for JSON processing (migradas de webrtc.c original)
static char *get_attr_type(attribute_type_t type)
{
    if (type == ATTRIBUTE_TYPE_BOOL) return "boolean";
    if (type == ATTRIBUTE_TYPE_INT) return "integer";
    if (type == ATTRIBUTE_TYPE_STRING) return "string";
    if (type == ATTRIBUTE_TYPE_PARENT) return "object";
    return "";
}

// Send function descriptions to OpenAI (migrado de webrtc.c original)
static int send_function_desc(bool vision_enabled)
{
    if (classes == NULL || webrtc == NULL) {
        return 0;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "session.update");
    cJSON *session = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "session", session);

    cJSON_AddStringToObject(session, "type", "realtime");
    // Always use vision instructions now - audio-only mode removed
    cJSON_AddStringToObject(session, "instructions", INSTRUCTIONS_AUDIO_VISION);
    
    cJSON *tools = cJSON_CreateArray();
    cJSON_AddItemToObject(session, "tools", tools);
    cJSON_AddStringToObject(session, "tool_choice", CONFIG_AG_OPENAI_TOOL_CHOICE);

    class_t *iter = classes;
    while (iter) {
        // Always register vision functions - audio-only mode removed
        if (strcmp(iter->name, "look_around") == 0) {
            cJSON *tool = cJSON_CreateObject();
            cJSON_AddItemToArray(tools, tool);
            cJSON_AddStringToObject(tool, "type", "function");
            cJSON_AddStringToObject(tool, "name", iter->name);
            cJSON_AddStringToObject(tool, "description", iter->desc);
            cJSON *parameters = cJSON_CreateObject();
            cJSON_AddItemToObject(tool, "parameters", parameters);
            cJSON_AddStringToObject(parameters, "type", "object");
            cJSON *properties = cJSON_CreateObject();
            cJSON_AddItemToObject(parameters, "properties", properties);
                
            for (int i = 0; i < iter->attr_num; i++) {
                attribute_t *attr = &iter->attr_list[i];
                cJSON *prop = cJSON_CreateObject();
                cJSON_AddItemToObject(properties, attr->name, prop);
                cJSON_AddStringToObject(prop, "type", get_attr_type(attr->type));
                cJSON_AddStringToObject(prop, "description", attr->desc);
            }
        }
        iter = iter->next;
    }
    
    char *json_string = cJSON_Print(root);
    if (json_string) {
        esp_webrtc_send_custom_data(webrtc, ESP_WEBRTC_CUSTOM_DATA_VIA_DATA_CHANNEL, (uint8_t *)json_string, strlen(json_string));
        mem_free(json_string);
    }
    cJSON_Delete(root);
    return 0;
}

// WebRTC event handler - improved based on WebRTC documentation
static int webrtc_event_handler(esp_webrtc_event_t *event, void *ctx)
{
    ESP_LOGI(TAG, "WebRTC Event: %d", event->type);
    
    if (event->type == ESP_WEBRTC_EVENT_DATA_CHANNEL_CONNECTED) {
        ESP_LOGI(TAG, "Data channel connected, creating oai-events channel");
        
        // Create data channel with proper label for OpenAI events
        // Per the WebRTC API docs, OpenAI uses "oai-events" for event communication
        esp_peer_data_channel_cfg_t cfg = {
            .label = "oai-events",  // OpenAI standard data channel name
        };
        esp_peer_handle_t peer_handle = NULL;
        esp_webrtc_get_peer_connection(webrtc, &peer_handle);
        esp_peer_create_data_channel(peer_handle, &cfg);
    }
    else if (event->type == ESP_WEBRTC_EVENT_DATA_CHANNEL_OPENED) {
        ESP_LOGI(TAG, "Data channel opened - sending initial configuration");
        
        // Send session update with configuration (always with vision enabled)
        send_function_desc(true);
        
        // According to WebRTC docs, we can send response.create to trigger initial response
        cJSON *response_create = cJSON_CreateObject();
        cJSON_AddStringToObject(response_create, "type", "response.create");
        
        // Add optional response configuration for consistency
        cJSON *response = cJSON_CreateObject();
        cJSON_AddNullToObject(response, "instructions");  // Use session instructions
        cJSON_AddItemToObject(response_create, "response", response);
        
        char *create_json = cJSON_PrintUnformatted(response_create);  // Use unformatted for efficiency
        if (create_json) {
            ESP_LOGI(TAG, "Sending response.create to trigger initial greeting");
            esp_webrtc_send_custom_data(webrtc, ESP_WEBRTC_CUSTOM_DATA_VIA_DATA_CHANNEL, 
                                      (uint8_t *)create_json, strlen(create_json));
            mem_free(create_json);
        }
        cJSON_Delete(response_create);
        
        ESP_LOGI(TAG, "✅ Fully operational. Ready to receive commands.");
    }
    else if (event->type == ESP_WEBRTC_EVENT_CONNECT_FAILED || 
             event->type == ESP_WEBRTC_EVENT_DATA_CHANNEL_CLOSED) {
        ESP_LOGW(TAG, "WebRTC connection issue: event %d", event->type);
    }
    else {
        ESP_LOGD(TAG, "WebRTC event: %d", event->type);
    }
    
    return 0;
}

// Process JSON functions - improved error handling
static int match_and_execute(cJSON *cur, attribute_t *attr, const char *call_id)
{
    cJSON *attr_value = cJSON_GetObjectItemCaseSensitive(cur, attr->name);
    if (!attr_value) {
        if (attr->required) {
            ESP_LOGW(TAG, "Missing required attribute: %s", attr->name);
            // Send error response back to OpenAI for missing required parameters
            if (call_id) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), "Error: Missing required parameter '%s'", attr->name);
                send_vision_result_to_openai(error_msg, call_id);
            }
        }
        return 0;
    }

    // Store call_id for function responses
    attr->call_id = (char *)call_id;

    // Process based on attribute type
    if (attr->type == ATTRIBUTE_TYPE_BOOL && cJSON_IsBool(attr_value)) {
        attr->b_state = cJSON_IsTrue(attr_value);
        if (attr->control) {
            attr->control(attr);
        }
    } else if (attr->type == ATTRIBUTE_TYPE_INT && cJSON_IsNumber(attr_value)) {
        attr->i_value = attr_value->valueint;
        if (attr->control) {
            attr->control(attr);
        }
    } else if (attr->type == ATTRIBUTE_TYPE_STRING && cJSON_IsString(attr_value)) {
        attr->s_value = attr_value->valuestring;
        if (attr->control) {
            attr->control(attr);
        }
    } else if (attr->type == ATTRIBUTE_TYPE_PARENT && cJSON_IsObject(attr_value)) {
        // Process nested attributes
        for (int j = 0; j < attr->attr_num; j++) {
            attribute_t *sub_attr = &attr->attr_list[j];
            match_and_execute(attr_value, sub_attr, call_id);
        }
    }
    return 1;
}

static int process_json(const char *json_data)
{
    cJSON *root = cJSON_Parse(json_data);
    if (!root) {
        ESP_LOGE(TAG, "Error parsing JSON data");
        return -1;
    }

    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type) || strcmp(type->valuestring, "response.function_call_arguments.done") != 0) {
        cJSON_Delete(root);
        return 0;
    }
    
    ESP_LOGI(TAG, "Processing function call");
    
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "name");
    if (name && cJSON_IsString(name)) {
        ESP_LOGI(TAG, "Function detected: %s", name->valuestring);
    }
    const cJSON *arguments = cJSON_GetObjectItemCaseSensitive(root, "arguments");
    const cJSON *call_id = cJSON_GetObjectItemCaseSensitive(root, "call_id");
    
    if (!cJSON_IsString(name) || !name->valuestring || !cJSON_IsString(arguments) || !arguments->valuestring) {
        ESP_LOGE(TAG, "Invalid function call format");
        cJSON_Delete(root);
        return -1;
    }

    const char *call_id_str = (call_id && cJSON_IsString(call_id)) ? call_id->valuestring : "unknown_call";

    cJSON *args_root = cJSON_Parse(arguments->valuestring);
    if (!args_root) {
        ESP_LOGE(TAG, "Error parsing function arguments");
        cJSON_Delete(root);
        return -1;
    }

    // Find the corresponding class and attributes
    class_t *iter = classes;
    bool function_found = false;
    while (iter) {
        if (strcmp(iter->name, name->valuestring) == 0) {
            ESP_LOGI(TAG, "Executing function: %s", name->valuestring);
            function_found = true;
            for (int i = 0; i < iter->attr_num; i++) {
                attribute_t *attr = &iter->attr_list[i];
                match_and_execute(args_root, attr, call_id_str);
            }
        }
        iter = iter->next;
    }
    
    if (!function_found) {
        ESP_LOGE(TAG, "Function '%s' not found", name->valuestring);
    }

    cJSON_Delete(args_root);
    cJSON_Delete(root);
    return 0;
}

// WebRTC data handler - optimized for real-time processing
static int webrtc_data_handler(esp_webrtc_custom_data_via_t via, uint8_t *data, int size, void *ctx)
{
    // Validate input parameters
    if (!data || size <= 0) {
        ESP_LOGW(TAG, "Invalid data received: size=%d", size);
        return -1;
    }
    
    // Only process data channel messages
    if (via == ESP_WEBRTC_CUSTOM_DATA_VIA_DATA_CHANNEL) {
        // Log only in debug mode to avoid audio interruption
#if defined(CONFIG_AG_WEBRTC_DEBUG_LOGS) && CONFIG_AG_WEBRTC_DEBUG_LOGS
        ESP_LOGD(TAG, "Data received via DataChannel (%d bytes)", size);
#endif
        
        // Null-terminate the data for safe string operations
        char *json_str = mem_alloc(size + 1, MEM_POLICY_PREFER_PSRAM, "json_buffer");
        if (json_str) {
            memcpy(json_str, data, size);
            json_str[size] = '\0';
            
            // Only log non-transcript messages to avoid audio interference
#if defined(CONFIG_AG_WEBRTC_DEBUG_LOGS) && CONFIG_AG_WEBRTC_DEBUG_LOGS
            cJSON *root = cJSON_Parse(json_str);
            if (root) {
                cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
                if (type && cJSON_IsString(type) && strcmp(type->valuestring, "response.audio_transcript.delta") != 0) {
                    ESP_LOGD(TAG, "Received: %.300s%s", json_str, size > 300 ? "..." : "");
                }
                cJSON_Delete(root);
            }
#endif
            
            // Process function calls
            process_json(json_str);
            
            mem_free(json_str);
        }
    }
    
    // Parse and handle different message types - optimized parsing
    cJSON *root = cJSON_ParseWithLength((const char *)data, size);  // Use length-aware parsing
    if (root) {
        cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
        if (type && cJSON_IsString(type)) {
            const char *type_str = type->valuestring;
            
            // Handle different response types
            if (strcmp(type_str, "response.audio_transcript.delta") == 0) {
                cJSON *delta = cJSON_GetObjectItemCaseSensitive(root, "delta");
                if (delta && cJSON_IsString(delta)) {
#if defined(CONFIG_AG_TRANSCRIPT_LOGGING) && CONFIG_AG_TRANSCRIPT_LOGGING
                    // Use lightweight printf without JARVIS prefix to reduce processing
                    printf("%s", delta->valuestring);
                    fflush(stdout);
#endif
                }
            }
            else if (strcmp(type_str, "response.text.delta") == 0) {
                cJSON *delta = cJSON_GetObjectItemCaseSensitive(root, "delta");
                if (delta && cJSON_IsString(delta)) {
                    printf("%s", delta->valuestring);
                    fflush(stdout);
                }
            }
            else if (strcmp(type_str, "response.text.done") == 0 || 
                     strcmp(type_str, "response.audio_transcript.done") == 0) {
                printf("\n");
                fflush(stdout);
            }
            else if (strcmp(type_str, "response.done") == 0) {
                ESP_LOGI(TAG, "Response completed");
                // Clear active response
                if (response_state.mutex && xSemaphoreTake(response_state.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    response_state.response_in_progress = false;
                    response_state.active_response_id[0] = '\0';
                    xSemaphoreGive(response_state.mutex);
                }
            }
            else if (strcmp(type_str, "conversation.item.created") == 0) {
                ESP_LOGI(TAG, "Conversation item created");
            }
            else if (strcmp(type_str, "response.created") == 0) {
                ESP_LOGI(TAG, "Response generation started");
                // Track active response with improved tracking
                if (response_state.mutex && xSemaphoreTake(response_state.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    response_state.response_in_progress = true;
                    cJSON *response_obj = cJSON_GetObjectItemCaseSensitive(root, "response");
                    if (response_obj) {
                        cJSON *id = cJSON_GetObjectItemCaseSensitive(response_obj, "id");
                        if (id && cJSON_IsString(id)) {
                            strncpy(response_state.active_response_id, id->valuestring, sizeof(response_state.active_response_id) - 1);
                            response_state.active_response_id[sizeof(response_state.active_response_id) - 1] = '\0';
                        }
                    }
                    xSemaphoreGive(response_state.mutex);
                }
            }
            else if (strcmp(type_str, "error") == 0) {
                cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
                if (error) {
                    // Extract error details for better debugging
                    cJSON *code = cJSON_GetObjectItemCaseSensitive(error, "code");
                    cJSON *message = cJSON_GetObjectItemCaseSensitive(error, "message");
                    cJSON *param = cJSON_GetObjectItemCaseSensitive(error, "param");
                    
                    ESP_LOGE(TAG, "OpenAI Error - Code: %s, Message: %s, Param: %s",
                            code && cJSON_IsString(code) ? code->valuestring : "unknown",
                            message && cJSON_IsString(message) ? message->valuestring : "unknown",
                            param && cJSON_IsString(param) ? param->valuestring : "none");
                    
                    printf("❌ Error: %s\n", 
                           message && cJSON_IsString(message) ? message->valuestring : "Unknown error");
                    
                    // Handle specific error codes
                    if (code && cJSON_IsString(code)) {
                        if (strcmp(code->valuestring, "rate_limit_exceeded") == 0) {
                            ESP_LOGW(TAG, "Rate limit hit - implementing backoff");
                            vTaskDelay(pdMS_TO_TICKS(2000));  // 2 second backoff
                        } else if (strcmp(code->valuestring, "invalid_api_key") == 0) {
                            ESP_LOGE(TAG, "Invalid API key - check configuration");
                        }
                    }
                }
            }
            else if (strcmp(type_str, "session.created") == 0) {
                ESP_LOGI(TAG, "Session created successfully");
                // Session is ready - we can now configure it with our tools
                send_function_desc(true);
            }
            else if (strcmp(type_str, "session.updated") == 0) {
                ESP_LOGI(TAG, "Session configuration updated");
            }
            else if (strcmp(type_str, "input_audio_buffer.speech_started") == 0) {
                ESP_LOGD(TAG, "Speech detected - user is speaking");
            }
            else if (strcmp(type_str, "input_audio_buffer.speech_stopped") == 0) {
                ESP_LOGD(TAG, "Speech stopped - processing audio");
            }
            else if (strcmp(type_str, "response.audio.delta") == 0) {
                // Audio data is being received - handled by WebRTC automatically
            }
            else if (strcmp(type_str, "response.audio.done") == 0) {
                ESP_LOGD(TAG, "Audio response completed");
            }
            else {
                ESP_LOGD(TAG, "Unhandled message type: %s", type_str);
            }
        }
        
        cJSON_Delete(root);
    }
    
    return 0;
}

esp_err_t openai_realtime_start(void)
{
    ESP_LOGI(TAG, "Starting OpenAI WebRTC session");
    
    // Initialize response state mutex if not already created
    if (!response_state.mutex) {
        response_state.mutex = xSemaphoreCreateMutex();
        if (!response_state.mutex) {
            ESP_LOGE(TAG, "Failed to create response state mutex");
            return ESP_FAIL;
        }
    }
    
    build_classes();
    
    if (webrtc) {
        esp_webrtc_close(webrtc);
        webrtc = NULL;
    }
    
    esp_peer_default_cfg_t peer_cfg = {
        .agent_recv_timeout = 2000,
    };
    openai_signaling_cfg_t openai_cfg = {
        .token = OPENAI_API_KEY,
    };
    
    esp_webrtc_cfg_t cfg = {
        .peer_cfg = {
            .audio_info = {
#ifdef CONFIG_AG_WEBRTC_SUPPORT_OPUS
                .codec = ESP_PEER_AUDIO_CODEC_OPUS,
                .sample_rate = 24000,  // OpenAI Realtime API requirement
                .channel = 1,           // Mono audio for efficiency
#else
                .codec = ESP_PEER_AUDIO_CODEC_G711A,
                .sample_rate = 8000,    // G711 standard rate
                .channel = 1,
#endif
            },
            .audio_dir = ESP_PEER_MEDIA_DIR_SEND_RECV,
            .enable_data_channel = true,  // Always enable for events
            .on_custom_data = webrtc_data_handler,
            .manual_ch_create = true,  // Manual channel creation for oai-events
            .extra_cfg = &peer_cfg,
            .extra_size = sizeof(peer_cfg),
        },
        .signaling_cfg.extra_cfg = &openai_cfg,
        .signaling_cfg.extra_size = sizeof(openai_cfg),
        .peer_impl = esp_peer_get_default_impl(),
        .signaling_impl = openai_signaling_get_impl(),
    };
    
    int ret = esp_webrtc_open(&cfg, &webrtc);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to open WebRTC: %d", ret);
        return ESP_FAIL;
    }
    
    // Set media provider from audio module
    esp_webrtc_media_provider_t media_provider = {};
    esp_err_t provider_ret = audio_module_get_media_provider(&media_provider);
    if (provider_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get media provider from audio module: %s", esp_err_to_name(provider_ret));
        esp_webrtc_close(webrtc);
        webrtc = NULL;
        return provider_ret;
    }

    esp_webrtc_set_media_provider(webrtc, &media_provider);

    // Set event handler
    esp_webrtc_set_event_handler(webrtc, webrtc_event_handler, NULL);

    // Start WebRTC
    ret = esp_webrtc_start(webrtc);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to start WebRTC: %d", ret);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "OpenAI WebRTC started successfully");
    return ESP_OK;
}

esp_err_t openai_realtime_stop(void)
{
    ESP_LOGI(TAG, "Stopping OpenAI WebRTC session");
    
    if (webrtc) {
        esp_webrtc_handle_t handle = webrtc;
        webrtc = NULL;
        esp_webrtc_close(handle);
    }
    
    // Clean up response tracking state
    if (response_state.mutex) {
        if (xSemaphoreTake(response_state.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            response_state.response_in_progress = false;
            response_state.active_response_id[0] = '\0';
            xSemaphoreGive(response_state.mutex);
        }
    }
    
    ESP_LOGI(TAG, "OpenAI WebRTC stopped");
    return ESP_OK;
}

esp_err_t openai_realtime_send_text(const char *text)
{
    if (!webrtc) {
        ESP_LOGE(TAG, "WebRTC not started");
        return ESP_FAIL;
    }
    
    if (!text || strlen(text) == 0) {
        ESP_LOGE(TAG, "Invalid text");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Check if a response is already in progress
    if (response_state.mutex && xSemaphoreTake(response_state.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (response_state.response_in_progress) {
            ESP_LOGW(TAG, "Response already in progress, cancelling previous");
            // Send a cancel event for the current response
            cJSON *cancel = cJSON_CreateObject();
            cJSON_AddStringToObject(cancel, "type", "response.cancel");
            char *cancel_json = cJSON_PrintUnformatted(cancel);
            if (cancel_json) {
                esp_webrtc_send_custom_data(webrtc, ESP_WEBRTC_CUSTOM_DATA_VIA_DATA_CHANNEL,
                                          (uint8_t *)cancel_json, strlen(cancel_json));
                mem_free(cancel_json);
            }
            cJSON_Delete(cancel);
            vTaskDelay(pdMS_TO_TICKS(100));  // Brief delay for cancel to process
        }
        xSemaphoreGive(response_state.mutex);
    }
    
    ESP_LOGI(TAG, "Sending text: %s", text);
    
    // First, send the conversation.item.create with the user message
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "conversation.item.create");
    
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "type", "message");
    cJSON_AddStringToObject(item, "role", "user");
    
    cJSON *contentArray = cJSON_CreateArray();
    cJSON *contentItem = cJSON_CreateObject();
    cJSON_AddStringToObject(contentItem, "type", "input_text");
    cJSON_AddStringToObject(contentItem, "text", text);
    cJSON_AddItemToArray(contentArray, contentItem);
    cJSON_AddItemToObject(item, "content", contentArray);
    cJSON_AddItemToObject(root, "item", item);
    
    char *json_string = cJSON_PrintUnformatted(root);  // Use unformatted for efficiency
    if (json_string) {
        ESP_LOGI(TAG, "Sending conversation.item.create");
        esp_err_t ret = esp_webrtc_send_custom_data(webrtc, ESP_WEBRTC_CUSTOM_DATA_VIA_DATA_CHANNEL, 
                                                    (uint8_t *)json_string, strlen(json_string));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send conversation item: %s", esp_err_to_name(ret));
            mem_free(json_string);
            cJSON_Delete(root);
            return ret;
        }
        mem_free(json_string);
    }
    cJSON_Delete(root);
    
    // Short delay to ensure message is processed
    vTaskDelay(pdMS_TO_TICKS(20)); // Minimal delay for message ordering
    
    // Then send response.create to trigger the response
    cJSON *response_create = cJSON_CreateObject();
    cJSON_AddStringToObject(response_create, "type", "response.create");
    
    char *create_json = cJSON_PrintUnformatted(response_create);  // Use unformatted
    if (create_json) {
        ESP_LOGI(TAG, "Sending response.create to trigger response");
        esp_err_t ret = esp_webrtc_send_custom_data(webrtc, ESP_WEBRTC_CUSTOM_DATA_VIA_DATA_CHANNEL, 
                                                    (uint8_t *)create_json, strlen(create_json));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send response.create: %s", esp_err_to_name(ret));
        }
        mem_free(create_json);
    }
    cJSON_Delete(response_create);
    
    return ESP_OK;
}

esp_err_t openai_realtime_query(void)
{
    if (webrtc) {
        esp_webrtc_query(webrtc);
        return ESP_OK;
    }
    return ESP_FAIL;
}

bool openai_realtime_is_connected(void)
{
    return webrtc != NULL;
}

esp_err_t openai_realtime_pause_audio(void)
{
    if (!webrtc) {
        ESP_LOGE(TAG, "WebRTC not started");
        return ESP_FAIL;
    }
    
    if (audio_state.audio_paused) {
        ESP_LOGD(TAG, "Audio already paused");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Pausing WebRTC audio");
    
    // Delegate to audio module to release output resources
    esp_err_t ret = audio_module_release_output();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to release audio output: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Set media provider from audio module
    esp_webrtc_media_provider_t media_provider = {};
    esp_err_t provider_ret = audio_module_get_media_provider(&media_provider);
    if (provider_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get media provider from audio module: %s", esp_err_to_name(provider_ret));
        esp_webrtc_close(webrtc);
        webrtc = NULL;
        return provider_ret;
    }
    esp_webrtc_set_media_provider(webrtc, &media_provider);
    
    audio_state.audio_paused = true;
    ESP_LOGI(TAG, "WebRTC audio paused successfully");
    
    return ESP_OK;
}

esp_err_t openai_realtime_resume_audio(void)
{
    if (!webrtc) {
        ESP_LOGE(TAG, "WebRTC not started");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Resuming/enabling WebRTC audio");
    
    esp_err_t ret = audio_module_restore_output();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to restore audio output: %s", esp_err_to_name(ret));
        return ret;
    }

    // Set media provider from audio module
    esp_webrtc_media_provider_t media_provider = {};
    esp_err_t provider_ret = audio_module_get_media_provider(&media_provider);
    if (provider_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get media provider from audio module: %s", esp_err_to_name(provider_ret));
        esp_webrtc_close(webrtc);
        webrtc = NULL;
        return provider_ret;
    }
    esp_webrtc_set_media_provider(webrtc, &media_provider);
    
    audio_state.audio_paused = false;
    ESP_LOGI(TAG, "WebRTC audio resumed/enabled successfully");
    
    return ESP_OK;
}

esp_err_t openai_realtime_set_activation_mode(bool vision_enabled)
{
    // Deprecated: Vision is always enabled now
    // This function is kept for backward compatibility but does nothing
    ESP_LOGW(TAG, "set_activation_mode is deprecated - vision is always enabled");
    return ESP_OK;
}