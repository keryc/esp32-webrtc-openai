#include "camera_commands.h"
#include "camera_module.h"
#include <esp_log.h>
#include <esp_console.h>
#include <argtable3/argtable3.h>
#include <string.h>
#include "memory_manager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
static const char *TAG = "cam_cmds";

// Command argument structures
static struct {
    struct arg_str *mode;
    struct arg_end *end;
} cam_start_args;

static struct {
    struct arg_str *quality;
    struct arg_end *end;
} cam_quality_args;

static struct {
    struct arg_int *fps;
    struct arg_end *end;
} cam_fps_args;

static struct {
    struct arg_str *url;
    struct arg_end *end;
} cam_stream_args;

static struct {
    struct arg_int *interval;
    struct arg_end *end;
} cam_interval_args;

static struct {
    struct arg_int *duration;
    struct arg_end *end;
} cam_smart_prefetch_args;


// Parse mode string to enum
static cam_mode_t parse_mode(const char *mode_str)
{
    if (strcasecmp(mode_str, "stream") == 0) {
        return CAM_MODE_STREAM_ONLY;
    } else if (strcasecmp(mode_str, "analysis") == 0) {
        return CAM_MODE_ANALYSIS_ONLY;
    } else if (strcasecmp(mode_str, "combined") == 0) {
        return CAM_MODE_COMBINED;
    }
    return CAM_MODE_COMBINED; // default to both
}

// Parse quality string to enum
static cam_quality_t parse_quality(const char *quality_str)
{
    if (strcasecmp(quality_str, "low") == 0) {
        return CAM_QUALITY_LOW;
    } else if (strcasecmp(quality_str, "medium") == 0) {
        return CAM_QUALITY_MEDIUM;
    } else if (strcasecmp(quality_str, "high") == 0) {
        return CAM_QUALITY_HIGH;
    } else if (strcasecmp(quality_str, "hd") == 0) {
        return CAM_QUALITY_HD;
    }
    return CAM_QUALITY_MEDIUM; // default
}

// Initialize camera/vision module
static int cmd_cam_init(int argc, char **argv)
{
    ESP_LOGI(TAG, "Initializing/Reinitializing unified camera/vision module...");
    
    // Default configuration
    cam_config_t config = {
        .mode = CAM_MODE_COMBINED,
        .quality = CAM_QUALITY_MEDIUM,
        .fps = 15,
        .auto_exposure = true,
        .auto_white_balance = true,
        .jpeg_quality = 10,
        .buffer_frames = 3,
        .enable_live_preview = true
    };
    
    esp_err_t ret = cam_module_init(&config, NULL);
    if (ret == ESP_OK) {
        printf("Camera/Vision module initialized/reinitialized successfully\n");
    } else {
        printf("Failed to initialize/reinitialize camera/vision module: %s\n", esp_err_to_name(ret));
    }
    
    return 0;
}

// Start camera capture
static int cmd_cam_start(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &cam_start_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, cam_start_args.end, argv[0]);
        return 1;
    }
    
    cam_mode_t mode = CAM_MODE_COMBINED;
    if (cam_start_args.mode->count) {
        mode = parse_mode(cam_start_args.mode->sval[0]);
    }
    
    const char *mode_names[] = {"Stream Only", "Analysis Only", "Combined (Stream+Analysis)"};
    ESP_LOGI(TAG, "Starting camera in %s mode", mode_names[mode]);
    
    esp_err_t ret = cam_module_start(mode);
    if (ret == ESP_OK) {
        printf("Camera started in %s mode\n", mode_names[mode]);
    } else {
        printf("Failed to start camera: %s\n", esp_err_to_name(ret));
    }
    
    return 0;
}

// Stop camera capture
static int cmd_cam_stop(int argc, char **argv)
{
    ESP_LOGI(TAG, "Stopping camera capture");
    
    esp_err_t ret = cam_module_stop();
    if (ret == ESP_OK) {
        printf("Camera stopped successfully\n");
    } else {
        printf("Failed to stop camera: %s\n", esp_err_to_name(ret));
    }
    
    return 0;
}

