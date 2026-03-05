/**
 * @file keyboard_reader_iomatrix.h
 * @brief GPIO matrix keyboard reader for CARDPUTER
 * @version 0.1
 * @date 2025-11-08
 *
 * @copyright Copyright (c) 2025
 */
#pragma once
#include "keyboard_reader.h"
#include <driver/gpio.h>

namespace KEYBOARD
{
    /**
     * @brief IO Matrix keyboard reader implementation for CARDPUTER
     *
     * This implementation uses GPIO matrix scanning to read keyboard state.
     * It scans 8 outputs and 7 inputs to detect key presses.
     */
    class IOMatrixKeyboardReader : public KeyboardReader
    {
    public:
        IOMatrixKeyboardReader() = default;
        virtual ~IOMatrixKeyboardReader() = default;

        void init() override;
        void update() override;

    private:
        const std::vector<int> output_list = {8, 9, 11};
        const std::vector<int> input_list = {13, 15, 3, 4, 5, 6, 7};

        const Chart_t X_map_chart[7] = {{1, 0, 1}, {2, 2, 3}, {4, 4, 5}, {8, 6, 7}, {16, 8, 9}, {32, 10, 11}, {64, 12, 13}};

        void _set_output(const std::vector<int>& pinList, uint8_t output);
        uint8_t _get_input(const std::vector<int>& pinList);
    };

} // namespace KEYBOARD
