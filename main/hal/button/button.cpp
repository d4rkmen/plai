/**
 * @file button.cpp
 * @brief Button class for debouncing and reading button state
 * @author d4rkmen
 * @license Apache License 2.0
 */
#include "button.h"
#include <driver/gpio.h>
#include "common_define.h"

Button::Button(uint8_t pin, uint16_t debounce_ms)
    : _pin(pin), _delay(debounce_ms), _state(true), _ignore_until(0), _has_changed(false)
{
    gpio_set_direction((gpio_num_t)_pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)_pin, GPIO_PULLUP_ONLY);
}

//
// public methods
//

bool Button::read()
{
    // ignore pin changes until after this delay time
    if (_ignore_until > millis())
    {
        // ignore any changes during this period
    }

    // pin has changed
    // else if (digitalRead(_pin) != _state)
    else if (gpio_get_level((gpio_num_t)_pin) != _state)
    {
        _ignore_until = millis() + _delay;
        _state = !_state;
        _has_changed = true;
    }

    return _state;
}

// has the button been toggled from on -> off, or vice versa
bool Button::is_toggled()
{
    read();
    return has_changed();
}

// mostly internal, tells you if a button has changed after calling the read() function
bool Button::has_changed()
{
    if (_has_changed)
    {
        _has_changed = false;
        return true;
    }
    return false;
}

// has the button gone from off -> on
bool Button::is_pressed() { return (read() == PRESSED && has_changed()); }

// has the button gone from on -> off
bool Button::is_released() { return (read() == RELEASED && has_changed()); }
