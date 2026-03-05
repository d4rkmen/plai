/**
 * @file button.h
 * @brief Button class for debouncing and reading button state
 * @author d4rkmen
 * @license Apache License 2.0
 */
#pragma once

#include <stdint.h>

class Button
{
public:
    Button(uint8_t pin, uint16_t debounce_ms = 100);
    bool read();
    bool is_toggled();
    bool is_pressed();
    bool is_released();
    bool has_changed();

    const static bool PRESSED = false;
    const static bool RELEASED = true;

private:
    uint8_t _pin;
    uint16_t _delay;
    bool _state;
    uint32_t _ignore_until;
    bool _has_changed;
};
