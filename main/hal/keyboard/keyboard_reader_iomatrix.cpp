/**
 * @file keyboard_reader_iomatrix.cpp
 * @brief GPIO matrix keyboard reader implementation for CARDPUTER
 * @version 0.1
 * @date 2025-11-08
 *
 * @copyright Copyright (c) 2025
 */
#include "keyboard_reader_iomatrix.h"
#include <driver/gpio.h>

#define digitalWrite(pin, level) gpio_set_level((gpio_num_t)pin, level)
#define digitalRead(pin) gpio_get_level((gpio_num_t)pin)

namespace KEYBOARD
{
    void IOMatrixKeyboardReader::_set_output(const std::vector<int>& pinList, uint8_t output)
    {
        output &= 0x07; // Mask to 3 bits
        gpio_set_level((gpio_num_t)pinList[0], output & 0x01);
        gpio_set_level((gpio_num_t)pinList[1], (output >> 1) & 0x01);
        gpio_set_level((gpio_num_t)pinList[2], (output >> 2) & 0x01);
    }

    uint8_t IOMatrixKeyboardReader::_get_input(const std::vector<int>& pinList)
    {
        uint8_t buffer = 0;
        for (int i = 0; i < 7; i++)
        {
            if (!gpio_get_level((gpio_num_t)pinList[i]))
            {
                buffer |= (1 << i);
            }
        }
        return buffer;
    }

    void IOMatrixKeyboardReader::init()
    {
        // Configure output pins
        for (auto i : output_list)
        {
            gpio_reset_pin((gpio_num_t)i);
            gpio_set_direction((gpio_num_t)i, GPIO_MODE_OUTPUT);
            gpio_set_pull_mode((gpio_num_t)i, GPIO_PULLUP_PULLDOWN);
            digitalWrite(i, 0);
        }

        // Configure input pins
        for (auto i : input_list)
        {
            gpio_reset_pin((gpio_num_t)i);
            gpio_set_direction((gpio_num_t)i, GPIO_MODE_INPUT);
            gpio_set_pull_mode((gpio_num_t)i, GPIO_PULLUP_ONLY);
        }

        _set_output(output_list, 0);
    }

    void IOMatrixKeyboardReader::update()
    {
        _key_list.clear();

        Point2D_t coor;
        uint8_t input_value = 0;

        for (int i = 0; i < 8; i++)
        {
            _set_output(output_list, i);
            input_value = _get_input(input_list);

            /* If key pressed */
            if (input_value)
            {
                /* Get X and Y for each pressed key in this scan */
                for (int j = 0; j < 7; j++)
                {
                    if (input_value & (0x01 << j))
                    {
                        coor.x = (i > 3) ? X_map_chart[j].x_1 : X_map_chart[j].x_2;

                        /* Get Y */
                        coor.y = (i > 3) ? (i - 4) : i;

                        /* Keep the same as picture */
                        coor.y = -coor.y;
                        coor.y = coor.y + 3;

                        _key_list.push_back(coor);
                    }
                }
            }
        }
    }

} // namespace KEYBOARD
