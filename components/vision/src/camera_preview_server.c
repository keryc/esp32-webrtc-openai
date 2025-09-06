#include "camera_preview_server.h"
#include <esp_log.h>
#include <esp_http_server.h>
#include <esp_wifi.h>
#include <string.h>
#include "memory_manager.h"
static const char *TAG = "cam_preview_server";

// Server state with double buffering
static struct {
    bool initialized;
    bool running;
    uint16_t port;
    httpd_handle_t server_handle;
    
    // Double buffering for frames
    uint8_t *frame_buffer_a;
    uint8_t *frame_buffer_b;
    volatile uint8_t *active_read_buffer;  // Buffer being read by HTTP
    volatile uint8_t *active_write_buffer; // Buffer being written by camera
    volatile size_t read_buffer_size;
    volatile size_t write_buffer_size;
    volatile uint32_t frame_version;       // Increments on each new frame
    size_t frame_buffer_capacity;
    
    SemaphoreHandle_t buffer_swap_mutex;   // Only for swapping pointers
} server_state = {0};

// HTML page for camera preview
static const char html_page[] = 
"<!DOCTYPE html>\n"
"<html>\n"
"<head>\n"
"    <title>Camera Preview</title>\n"
"    <style>\n"
"        body { font-family: Arial, sans-serif; text-align: center; background: #000; color: #fff; }\n"
"        .container { max-width: 1200px; margin: 0 auto; padding: 20px; }\n"
"        img { max-width: 100%; height: auto; border: 2px solid #333; border-radius: 8px; }\n"
"        .controls { margin: 20px 0; }\n"
"        button { background: #007bff; color: white; border: none; padding: 10px 20px; margin: 5px; cursor: pointer; border-radius: 4px; }\n"
"        button:hover { background: #0056b3; }\n"
"        .status { color: #28a745; margin: 10px 0; }\n"
"    </style>\n"
"    <script>\n"
"        let refreshInterval;\n"
"        function startStream() {\n"
"            const img = document.getElementById('camera-feed');\n"
"            refreshInterval = setInterval(() => {\n"
"                img.src = '/stream?' + new Date().getTime();\n"
"            }, 200); // ~5fps refresh for smoother experience\n"
"            document.getElementById('status').innerText = 'Stream: Active';\n"
"        }\n"
"        function stopStream() {\n"
"            if (refreshInterval) {\n"
"                clearInterval(refreshInterval);\n"
"            }\n"
"            document.getElementById('status').innerText = 'Stream: Stopped';\n"
"        }\n"
"        window.onload = () => {\n"
"            startStream();\n"
"        };\n"
"    </script>\n"
"</head>\n"
"<body>\n"
"    <div class='container'>\n"
"        <h1>🤖 Live Camera Preview</h1>\n"
"        <div class='status' id='status'>Stream: Loading...</div>\n"
"        <div class='controls'>\n"
"            <button onclick='startStream()'>▶️ Start Stream</button>\n"
"            <button onclick='stopStream()'>⏹️ Stop Stream</button>\n"
"        </div>\n"
"        <img id='camera-feed' src='/stream' alt='Camera Feed' />\n"
"        <p>Live preview from your AI camera</p>\n"
"    </div>\n"
"</body>\n"
"</html>\n";

// HTTP handler for main preview page
static esp_err_t preview_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html_page, strlen(html_page));
}

// HTTP handler for camera stream with lock-free reading
static esp_err_t stream_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    
    // Get stable references to current read buffer (no mutex needed for reading)
    volatile uint8_t *read_buffer = server_state.active_read_buffer;
    volatile size_t buffer_size = server_state.read_buffer_size;
    
    if (!read_buffer || buffer_size == 0) {
        // No frame available, send placeholder
        httpd_resp_set_status(req, "204 No Content");
        return httpd_resp_send(req, NULL, 0);
    }
    
    // Set JPEG content type
    httpd_resp_set_type(req, "image/jpeg");
    
    // Add cache control headers
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    
    // Send JPEG frame (no mutex needed, reading from dedicated read buffer)
    ret = httpd_resp_send(req, (char*)read_buffer, buffer_size);
    
    return ret;
}

