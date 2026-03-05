/**
 * @file key_repeat.h
 * @brief Key repeat / auto-fire helper for held keys
 */
#pragma once

#include <cstdint>

#ifndef KEY_HOLD_MS
#define KEY_HOLD_MS 500
#endif
#ifndef KEY_REPEAT_MS
#define KEY_REPEAT_MS 100
#endif

// Evaluate key repeat logic. Returns true when action should fire.
// is_repeat   - static bool tracking repeat state (caller-owned)
// next_fire   - static uint32_t tracking next fire time (caller-owned)
// now         - current millis()
bool key_repeat_check(bool& is_repeat, uint32_t& next_fire, uint32_t now);
