/**
 * @file keyboard.h
 * @author Forairaaaaa
 * @brief
 * @version 0.1
 * @date 2023-09-22
 *
 * @copyright Copyright (c) 2023
 *
 */
#pragma once
#include <functional>
#include <iostream>
#include <memory>
#include <vector>
#include "board.h"
#include "keyboard_reader.h"

// Forward declaration to avoid circular dependency
namespace HAL
{
    class Hal;
}

#define KEY_A 0x04 // Keyboard a and A
#define KEY_B 0x05 // Keyboard b and B
#define KEY_C 0x06 // Keyboard c and C
#define KEY_D 0x07 // Keyboard d and D
#define KEY_E 0x08 // Keyboard e and E
#define KEY_F 0x09 // Keyboard f and F
#define KEY_G 0x0a // Keyboard g and G
#define KEY_H 0x0b // Keyboard h and H
#define KEY_I 0x0c // Keyboard i and I
#define KEY_J 0x0d // Keyboard j and J
#define KEY_K 0x0e // Keyboard k and K
#define KEY_L 0x0f // Keyboard l and L
#define KEY_M 0x10 // Keyboard m and M
#define KEY_N 0x11 // Keyboard n and N
#define KEY_O 0x12 // Keyboard o and O
#define KEY_P 0x13 // Keyboard p and P
#define KEY_Q 0x14 // Keyboard q and Q
#define KEY_R 0x15 // Keyboard r and R
#define KEY_S 0x16 // Keyboard s and S
#define KEY_T 0x17 // Keyboard t and T
#define KEY_U 0x18 // Keyboard u and U
#define KEY_V 0x19 // Keyboard v and V
#define KEY_W 0x1a // Keyboard w and W
#define KEY_X 0x1b // Keyboard x and X
#define KEY_Y 0x1c // Keyboard y and Y
#define KEY_Z 0x1d // Keyboard z and Z

#define KEY_1 0x1e // Keyboard 1 and !
#define KEY_2 0x1f // Keyboard 2 and @
#define KEY_3 0x20 // Keyboard 3 and #
#define KEY_4 0x21 // Keyboard 4 and $
#define KEY_5 0x22 // Keyboard 5 and %
#define KEY_6 0x23 // Keyboard 6 and ^
#define KEY_7 0x24 // Keyboard 7 and &
#define KEY_8 0x25 // Keyboard 8 and *
#define KEY_9 0x26 // Keyboard 9 and (
#define KEY_0 0x27 // Keyboard 0 and )

#define KEY_ENTER 0x28      // Keyboard Return (ENTER)
#define KEY_ESC 0x29        // Keyboard ESCAPE
#define KEY_BACKSPACE 0x2a  // Keyboard DELETE (Backspace)
#define KEY_TAB 0x2b        // Keyboard Tab
#define KEY_SPACE 0x2c      // Keyboard Spacebar
#define KEY_MINUS 0x2d      // Keyboard - and _
#define KEY_EQUAL 0x2e      // Keyboard = and +
#define KEY_LEFTBRACE 0x2f  // Keyboard [ and {
#define KEY_RIGHTBRACE 0x30 // Keyboard ] and }
#define KEY_BACKSLASH 0x31  // Keyboard \ and |
#define KEY_HASHTILDE 0x32  // Keyboard Non-US # and ~
#define KEY_SEMICOLON 0x33  // Keyboard ; and :
#define KEY_APOSTROPHE 0x34 // Keyboard ' and "
#define KEY_GRAVE 0x35      // Keyboard ` and ~
#define KEY_COMMA 0x36      // Keyboard , and <
#define KEY_DOT 0x37        // Keyboard . and >
#define KEY_SLASH 0x38      // Keyboard / and ?
#define KEY_CAPSLOCK 0x39   // Keyboard Caps Lock