// Test camera capture
static int cmd_cam_test(int argc, char **argv)
{
    ESP_LOGI(TAG, "Testing camera capture");
    
    esp_err_t ret = cam_module_test_capture();
    if (ret == ESP_OK) {
        printf("Camera test completed successfully\n");
    } else {
        printf("Camera test failed: %s\n", esp_err_to_name(ret));
    }
    
    return 0;
}

// Set camera quality
static int cmd_cam_quality(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &cam_quality_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, cam_quality_args.end, argv[0]);
        return 1;
    }
    
    if (cam_quality_args.quality->count == 0) {
        printf("Available qualities: low, medium, high, hd\n");
        return 0;
    }
    
    cam_quality_t quality = parse_quality(cam_quality_args.quality->sval[0]);
    const char *quality_names[] = {"Low", "Medium", "High", "HD"};
    
    ESP_LOGI(TAG, "Setting quality to %s", quality_names[quality]);
    
    esp_err_t ret = cam_module_set_quality(quality);
    if (ret == ESP_OK) {
        printf("Camera quality set to %s\n", quality_names[quality]);
    } else {
        printf("Failed to set quality: %s\n", esp_err_to_name(ret));
    }
    
    return 0;
}

// Set camera FPS
static int cmd_cam_fps(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &cam_fps_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, cam_fps_args.end, argv[0]);
        return 1;
    }
    
    if (cam_fps_args.fps->count == 0) {
        printf("Usage: cam_fps <fps>\n");
        return 0;
    }
    
    uint32_t fps = (uint32_t)cam_fps_args.fps->ival[0];
    
    ESP_LOGI(TAG, "Setting FPS to %lu", fps);
    
    esp_err_t ret = cam_module_set_fps(fps);
    if (ret == ESP_OK) {
        printf("Camera FPS set to %lu\n", fps);
    } else {
        printf("Failed to set FPS: %s\n", esp_err_to_name(ret));
    }
    
    return 0;
}

// Get camera statistics
static int cmd_cam_stats(int argc, char **argv)
{
    cam_stats_t stats;
    esp_err_t ret = cam_module_get_stats(&stats);
    
    if (ret == ESP_OK) {
        printf("Camera/Vision Statistics:\n");
        printf("  Total frames captured: %lu\n", stats.total_frames_captured);
        printf("  Frames dropped: %lu\n", stats.frames_dropped);
        printf("  Current FPS: %lu\n", stats.current_fps);
        printf("  Buffer usage: %lu%%\n", stats.buffer_usage_percent);
        printf("  Is streaming: %s\n", stats.is_streaming ? "Yes" : "No");
        printf("  Is recording: %s\n", stats.is_recording ? "Yes" : "No");
        printf("  Total bytes processed: %llu\n", stats.total_bytes_processed);
    } else {
        printf("Failed to get statistics: %s\n", esp_err_to_name(ret));
    }
    
    return 0;
}

// Start live preview stream
static int cmd_cam_stream_start(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &cam_stream_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, cam_stream_args.end, argv[0]);
        return 1;
    }
    
    const char *url = cam_stream_args.url->count > 0 ? cam_stream_args.url->sval[0] : "webrtc://default";
    
    ESP_LOGI(TAG, "Starting preview stream to: %s", url);
    
    esp_err_t ret = cam_module_start_preview_stream(url);
    if (ret == ESP_OK) {
        printf("✅ Preview stream started!\n");
        printf("📷 Camera streaming is active\n");
        printf("🌐 HTTP server is running\n");
        printf("💻 Open your browser and navigate to the IP address shown above\n");
        printf("   If no URL shown, check with 'ifconfig' command\n");
    } else {
        printf("❌ Failed to start preview stream: %s\n", esp_err_to_name(ret));
        printf("   Try: cam_init first, then cam_stream_start\n");
    }
    
    return 0;
}

