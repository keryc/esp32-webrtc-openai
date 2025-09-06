#include "board_module.h"
#include "esp_log.h"
#include "codec_board.h"
#include "codec_init.h"
#include "sdkconfig.h"

static const char *TAG = "board_module";

// Module state
static struct {
    bool initialized;
} board_state = {0};

esp_err_t board_module_init(void)
{
    if (board_state.initialized) {
        ESP_LOGD(TAG, "Board module already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing board hardware peripherals");
    
    // Set codec board type for hardware configuration
    set_codec_board_type(CONFIG_AG_SYSTEM_BOARD_NAME);
    
    // Initialize codec hardware (I2C, I2S, codec chip)
    codec_init_cfg_t cfg = {
#if CONFIG_IDF_TARGET_ESP32S3
        .in_mode    = CODEC_I2S_MODE_STD,
        .out_mode   = CODEC_I2S_MODE_STD,
        .in_use_tdm = false,
#endif
        .reuse_dev = false
    };
    
    int ret = init_codec(&cfg);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to initialize codec: %d", ret);
        return ESP_FAIL;
    }

    board_state.initialized = true;
    
    ESP_LOGI(TAG, "Board hardware peripherals initialized successfully");
    return ESP_OK;
}