#define KEY_KPSLASH 0x54      // Keypad /
#define KEY_KPASTERISK 0x55   // Keypad *
#define KEY_KPMINUS 0x56      // Keypad -
#define KEY_KPPLUS 0x57       // Keypad +
#define KEY_KPENTER 0x58      // Keypad ENTER
#define KEY_KPLEFTPAREN 0xb6  // Keypad (
#define KEY_KPRIGHTPAREN 0xb7 // Keypad )
#define KEY_LEFTCTRL 0xe0     // Keyboard Left Control
#define KEY_LEFTALT 0xe2      // Keyboard Left Alt

#define KEY_NUM_ESC 1
#define KEY_NUM_1 2
#define KEY_NUM_2 3
#define KEY_NUM_3 4
#define KEY_NUM_4 5
#define KEY_NUM_5 6
#define KEY_NUM_6 7
#define KEY_NUM_7 8
#define KEY_NUM_8 9
#define KEY_NUM_9 10
#define KEY_NUM_0 11
#define KEY_NUM_UNDERSCORE 12
#define KEY_NUM_EQUAL 13
#define KEY_NUM_BACKSPACE 14
#define KEY_NUM_TAB 15
#define KEY_NUM_Q 16
#define KEY_NUM_W 17
#define KEY_NUM_E 18
#define KEY_NUM_R 19
#define KEY_NUM_T 20
#define KEY_NUM_Y 21
#define KEY_NUM_U 22
#define KEY_NUM_I 23
#define KEY_NUM_O 24
#define KEY_NUM_P 25
#define KEY_NUM_LEFTBRACE 26
#define KEY_NUM_RIGHTBRACE 27
#define KEY_NUM_BACKSLASH 28
#define KEY_NUM_FN 29
#define KEY_NUM_SHIFT 30
#define KEY_NUM_A 31
#define KEY_NUM_S 32
#define KEY_NUM_D 33
#define KEY_NUM_F 34
#define KEY_NUM_G 35
#define KEY_NUM_H 36
#define KEY_NUM_J 37
#define KEY_NUM_K 38
#define KEY_NUM_L 39
#define KEY_NUM_UP 40
#define KEY_NUM_APOSTROPHE 41
#define KEY_NUM_ENTER 42
#define KEY_NUM_CTRL 43
#define KEY_NUM_OPT 44
#define KEY_NUM_ALT 45
#define KEY_NUM_Z 46
#define KEY_NUM_X 47
#define KEY_NUM_C 48
#define KEY_NUM_V 49
#define KEY_NUM_B 50
#define KEY_NUM_N 51
#define KEY_NUM_M 52
#define KEY_NUM_LEFT 53
#define KEY_NUM_DOWN 54
#define KEY_NUM_RIGHT 55
#define KEY_NUM_SPACE 56

namespace KEYBOARD
{

    enum class KeyType : uint8_t
    {
        REGULAR = 0,
        TAB = 1,
        FN = 2,
        SHIFT = 3,
        CTRL = 4,
        OPT = 5,
        ALT = 6,
        DEL = 7,
        ENTER = 8,
        SPACE = 9
    };

    const std::vector<int> output_list = {8, 9, 11};
    const std::vector<int> input_list = {13, 15, 3, 4, 5, 6, 7};

    const Chart_t X_map_chart[7] = {{1, 0, 1}, {2, 2, 3}, {4, 4, 5}, {8, 6, 7}, {16, 8, 9}, {32, 10, 11}, {64, 12, 13}};

    struct KeyValue_t
    {
        const char* value_first;
        const int value_num_first;
        const char* value_second;
        const int value_num_second;
        const KeyType key_type;
    };