// Stop live preview stream
static int cmd_cam_stream_stop(int argc, char **argv)
{
    ESP_LOGI(TAG, "Stopping preview stream");
    
    esp_err_t ret = cam_module_stop_preview_stream();
    if (ret == ESP_OK) {
        printf("Preview stream stopped\n");
    } else {
        printf("Failed to stop preview stream: %s\n", esp_err_to_name(ret));
    }
    
    return 0;
}

// Check if module is ready
static int cmd_cam_status(int argc, char **argv)
{
    bool ready = cam_module_is_ready();
    
    printf("Camera/Vision Module Status: %s\n", ready ? "READY" : "NOT READY");
    
    if (ready) {
        cam_stats_t stats;
        if (cam_module_get_stats(&stats) == ESP_OK) {
            printf("Current state: %s\n", stats.is_streaming ? "STREAMING" : "IDLE");
        }
    }
    
    return 0;
}

// Vision buffer command implementations
static int cmd_capture_start(int argc, char **argv)
{
    printf("Starting continuous capture mode...\n");
    esp_err_t ret = cam_module_start_capture();
    if (ret == ESP_OK) {
        printf("✅ Capture started in analysis mode\n");
        printf("Note: This starts streaming task for continuous monitoring\n");
    } else {
        printf("❌ Failed to start capture: %s\n", esp_err_to_name(ret));
    }
    return (ret == ESP_OK) ? 0 : 1;
}

static int cmd_capture_stop(int argc, char **argv)
{
    printf("Stopping continuous capture...\n");
    esp_err_t ret = cam_module_stop_capture();
    if (ret == ESP_OK) {
        printf("✅ Capture stopped\n");
        uint32_t captured, dropped;
        cam_module_get_capture_stats(&captured, &dropped);
        printf("Stats: %lu frames captured, %lu dropped\n", captured, dropped);
    } else {
        printf("❌ Failed to stop capture: %s\n", esp_err_to_name(ret));
    }
    return (ret == ESP_OK) ? 0 : 1;
}

static int cmd_cam_set_interval(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &cam_interval_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, cam_interval_args.end, argv[0]);
        return 1;
    }
    
    uint32_t interval_ms = cam_interval_args.interval->ival[0];
    
    esp_err_t ret = cam_module_set_capture_interval(interval_ms);
    if (ret == ESP_OK) {
        printf("⏱️ Capture interval set to %u ms\n", (unsigned)interval_ms);
    } else {
        printf("❌ Failed to set capture interval: %s\n", esp_err_to_name(ret));
    }
    
    return (ret == ESP_OK) ? 0 : 1;
}

// Test single frame capture directly
static int cmd_cam_capture_test(int argc, char **argv)
{
    printf("📸 Testing direct frame capture from hardware...\n");
    
    // Check if camera is initialized
    if (!cam_module_is_ready()) {
        printf("❌ Camera not initialized. Run 'cam_init' first.\n");
        return 1;
    }
    
    // Try to capture a single frame
    int frame_count = 0;
    char **frames = cam_module_get_vision_frames(1, &frame_count);
    
    if (frames && frame_count > 0) {
        printf("✅ Frame captured successfully!\n");
        printf("   Frame is base64 encoded, size: %zu bytes\n", strlen(frames[0]));
        
        // Free the captured frame
        for (int i = 0; i < frame_count; i++) {
            if (frames[i]) mem_free(frames[i]);
        }
        mem_free(frames);
    } else {
        printf("❌ Failed to capture frame\n");
        printf("   Check camera initialization and hardware connection\n");
    }
    
    return 0;
}