esp_err_t camera_preview_server_init(uint16_t port)
{
    if (server_state.initialized) {
        ESP_LOGW(TAG, "Preview server already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing camera preview server on port %d", port);
    
    server_state.port = port;
    
    // Pre-allocate double frame buffers (1MB each)
    server_state.frame_buffer_capacity = 1024 * 1024;
    
    server_state.frame_buffer_a = mem_alloc(server_state.frame_buffer_capacity, MEM_POLICY_PREFER_PSRAM, "camera_preview_server");
    server_state.frame_buffer_b = mem_alloc(server_state.frame_buffer_capacity, MEM_POLICY_PREFER_PSRAM, "camera_preview_server");
    
    if (!server_state.frame_buffer_a || !server_state.frame_buffer_b) {
        ESP_LOGE(TAG, "Failed to allocate frame buffers");
        if (server_state.frame_buffer_a) mem_free(server_state.frame_buffer_a);
        if (server_state.frame_buffer_b) mem_free(server_state.frame_buffer_b);
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize buffer pointers
    server_state.active_read_buffer = server_state.frame_buffer_a;
    server_state.active_write_buffer = server_state.frame_buffer_b;
    server_state.read_buffer_size = 0;
    server_state.write_buffer_size = 0;
    server_state.frame_version = 0;
    
    // Create buffer swap mutex (only used for pointer swapping, very fast)
    server_state.buffer_swap_mutex = xSemaphoreCreateMutex();
    if (!server_state.buffer_swap_mutex) {
        ESP_LOGE(TAG, "Failed to create buffer swap mutex");
        mem_free(server_state.frame_buffer_a);
        mem_free(server_state.frame_buffer_b);
        return ESP_ERR_NO_MEM;
    }
    
    server_state.initialized = true;
    ESP_LOGI(TAG, "Camera preview server initialized successfully");
    return ESP_OK;
}

esp_err_t camera_preview_server_start(void)
{
    if (!server_state.initialized) {
        ESP_LOGE(TAG, "Server not initialized");
        return ESP_FAIL;
    }
    
    if (server_state.running) {
        ESP_LOGW(TAG, "Server already running");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Starting camera preview HTTP server");
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = server_state.port;
    config.max_open_sockets = 4;
    config.task_priority = 5;
    
    esp_err_t ret = httpd_start(&server_state.server_handle, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Register URI handlers
    httpd_uri_t preview_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = preview_page_handler,
        .user_ctx = NULL
    };
    
    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL
    };
    
    httpd_register_uri_handler(server_state.server_handle, &preview_uri);
    httpd_register_uri_handler(server_state.server_handle, &stream_uri);
    
    server_state.running = true;
    
    // Get IP and show access info
    char url[128];
    if (camera_preview_server_get_url(url, sizeof(url)) == ESP_OK) {
        ESP_LOGI(TAG, "Camera preview server started successfully");
        ESP_LOGI(TAG, "Access camera preview at: %s", url);
        ESP_LOGI(TAG, "Or directly: http://<your-esp32-ip>:%d/", server_state.port);
    }
    
    return ESP_OK;
}

esp_err_t camera_preview_server_stop(void)
{
    if (!server_state.initialized || !server_state.running) {
        ESP_LOGW(TAG, "Server not running");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Stopping camera preview server");
    
    if (server_state.server_handle) {
        esp_err_t ret = httpd_stop(server_state.server_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop HTTP server: %s", esp_err_to_name(ret));
            return ret;
        }
        server_state.server_handle = NULL;
    }
    
    server_state.running = false;
    ESP_LOGI(TAG, "Camera preview server stopped");
    return ESP_OK;
}

esp_err_t camera_preview_server_deinit(void)
{
    if (!server_state.initialized) {
        return ESP_OK;
    }
    
    // Stop server if running
    if (server_state.running) {
        camera_preview_server_stop();
    }
    
    // Clean up resources
    if (server_state.buffer_swap_mutex) {
        vSemaphoreDelete(server_state.buffer_swap_mutex);
        server_state.buffer_swap_mutex = NULL;
    }
    
    if (server_state.frame_buffer_a) {
        mem_free(server_state.frame_buffer_a);
        server_state.frame_buffer_a = NULL;
    }
    
    if (server_state.frame_buffer_b) {
        mem_free(server_state.frame_buffer_b);
        server_state.frame_buffer_b = NULL;
    }
    
    server_state.active_read_buffer = NULL;
    server_state.active_write_buffer = NULL;
    
    server_state.initialized = false;
    server_state.frame_buffer_capacity = 0;
    server_state.read_buffer_size = 0;
    server_state.write_buffer_size = 0;
    server_state.frame_version = 0;
    
    ESP_LOGI(TAG, "Camera preview server deinitialized");
    return ESP_OK;
}

esp_err_t camera_preview_server_send_frame(uint8_t *frame_data, size_t frame_size)
{
    if (!server_state.initialized || !server_state.running) {
        return ESP_FAIL;
    }
    
    if (!frame_data || frame_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Check if frame fits in our buffer
    if (frame_size > server_state.frame_buffer_capacity) {
        ESP_LOGW(TAG, "Frame too large (%zu > %zu), skipping", frame_size, server_state.frame_buffer_capacity);
        return ESP_ERR_INVALID_SIZE;
    }
    
    // Copy frame data to write buffer (no mutex needed)
    memcpy((void*)server_state.active_write_buffer, frame_data, frame_size);
    server_state.write_buffer_size = frame_size;
    
    // Quick buffer swap (only time we need mutex, just for pointer swapping)
    if (xSemaphoreTake(server_state.buffer_swap_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        // Swap read and write buffers
        volatile uint8_t *temp_buffer = server_state.active_read_buffer;
        volatile size_t temp_size = server_state.read_buffer_size;
        
        server_state.active_read_buffer = server_state.active_write_buffer;
        server_state.read_buffer_size = server_state.write_buffer_size;
        
        server_state.active_write_buffer = temp_buffer;
        server_state.write_buffer_size = temp_size;
        
        server_state.frame_version++;
        
        xSemaphoreGive(server_state.buffer_swap_mutex);
        return ESP_OK;
    } else {
        // Very rare case - just skip this frame update
        return ESP_ERR_TIMEOUT;
    }
}

bool camera_preview_server_is_running(void)
{
    return server_state.initialized && server_state.running;
}

esp_err_t camera_preview_server_get_url(char *url_buffer, size_t buffer_size)
{
    if (!url_buffer || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Get WiFi IP address
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        snprintf(url_buffer, buffer_size, "http://" IPSTR ":%d/", 
                 IP2STR(&ip_info.ip), server_state.port);
        return ESP_OK;
    } else {
        snprintf(url_buffer, buffer_size, "http://<esp32-ip>:%d/", server_state.port);
        return ESP_FAIL;
    }
}