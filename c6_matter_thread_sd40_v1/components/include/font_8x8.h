/*
 * font_8x8.h
 *
 * 8x8 pixel monospace font header
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 8x8 pixel font data array
 *
 * Each character is stored as 8 bytes, one per column.
 * Characters range from ASCII 32 (space) to 126 (~).
 */
extern const uint8_t font_8x8[95][8];

/**
 * @brief Get font data for a character
 *
 * @param[in] c ASCII character (32-126)
 * @return Pointer to 8-byte font data, or space if out of range
 */
const uint8_t *font_8x8_get_char(char c);

#ifdef __cplusplus
}
#endif