    const KeyValue_t _key_value_map[4][14] = {{{"`", KEY_GRAVE, "~", KEY_GRAVE, KeyType::REGULAR},
                                               {"1", KEY_1, "!", KEY_1, KeyType::REGULAR},
                                               {"2", KEY_2, "@", KEY_2, KeyType::REGULAR},
                                               {"3", KEY_3, "#", KEY_3, KeyType::REGULAR},
                                               {"4", KEY_4, "$", KEY_4, KeyType::REGULAR},
                                               {"5", KEY_5, "%", KEY_5, KeyType::REGULAR},
                                               {"6", KEY_6, "^", KEY_6, KeyType::REGULAR},
                                               {"7", KEY_7, "&", KEY_7, KeyType::REGULAR},
                                               {"8", KEY_8, "*", KEY_KPASTERISK, KeyType::REGULAR},
                                               {"9", KEY_9, "(", KEY_KPLEFTPAREN, KeyType::REGULAR},
                                               {"0", KEY_0, ")", KEY_KPRIGHTPAREN, KeyType::REGULAR},
                                               {"-", KEY_MINUS, "_", KEY_KPMINUS, KeyType::REGULAR},
                                               {"=", KEY_EQUAL, "+", KEY_KPPLUS, KeyType::REGULAR},
                                               {"del", KEY_BACKSPACE, "del", KEY_BACKSPACE, KeyType::DEL}},
                                              {{"tab", KEY_TAB, "tab", KEY_TAB, KeyType::TAB},
                                               {"q", KEY_Q, "Q", KEY_Q, KeyType::REGULAR},
                                               {"w", KEY_W, "W", KEY_W, KeyType::REGULAR},
                                               {"e", KEY_E, "E", KEY_E, KeyType::REGULAR},
                                               {"r", KEY_R, "R", KEY_R, KeyType::REGULAR},
                                               {"t", KEY_T, "T", KEY_T, KeyType::REGULAR},
                                               {"y", KEY_Y, "Y", KEY_Y, KeyType::REGULAR},
                                               {"u", KEY_U, "U", KEY_U, KeyType::REGULAR},
                                               {"i", KEY_I, "I", KEY_I, KeyType::REGULAR},
                                               {"o", KEY_O, "O", KEY_O, KeyType::REGULAR},
                                               {"p", KEY_P, "P", KEY_P, KeyType::REGULAR},
                                               {"[", KEY_LEFTBRACE, "{", KEY_LEFTBRACE, KeyType::REGULAR},
                                               {"]", KEY_RIGHTBRACE, "}", KEY_RIGHTBRACE, KeyType::REGULAR},
                                               {"\\", KEY_BACKSLASH, "|", KEY_BACKSLASH, KeyType::REGULAR}},
                                              {{"fn", 0, "fn", 0, KeyType::FN},
                                               {"shift", 0, "shift", 0, KeyType::SHIFT},
                                               {"a", KEY_A, "A", KEY_A, KeyType::REGULAR},
                                               {"s", KEY_S, "S", KEY_S, KeyType::REGULAR},
                                               {"d", KEY_D, "D", KEY_D, KeyType::REGULAR},
                                               {"f", KEY_F, "F", KEY_F, KeyType::REGULAR},
                                               {"g", KEY_G, "G", KEY_G, KeyType::REGULAR},
                                               {"h", KEY_H, "H", KEY_H, KeyType::REGULAR},
                                               {"j", KEY_J, "J", KEY_J, KeyType::REGULAR},
                                               {"k", KEY_K, "K", KEY_K, KeyType::REGULAR},
                                               {"l", KEY_L, "L", KEY_L, KeyType::REGULAR},
                                               {";", KEY_SEMICOLON, ":", KEY_SEMICOLON, KeyType::REGULAR},
                                               {"'", KEY_APOSTROPHE, "\"", KEY_APOSTROPHE, KeyType::REGULAR},
                                               {"enter", KEY_ENTER, "enter", KEY_ENTER, KeyType::ENTER}},
                                              {{"ctrl", KEY_LEFTCTRL, "ctrl", KEY_LEFTCTRL, KeyType::CTRL},
                                               {"opt", 0, "opt", 0, KeyType::OPT},
                                               {"alt", KEY_LEFTALT, "alt", KEY_LEFTALT, KeyType::ALT},
                                               {"z", KEY_Z, "Z", KEY_Z, KeyType::REGULAR},
                                               {"x", KEY_X, "X", KEY_X, KeyType::REGULAR},
                                               {"c", KEY_C, "C", KEY_C, KeyType::REGULAR},
                                               {"v", KEY_V, "V", KEY_V, KeyType::REGULAR},
                                               {"b", KEY_B, "B", KEY_B, KeyType::REGULAR},
                                               {"n", KEY_N, "N", KEY_N, KeyType::REGULAR},
                                               {"m", KEY_M, "M", KEY_M, KeyType::REGULAR},
                                               {",", KEY_COMMA, "<", KEY_COMMA, KeyType::REGULAR},
                                               {".", KEY_DOT, ">", KEY_DOT, KeyType::REGULAR},
                                               {"/", KEY_KPSLASH, "?", KEY_KPSLASH, KeyType::REGULAR},
                                               {"space", KEY_SPACE, "space", KEY_SPACE, KeyType::SPACE}}};

