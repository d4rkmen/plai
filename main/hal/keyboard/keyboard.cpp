/**
 * @file keyboard.cpp
 * @author Forairaaaaa
 * @brief
 * @version 0.2
 * @date 2023-09-22
 *
 * @copyright Copyright (c) 2023
 *
 */
#include "keyboard.h"
#include "keyboard_reader_iomatrix.h"
#include "keyboard_reader_tca8418.h"
#include <driver/gpio.h>
#include "common_define.h"
#include "esp_log.h"

#define TAG "KEYBOARD"

using namespace KEYBOARD;

void Keyboard::init()
{
    // Detect board type if set to AUTO_DETECT
    if (_board_type == HAL::BoardType::AUTO_DETECT)
    {
        ESP_LOGI(TAG, "Auto-detecting board type...");

        // Try to initialize TCA8418 keyboard reader
        auto tca8418_reader = std::make_unique<TCA8418KeyboardReader>(_hal);
        tca8418_reader->init();

        if (tca8418_reader->isInitialized())
        {
            ESP_LOGI(TAG, "TCA8418 initialized successfully - using CARDPUTER_ADV mode");
            _board_type = HAL::BoardType::CARDPUTER_ADV;
            _keyboard_reader = std::move(tca8418_reader);
        }
        else
        {
            ESP_LOGI(TAG, "TCA8418 initialization failed - using CARDPUTER mode");
            _board_type = HAL::BoardType::CARDPUTER;
            _keyboard_reader = std::make_unique<IOMatrixKeyboardReader>();
            _keyboard_reader->init();
        }
    }
    else if (_board_type == HAL::BoardType::CARDPUTER_ADV)
    {
        ESP_LOGI(TAG, "Board type forced to CARDPUTER_ADV");
        _keyboard_reader = std::make_unique<TCA8418KeyboardReader>(_hal, KEYBOARD_TCA8418_INT_PIN);
        _keyboard_reader->init();
    }
    else
    {
        ESP_LOGI(TAG, "Board type forced to CARDPUTER");
        _keyboard_reader = std::make_unique<IOMatrixKeyboardReader>();
        _keyboard_reader->init();
    }

    _last_pressed_time = millis();
}

uint8_t Keyboard::getKeyNum(Point2D_t keyCoor)
{
    uint8_t ret = 0;

    if ((keyCoor.x < 0) || (keyCoor.y < 0))
    {
        return 0;
    }

    ret = (keyCoor.y * 14) + (keyCoor.x + 1);

    return ret;
}

void Keyboard::updateKeyList()
{
    if (_keyboard_reader)
    {
        _keyboard_reader->update();

        const auto& raw_keys = _keyboard_reader->keyList();

        // Clear suppress flag when wake key is released
        if (_suppress_key_until_release && raw_keys.empty())
        {
            _suppress_key_until_release = false;
        }

        // When dimmed, key press wakes screen but key is not processed
        if (_is_dimmed && !raw_keys.empty())
        {
            setDimmed(false);
            _suppress_key_until_release = true;
        }

        // Update last pressed time if keys are pressed (reset dim idle timer)
        if (!raw_keys.empty())
        {
            _last_pressed_time = millis();
        }

        // Dim timeout logic: dim when idle, undim when recent activity
        uint32_t now = millis();
        if (_dim_time_ms > 0)
        {
            if ((now - _last_pressed_time) > _dim_time_ms)
            {
                if (!_is_dimmed)
                    setDimmed(true);
            }
            else
            {
                if (_is_dimmed)
                    setDimmed(false);
            }
        }
    }
}

bool Keyboard::isKeyPressing(int keyNum)
{
    const auto& keys = keyList();
    for (const auto& key : keys)
    {
        if (getKeyNum(key) == keyNum)
            return true;
    }
    return false;
}

bool Keyboard::waitForRelease(int keyNum, int timeout_ms)
{
    const uint32_t start = millis();
    while (isKeyPressing(keyNum))
    {
        delay(10);
        updateKeyList();
        if (timeout_ms && (millis() - start > static_cast<uint32_t>(timeout_ms)))
        {
            return false;
        }
    }
    return true;
}

void Keyboard::updateKeysState()
{
    _keys_state_buffer.reset();
    _key_values_without_special_keys.clear();

    const auto& keys = keyList();

    // Process all keys in one pass
    for (const auto& key : keys)
    {
        const auto& key_value = getKeyValue(key);

        switch (key_value.key_type)
        {
        case KeyType::TAB:
            _keys_state_buffer.tab = true;
            break;
        case KeyType::FN:
            _keys_state_buffer.fn = true;
            break;
        case KeyType::SHIFT:
            _keys_state_buffer.shift = true;
            break;
        case KeyType::CTRL:
            _keys_state_buffer.ctrl = true;
            break;
        case KeyType::OPT:
            _keys_state_buffer.opt = true;
            break;
        case KeyType::ALT:
            _keys_state_buffer.alt = true;
            break;
        case KeyType::DEL:
            _keys_state_buffer.del = true;
            break;
        case KeyType::ENTER:
            _keys_state_buffer.enter = true;
            break;
        case KeyType::SPACE:
            _keys_state_buffer.space = true;
            break;
        case KeyType::REGULAR:
        default:
            _key_values_without_special_keys.push_back(key);
            break;
        }
    }

    // Process regular keys
    const bool modifier_active = _keys_state_buffer.ctrl || _keys_state_buffer.shift || _is_caps_locked;
    for (const auto& key : _key_values_without_special_keys)
    {
        const auto& key_value = getKeyValue(key);
        _keys_state_buffer.values.push_back(modifier_active ? *key_value.value_second : *key_value.value_first);
    }
}

bool Keyboard::isChanged()
{
    const uint8_t current_size = keyList().size();
    const bool changed = (_last_key_size != current_size);
    _last_key_size = current_size;
    return changed;
}

uint32_t Keyboard::lastPressedTime() const { return _last_pressed_time; }
void Keyboard::resetLastPressedTime() { _last_pressed_time = millis(); }
void Keyboard::setDimmed(bool value)
{
    if (_is_dimmed != value)
    {
        _is_dimmed = value;
        if (_dimmed_callback)
            _dimmed_callback(_is_dimmed);
    }
}
bool Keyboard::isDimmed() const { return _is_dimmed; }
void Keyboard::setDimmedCallback(std::function<void(bool)> cb) { _dimmed_callback = std::move(cb); }
void Keyboard::set_dim_time(uint32_t ms) { _dim_time_ms = ms; }