// Reset camera module (full cleanup and reinit)
static int cmd_cam_reset(int argc, char **argv)
{
    printf("🔄 Resetting camera module...\n");
    
    // First stop any ongoing capture
    if (cam_module_is_capturing()) {
        printf("  Stopping current capture...\n");
        cam_module_stop();
    }
    
    // Deinitialize
    printf("  Deinitializing module...\n");
    esp_err_t ret = cam_module_deinit();
    if (ret != ESP_OK) {
        printf("⚠️  Warning during deinit: %s\n", esp_err_to_name(ret));
    }
    
    // Wait a bit for hardware to settle
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Reinitialize with default config
    printf("  Reinitializing module...\n");
    cam_config_t config = {
        .mode = CAM_MODE_COMBINED,
        .quality = CAM_QUALITY_MEDIUM,
        .fps = 15,
        .auto_exposure = true,
        .auto_white_balance = true,
        .jpeg_quality = 10,
        .buffer_frames = 3,
        .enable_live_preview = false
    };
    
    ret = cam_module_init(&config, NULL);
    if (ret == ESP_OK) {
        printf("✅ Camera module reset successfully\n");
        printf("   You can now use camera commands\n");
    } else {
        printf("❌ Failed to reset camera module: %s\n", esp_err_to_name(ret));
    }
    
    return (ret == ESP_OK) ? 0 : 1;
}

// Diagnose camera hardware state
static int cmd_cam_diagnose(int argc, char **argv)
{
    printf("🔍 Camera Module Diagnostics\n");
    printf("============================\n");
    
    // Check module initialization
    bool ready = cam_module_is_ready();
    printf("Module Ready: %s\n", ready ? "✅ YES" : "❌ NO");
    
    if (ready) {
        // Get statistics
        cam_stats_t stats;
        if (cam_module_get_stats(&stats) == ESP_OK) {
            printf("\n📊 Current Statistics:\n");
            printf("  Streaming: %s\n", stats.is_streaming ? "YES" : "NO");
            printf("  Recording: %s\n", stats.is_recording ? "YES" : "NO");
            printf("  Frames captured: %lu\n", stats.total_frames_captured);
            printf("  Frames dropped: %lu\n", stats.frames_dropped);
            printf("  Current FPS: %lu\n", stats.current_fps);
            printf("  Buffer usage: %lu%%\n", stats.buffer_usage_percent);
        }
        
        // Check continuous capture state
        bool capturing = cam_module_is_capturing();
        printf("\n🎥 Capture States:\n");
        printf("  Continuous capture: %s\n", capturing ? "ACTIVE" : "INACTIVE");
        
        if (capturing) {
            uint32_t captured, dropped;
            cam_module_get_capture_stats(&captured, &dropped);
            printf("  Continuous stats: %lu captured, %lu dropped\n", captured, dropped);
        }
        
        // Test direct hardware capture
        printf("\n🔧 Hardware Test:\n");
        printf("  Testing direct capture... ");
        fflush(stdout);
        
        int frame_count = 0;
        char **frames = cam_module_get_vision_frames(1, &frame_count);
        if (frames && frame_count > 0) {
            printf("✅ SUCCESS\n");
            for (int i = 0; i < frame_count; i++) {
                if (frames[i]) mem_free(frames[i]);
            }
            mem_free(frames);
        } else {
            printf("❌ FAILED\n");
        }
    } else {
        printf("\n⚠️  Camera module not initialized.\n");
        printf("   Run 'cam_init' to initialize the camera.\n");
    }
    
    printf("\n💡 Quick Commands:\n");
    printf("  cam_init          - Initialize camera\n");
    printf("  cam_capture_test  - Test single frame capture\n");
    printf("  cam_start         - Start streaming\n");
    printf("  cam_capture_start - Start continuous capture\n");
    
    return 0;
}

