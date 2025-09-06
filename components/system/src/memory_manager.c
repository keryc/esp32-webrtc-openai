/*
 * Memory Manager Implementation
 * Provides runtime memory detection, smart allocation, and monitoring
 */

#include "memory_manager.h"
#include <esp_log.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_flash.h>
#include <esp_timer.h>
#include <freertos/task.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_chip_info.h"

static const char *TAG = "mem_manager";

// Memory thresholds - INCREASED for safety
#define MIN_INTERNAL_FREE_KB    80   // Increased from 50KB
#define MIN_PSRAM_FREE_KB       512  // Increased from 256KB  
#define MIN_DMA_FREE_KB         20   // Increased from 16KB
#define FRAGMENTATION_THRESHOLD 0.3f  // 30% fragmentation

static struct {
    bool initialized;
    memory_status_t status;
    esp_timer_handle_t monitor_timer;
    uint32_t allocation_count;
    uint32_t allocation_failures;
} mem_state = {0};

// Forward declaration
static void update_memory_status(void);

// Memory monitoring timer callback
static void memory_monitor_cb(void* arg)
{
    update_memory_status();
    
    // ALWAYS show status every 30 seconds (not just warnings)
    ESP_LOGI(TAG, "[AUTO] Internal: %u KB free (min:%u) | PSRAM: %u KB free (min:%u) | DMA: %u KB | Largest: %u KB",
             mem_state.status.internal_free_kb,
             mem_state.status.internal_min_free_kb,
             mem_state.status.psram_free_kb,
             mem_state.status.psram_min_free_kb,
             mem_state.status.dma_free_kb,
             mem_state.status.largest_free_block_kb);
    
    // CRITICAL: Restart if memory too low to prevent crash
    if (mem_state.status.internal_free_kb < 40) {
        ESP_LOGE(TAG, "🔴 CRITICAL: Internal memory < 40KB! Restarting to prevent crash...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }
    
    if (mem_state.status.low_internal_memory) {
        ESP_LOGW(TAG, "⚠️ Low internal memory: %u KB free", 
                 mem_state.status.internal_free_kb);
    }
    
    if (mem_state.status.low_psram_memory && mem_state.status.has_psram) {
        ESP_LOGW(TAG, "⚠️ Low PSRAM: %u KB free", 
                 mem_state.status.psram_free_kb);
    }
    
    // Check for memory leaks
    if (mem_state.status.internal_min_free_kb < mem_state.status.internal_free_kb / 2) {
        ESP_LOGW(TAG, "⚠️ Possible memory leak detected!");
    }
}

// Update current memory status
static void update_memory_status(void)
{
    // Internal memory
    mem_state.status.internal_total_kb = heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024;
    mem_state.status.internal_free_kb = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
    mem_state.status.internal_min_free_kb = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024;
    
    // PSRAM
    if (mem_state.status.has_psram) {
        mem_state.status.psram_total_kb = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024;
        mem_state.status.psram_free_kb = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
        mem_state.status.psram_min_free_kb = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM) / 1024;
    }
    
    // DMA memory
    mem_state.status.dma_free_kb = heap_caps_get_free_size(MALLOC_CAP_DMA) / 1024;
    
    // Largest free block (fragmentation indicator)
    mem_state.status.largest_free_block_kb = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT) / 1024;
    
    // Check memory pressure
    mem_state.status.low_internal_memory = (mem_state.status.internal_free_kb < MIN_INTERNAL_FREE_KB);
    mem_state.status.low_psram_memory = (mem_state.status.psram_free_kb < MIN_PSRAM_FREE_KB);
    
    // Check fragmentation
    float fragmentation = 1.0f - ((float)mem_state.status.largest_free_block_kb / 
                                  (float)mem_state.status.internal_free_kb);
    mem_state.status.fragmentation_detected = (fragmentation > FRAGMENTATION_THRESHOLD);
    
    // Update task stack watermarks
    mem_state.status.task_count = 0;
    
#if CONFIG_FREERTOS_USE_TRACE_FACILITY
    TaskStatus_t* task_status = pvPortMalloc(uxTaskGetNumberOfTasks() * sizeof(TaskStatus_t));
    
    if (task_status) {
        UBaseType_t task_count = uxTaskGetSystemState(task_status, uxTaskGetNumberOfTasks(), NULL);
        
        for (int i = 0; i < task_count && i < 32; i++) {
            mem_state.status.tasks[i].name = task_status[i].pcTaskName;
            mem_state.status.tasks[i].stack_hwm = task_status[i].usStackHighWaterMark;
            
            // Core ID is only available in certain FreeRTOS configurations
            #if (configTASKLIST_INCLUDE_COREID == 1)
                mem_state.status.tasks[i].core_id = task_status[i].xCoreID;
            #else
                // Fallback: try to determine core ID or set to unknown
                mem_state.status.tasks[i].core_id = -1; // Unknown/not available
            #endif
            
            mem_state.status.task_count++;
        }
        
        vPortFree(task_status);
    }
#else
    // Task monitoring not available - trace facility disabled
    ESP_LOGD(TAG, "Task monitoring disabled (CONFIG_FREERTOS_USE_TRACE_FACILITY=n)");
#endif
}

