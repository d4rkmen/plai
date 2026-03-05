/**
 * @file keyboard_reader.h
 * @brief Abstract interface for keyboard hardware reading implementations
 * @version 0.1
 * @date 2025-11-08
 *
 * @copyright Copyright (c) 2025
 */
#pragma once
#include <vector>
#include <stdint.h>

namespace KEYBOARD
{
    struct Point2D_t
    {
        int x;
        int y;

        bool operator==(const Point2D_t& other) const { return x == other.x && y == other.y; }
    };

    struct Chart_t
    {
        uint8_t value;
        uint8_t x_1;
        uint8_t x_2;
    };

    /**
     * @brief Abstract base class for keyboard reading implementations
     *
     * This class provides a common interface for different keyboard hardware
     * implementations (IOMatrix for CARDPUTER, TCA8418 for CARDPUTER_ADV)
     */
    class KeyboardReader
    {
    public:
        virtual ~KeyboardReader() = default;

        /**
         * @brief Initialize the keyboard reader hardware
         */
        virtual void init() = 0;

        /**
         * @brief Update the keyboard state and key list
         *
         * This method should scan the keyboard hardware and update
         * the internal key list with currently pressed keys
         */
        virtual void update() = 0;

        /**
         * @brief Get the current list of pressed keys
         * @return Reference to the key list
         */
        inline const std::vector<Point2D_t>& keyList() const { return _key_list; }

    protected:
        std::vector<Point2D_t> _key_list;
    };

} // namespace KEYBOARD
