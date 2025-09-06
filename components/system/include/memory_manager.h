/*
 * Memory Manager
 * Runtime memory detection, allocation policies, and monitoring
 */

#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // Hardware detected at runtime
    size_t flash_size_mb;
    size_t psram_size_mb;
    bool has_psram;
    
    // Memory status
    size_t internal_total_kb;
    size_t internal_free_kb;
    size_t internal_min_free_kb;
    size_t psram_total_kb;
    size_t psram_free_kb;
    size_t psram_min_free_kb;
    size_t dma_free_kb;
    size_t largest_free_block_kb;
    
    // Task watermarks
    struct {
        const char* name;
        UBaseType_t stack_hwm;  // High water mark in words
        size_t stack_size;
        int core_id;
    } tasks[32];
    int task_count;
    
    // Memory pressure flags
    bool low_internal_memory;
    bool low_psram_memory;
    bool fragmentation_detected;
} memory_status_t;

typedef enum {
    MEM_POLICY_PREFER_PSRAM,    // Try PSRAM first, fallback to internal
    MEM_POLICY_REQUIRE_INTERNAL, // Must be internal (for DMA, ISR, etc)
    MEM_POLICY_REQUIRE_DMA,      // Must be DMA capable
    MEM_POLICY_ADAPTIVE          // Adjust based on current memory
} memory_policy_t;

// Initialize memory manager
esp_err_t memory_manager_init(void);

// Allocation functions with policies - prefixed to avoid LWIP conflicts
void* mm_alloc(size_t size, memory_policy_t policy, const char* tag);
void* mm_calloc(size_t n, size_t size, memory_policy_t policy, const char* tag);
void* mm_realloc(void* ptr, size_t size, memory_policy_t policy, const char* tag);
void mm_free(void* ptr);

// Macros for backward compatibility - use these in code
#define mem_alloc mm_alloc
#define mem_calloc mm_calloc
#define mem_realloc mm_realloc
#define mem_free mm_free

// Get current memory status
void memory_manager_get_status(memory_status_t* status);
void memory_manager_print_status(void);
void memory_manager_print_tasks(void);

// Monitoring and alerts
bool memory_manager_check_pressure(void);
esp_err_t memory_manager_adjust_for_pressure(void);
void memory_manager_enable_monitoring(uint32_t interval_ms);

// Feature capability checks based on memory
bool mem_can_enable_vision(void);
bool mem_can_enable_hd_video(void);

// Heap tracing helpers
void memory_manager_start_trace(void);
void memory_manager_stop_trace(void);
void memory_manager_dump_trace(void);

#ifdef __cplusplus
}
#endif

#endif // MEMORY_MANAGER_H