esp_err_t camera_commands_register(void)
{
    ESP_LOGI(TAG, "Registering unified camera/vision commands");
    
    // Initialize argument structures
    cam_start_args.mode = arg_str0(NULL, NULL, "<mode>", "Capture mode: stream, analysis, combined");
    cam_start_args.end = arg_end(1);
    
    cam_quality_args.quality = arg_str0(NULL, NULL, "<quality>", "Quality: low, medium, high, hd");
    cam_quality_args.end = arg_end(1);
    
    cam_fps_args.fps = arg_int0(NULL, NULL, "<fps>", "Target frames per second (1-30)");
    cam_fps_args.end = arg_end(1);
    
    cam_stream_args.url = arg_str0(NULL, NULL, "<url>", "Stream URL or endpoint");
    cam_stream_args.end = arg_end(1);
    
    cam_interval_args.interval = arg_int1(NULL, NULL, "<ms>", "Capture interval in milliseconds");
    cam_interval_args.end = arg_end(1);
    
    cam_smart_prefetch_args.duration = arg_int0(NULL, NULL, "<duration_ms>", "Prefetch duration in milliseconds (default 10000)");
    cam_smart_prefetch_args.end = arg_end(1);
    
    
    // Register commands
    const esp_console_cmd_t commands[] = {
        {
            .command = "cam_init",
            .help = "Initialize/Reinitialize unified camera/vision module with esp_capture",
            .hint = NULL,
            .func = &cmd_cam_init,
        },
        {
            .command = "cam_start",
            .help = "Start camera capture in specified mode",
            .hint = NULL,
            .func = &cmd_cam_start,
            .argtable = &cam_start_args
        },
        {
            .command = "cam_stop",
            .help = "Stop camera capture",
            .hint = NULL,
            .func = &cmd_cam_stop,
        },
        {
            .command = "cam_test",
            .help = "Test camera capture functionality",
            .hint = NULL,
            .func = &cmd_cam_test,
        },
        {
            .command = "cam_quality",
            .help = "Set camera quality (low, medium, high, hd)",
            .hint = NULL,
            .func = &cmd_cam_quality,
            .argtable = &cam_quality_args
        },
        {
            .command = "cam_fps",
            .help = "Set camera frame rate",
            .hint = NULL,
            .func = &cmd_cam_fps,
            .argtable = &cam_fps_args
        },
        {
            .command = "cam_stats",
            .help = "Show camera/vision statistics",
            .hint = NULL,
            .func = &cmd_cam_stats,
        },
        {
            .command = "cam_stream_start",
            .help = "Start live camera preview stream to laptop",
            .hint = NULL,
            .func = &cmd_cam_stream_start,
            .argtable = &cam_stream_args
        },
        {
            .command = "cam_stream_stop",
            .help = "Stop live camera preview stream",
            .hint = NULL,
            .func = &cmd_cam_stream_stop,
        },
        {
            .command = "cam_status",
            .help = "Check camera/vision module status",
            .hint = NULL,
            .func = &cmd_cam_status,
        },
        {
            .command = "cam_capture_start",
            .help = "Start continuous capture mode (for future recording features)",
            .hint = NULL,
            .func = &cmd_capture_start,
        },
        {
            .command = "cam_capture_stop",
            .help = "Stop continuous capture mode",
            .hint = NULL,
            .func = &cmd_capture_stop,
        },
        {
            .command = "cam_set_interval",
            .help = "Set capture interval in milliseconds",
            .hint = NULL,
            .func = &cmd_cam_set_interval,
            .argtable = &cam_interval_args
        },
        {
            .command = "cam_capture_test",
            .help = "Test direct frame capture from camera hardware",
            .hint = NULL,
            .func = &cmd_cam_capture_test,
        },
        {
            .command = "cam_reset",
            .help = "Reset camera module (full cleanup and reinit)",
            .hint = NULL,
            .func = &cmd_cam_reset,
        },
        {
            .command = "cam_diagnose",
            .help = "Run full camera module diagnostics",
            .hint = NULL,
            .func = &cmd_cam_diagnose,
        }
    };
    
    // Register all commands
    for (int i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&commands[i]));
    }
    
    ESP_LOGI(TAG, "Unified camera/vision commands registered successfully");
    return ESP_OK;
}