esp_err_t memory_manager_init(void)
{
    if (mem_state.initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing memory manager");
    
    // Detect flash size
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    mem_state.status.flash_size_mb = flash_size / (1024 * 1024);
    
    // Detect PSRAM using heap capabilities
    size_t psram_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    mem_state.status.has_psram = (psram_size > 0);
    if (mem_state.status.has_psram) {
        mem_state.status.psram_size_mb = psram_size / (1024 * 1024);
        ESP_LOGI(TAG, "✅ PSRAM detected: %u MB", mem_state.status.psram_size_mb);
    } else {
        ESP_LOGW(TAG, "⚠️ No PSRAM detected - running in limited mode");
    }
    
    ESP_LOGI(TAG, "Flash: %u MB", mem_state.status.flash_size_mb);
    
    // Initial status update
    update_memory_status();
    
    mem_state.initialized = true;
    
    memory_manager_print_status();
    
    return ESP_OK;
}

void* mm_alloc(size_t size, memory_policy_t policy, const char* tag)
{
    if (!mem_state.initialized) {
        ESP_LOGE(TAG, "Memory manager not initialized!");
        return NULL;
    }
    
    void* ptr = NULL;
    uint32_t caps = 0;
    
    mem_state.allocation_count++;
    
    switch (policy) {
        case MEM_POLICY_PREFER_PSRAM:
            if (mem_state.status.has_psram && !mem_state.status.low_psram_memory) {
                ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
                if (ptr) {
                    ESP_LOGD(TAG, "[%s] Allocated %u bytes in PSRAM", tag, size);
                    return ptr;
                }
            }
            // Fallback to internal
            caps = MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL;
            break;
            
        case MEM_POLICY_REQUIRE_INTERNAL:
            caps = MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL;
            break;
            
        case MEM_POLICY_REQUIRE_DMA:
            caps = MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL;
            break;
            
        case MEM_POLICY_ADAPTIVE:
            // Choose based on current memory status
            if (mem_state.status.low_internal_memory && mem_state.status.has_psram) {
                caps = MALLOC_CAP_SPIRAM;
            } else {
                caps = MALLOC_CAP_DEFAULT;
            }
            break;
    }
    
    ptr = heap_caps_malloc(size, caps);
    
    if (ptr) {
        ESP_LOGD(TAG, "[%s] Allocated %u bytes (caps=0x%lx)", tag, (unsigned)size, (unsigned long)caps);
    } else {
        mem_state.allocation_failures++;
        ESP_LOGE(TAG, "[%s] Failed to allocate %u bytes (caps=0x%lx)", tag, (unsigned)size, (unsigned long)caps);
        
        // Try emergency cleanup
        if (mem_state.allocation_failures > 10) {
            ESP_LOGE(TAG, "Too many allocation failures, attempting cleanup...");
            memory_manager_adjust_for_pressure();
        }
    }
    
    return ptr;
}

void* mm_calloc(size_t n, size_t size, memory_policy_t policy, const char* tag)
{
    void* ptr = mm_alloc(n * size, policy, tag);
    if (ptr) {
        memset(ptr, 0, n * size);
    }
    return ptr;
}

void* mm_realloc(void* ptr, size_t size, memory_policy_t policy, const char* tag)
{
    if (!ptr) {
        return mm_alloc(size, policy, tag);
    }
    
    // For realloc, try to keep in same memory type
    uint32_t caps = MALLOC_CAP_DEFAULT;
    if (heap_caps_get_allocated_size(ptr) > 0) {
        // Determine current location
        if (esp_ptr_in_dram(ptr)) {
            caps = MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL;
        } else if (esp_ptr_external_ram(ptr)) {
            caps = MALLOC_CAP_SPIRAM;
        }
    }
    
    void* new_ptr = heap_caps_realloc(ptr, size, caps);
    if (!new_ptr) {
        ESP_LOGE(TAG, "[%s] Realloc failed for %u bytes", tag, size);
    }
    
    return new_ptr;
}

void mm_free(void* ptr)
{
    if (ptr) {
        heap_caps_free(ptr);
    }
}

void memory_manager_get_status(memory_status_t* status)
{
    if (!mem_state.initialized || !status) {
        return;
    }
    
    update_memory_status();
    memcpy(status, &mem_state.status, sizeof(memory_status_t));
}

void memory_manager_print_status(void)
{
    update_memory_status();
    
    ESP_LOGI(TAG, "========== Memory Status ==========");
    ESP_LOGI(TAG, "Flash: %u MB | PSRAM: %s (%u MB)",
             mem_state.status.flash_size_mb,
             mem_state.status.has_psram ? "Yes" : "No",
             mem_state.status.psram_size_mb);
    
    ESP_LOGI(TAG, "Internal RAM:");
    ESP_LOGI(TAG, "  Total: %u KB", mem_state.status.internal_total_kb);
    ESP_LOGI(TAG, "  Free: %u KB (min: %u KB)",
             mem_state.status.internal_free_kb,
             mem_state.status.internal_min_free_kb);
    
    if (mem_state.status.has_psram) {
        ESP_LOGI(TAG, "PSRAM:");
        ESP_LOGI(TAG, "  Total: %u KB", mem_state.status.psram_total_kb);
        ESP_LOGI(TAG, "  Free: %u KB (min: %u KB)",
                 mem_state.status.psram_free_kb,
                 mem_state.status.psram_min_free_kb);
    }
    
    ESP_LOGI(TAG, "DMA Free: %u KB", mem_state.status.dma_free_kb);
    ESP_LOGI(TAG, "Largest Block: %u KB", mem_state.status.largest_free_block_kb);
    
    if (mem_state.status.fragmentation_detected) {
        ESP_LOGW(TAG, "⚠️ Memory fragmentation detected!");
    }
    
    ESP_LOGI(TAG, "Allocations: %lu (failures: %lu)",
             (unsigned long)mem_state.allocation_count,
             (unsigned long)mem_state.allocation_failures);
    ESP_LOGI(TAG, "===================================");
}

void memory_manager_print_tasks(void)
{
    update_memory_status();
    
#if CONFIG_FREERTOS_USE_TRACE_FACILITY
    if (mem_state.status.task_count == 0) {
        ESP_LOGW(TAG, "No task information available");
        return;
    }
    
    ESP_LOGI(TAG, "========== Task Stack Usage ==========");
    ESP_LOGI(TAG, "%-16s | Core | Stack HWM | Usage", "Task");
    ESP_LOGI(TAG, "--------------------------------------");
    
    for (int i = 0; i < mem_state.status.task_count; i++) {
        int usage_percent = 100 - (mem_state.status.tasks[i].stack_hwm * 100 / 
                                   (mem_state.status.tasks[i].stack_hwm * 4)); // Rough estimate
        
        const char* warning = "";
        if (mem_state.status.tasks[i].stack_hwm < 512) {
            warning = " ⚠️";
        }
        
        // Handle case where core ID is not available
        if (mem_state.status.tasks[i].core_id >= 0) {
            ESP_LOGI(TAG, "%-16s |  %d   | %6u | ~%d%%%s",
                     mem_state.status.tasks[i].name,
                     mem_state.status.tasks[i].core_id,
                     mem_state.status.tasks[i].stack_hwm,
                     usage_percent,
                     warning);
        } else {
            ESP_LOGI(TAG, "%-16s |  ?   | %6u | ~%d%%%s",
                     mem_state.status.tasks[i].name,
                     mem_state.status.tasks[i].stack_hwm,
                     usage_percent,
                     warning);
        }
    }
    ESP_LOGI(TAG, "======================================");
#else
    ESP_LOGI(TAG, "========== Task Stack Usage ==========");
    ESP_LOGI(TAG, "Task monitoring disabled (CONFIG_FREERTOS_USE_TRACE_FACILITY=n)");
    ESP_LOGI(TAG, "Enable CONFIG_FREERTOS_USE_TRACE_FACILITY for task details");
    ESP_LOGI(TAG, "======================================");
#endif
}

bool memory_manager_check_pressure(void)
{
    update_memory_status();
    return mem_state.status.low_internal_memory || 
           mem_state.status.low_psram_memory ||
           mem_state.status.fragmentation_detected;
}

esp_err_t memory_manager_adjust_for_pressure(void)
{
    ESP_LOGW(TAG, "Adjusting for memory pressure...");
    
    // Force garbage collection if possible
    // Note: ESP32 doesn't have GC, but we can suggest cleanup
    
    update_memory_status();
    
    if (mem_state.status.fragmentation_detected) {
        ESP_LOGW(TAG, "Fragmentation detected - consider restart");
    }
    
    return ESP_OK;
}

void memory_manager_enable_monitoring(uint32_t interval_ms)
{
    if (mem_state.monitor_timer) {
        esp_timer_stop(mem_state.monitor_timer);
        esp_timer_delete(mem_state.monitor_timer);
    }
    
    esp_timer_create_args_t timer_args = {
        .callback = memory_monitor_cb,
        .name = "mem_monitor"
    };
    
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &mem_state.monitor_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(mem_state.monitor_timer, interval_ms * 1000));
    
    ESP_LOGI(TAG, "Memory monitoring enabled (interval: %lu ms)", (unsigned long)interval_ms);
}

// Feature capability checks
bool mem_can_enable_vision(void)
{
    // Vision needs at least 100KB internal + ideally PSRAM
    return (mem_state.status.internal_free_kb > 100) && 
           (mem_state.status.has_psram || mem_state.status.internal_free_kb > 200);
}

bool mem_can_enable_hd_video(void)
{
    // HD needs PSRAM with at least 2MB free
    return mem_state.status.has_psram && (mem_state.status.psram_free_kb > 2048);
}