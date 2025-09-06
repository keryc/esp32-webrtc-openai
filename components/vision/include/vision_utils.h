#ifndef VISION_UTILS_H
#define VISION_UTILS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Encode JPEG data to base64 string
 * 
 * @param jpeg_data Input JPEG data
 * @param jpeg_size Size of JPEG data
 * @return Allocated base64 string (must be freed) or NULL on error
 */
char* vision_utils_encode_base64(const uint8_t *jpeg_data, size_t jpeg_size);

#ifdef __cplusplus
}
#endif

#endif // VISION_UTILS_H