    class Keyboard
    {
    public:
        struct KeysState
        {
            bool tab = false;
            bool fn = false;
            bool shift = false;
            bool ctrl = false;
            bool opt = false;
            bool alt = false;
            bool del = false;
            bool enter = false;
            bool space = false;

            std::vector<char> values;
            std::vector<int> hidKey;

            void reset()
            {
                tab = false;
                fn = false;
                shift = false;
                ctrl = false;
                opt = false;
                alt = false;
                del = false;
                enter = false;
                space = false;

                values.clear();
                hidKey.clear();
            }
        };

    private:
        std::unique_ptr<KeyboardReader> _keyboard_reader;
        std::vector<Point2D_t> _key_values_without_special_keys;
        KeysState _keys_state_buffer;

        bool _is_caps_locked;
        uint8_t _last_key_size;
        uint32_t _last_pressed_time;
        bool _is_dimmed;
        bool _suppress_key_until_release; // when true, keyList returns empty (wake-from-dim key not processed)
        std::function<void(bool)> _dimmed_callback; // invoked when _is_dimmed changes
        uint32_t _dim_time_ms = 0;                 // idle timeout before dimming (0 = disabled)

        HAL::Hal* _hal;
        HAL::BoardType _board_type;

    public:
        Keyboard(HAL::Hal* hal) : _is_caps_locked(false), _last_key_size(0), _suppress_key_until_release(false), _hal(hal), _board_type(HAL::BoardType::AUTO_DETECT)
        {
            init();
        }
        
        void init();

        uint8_t getKeyNum(Point2D_t keyCoor);

        void updateKeyList();
        inline const std::vector<KEYBOARD::Point2D_t>& keyList() const
        {
            static const std::vector<KEYBOARD::Point2D_t> empty_list;
            if (_suppress_key_until_release)
                return empty_list;
            if (_keyboard_reader)
                return _keyboard_reader->keyList();
            return empty_list;
        }

        inline KeyValue_t getKeyValue(const Point2D_t& keyCoor) { return _key_value_map[keyCoor.y][keyCoor.x]; }

        bool isKeyPressing(int keyNum);
        bool waitForRelease(int keyNum, int timeout_ms = 0);
        uint32_t lastPressedTime() const;

        void updateKeysState();
        void resetLastPressedTime();

        inline KeysState& keysState() { return _keys_state_buffer; }

        inline bool capslocked() const { return _is_caps_locked; }
        inline void setCapsLocked(bool isLocked) { _is_caps_locked = isLocked; }

        bool isChanged();
        bool isDimmed() const;
        void setDimmed(bool value);
        void setDimmedCallback(std::function<void(bool)> cb);
        void set_dim_time(uint32_t ms);
        inline uint8_t isPressed() const { return static_cast<uint8_t>(keyList().size()); }
        inline HAL::BoardType boardType() const { return _board_type; }
        void setNotifyTask(TaskHandle_t task)
        {
            if (_keyboard_reader)
                _keyboard_reader->setNotifyTask(task);
        }
    };
} // namespace KEYBOARD
