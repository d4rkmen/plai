#include "dialog.h"
#include "esp_log.h"
#include "apps/utils/anim/hl_text.h"
#include "apps/utils/screenshot/screenshot_tools.h"
#include "lgfx/v1/misc/enum.hpp"
#include "apps/utils/text/text_utils.h"
#include "key_repeat.h"
#include "draw_helper.h"

static const char* TAG = "DIALOG";
static bool is_repeat = false;
static uint32_t next_fire_ts = 0xFFFFFFFF;
using UTILS::TEXT::utf8_byte_offset;
using UTILS::TEXT::utf8_char_len;
using UTILS::TEXT::utf8_strlen;
using UTILS::TEXT::utf8_substr;

// Keyboard layout definitions
static constexpr size_t KBD_KEY_COUNT = 47;

struct KbdLayout
{
    const char* const chars[KBD_KEY_COUNT];       // lowercase / normal
    const char* const chars_shift[KBD_KEY_COUNT]; // uppercase / shifted
    const char* label_lower;                      // mode indicator (lowercase)
    const char* label_upper;                      // mode indicator (uppercase)
    uint32_t color_lower;                         // indicator color (lowercase)
    uint32_t color_upper;                         // indicator color (uppercase)
};

static const KbdLayout kbd_layouts[] = {
    // English
    {{"1",  "2", "3", "4", "5", "6", "7", "8", "9", "0", "_", "=", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "[", "]",
      "\\", "a", "s", "d", "f", "g", "h", "j", "k", "l", ";", "'", "z", "x", "c", "v", "b", "n", "m", ",", ".", "/", " "},
     {"!", "@", "#", "$", "%", "^", "&", "*", "(", ")", "-", "+",  "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "{", "}",
      "|", "A", "S", "D", "F", "G", "H", "J", "K", "L", ":", "\"", "Z", "X", "C", "V", "B", "N", "M", "<", ">", "?", " "},
     "abc",
     "ABC",
     THEME_COLOR_KEYBOARD_LOWER,
     THEME_COLOR_KEYBOARD_UPPER},
    // Ukrainian (ЙЦУКЕН)
    {{"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=", "й", "ц", "у", "к", "е", "н", "г", "ш", "щ", "з", "х", "ї",
      "ґ", "ф", "і", "в", "а", "п", "р", "о", "л", "д", "ж", "є", "я", "ч", "с", "м", "и", "т", "ь", "б", "ю", ".", " "},
     {"!", "@", "#", "$", "%", "^", "&", "*", "(", ")", "_", "+", "Й", "Ц", "У", "К", "Е", "Н", "Г", "Ш", "Щ", "З", "Х", "Ї",
      "Ґ", "Ф", "І", "В", "А", "П", "Р", "О", "Л", "Д", "Ж", "Є", "Я", "Ч", "С", "М", "И", "Т", "Ь", "Б", "Ю", ",", " "},
     "абв",
     "АБВ",
     THEME_COLOR_KEYBOARD_LOWER,
     THEME_COLOR_KEYBOARD_UPPER},
};

static constexpr int KBD_LAYOUT_COUNT = sizeof(kbd_layouts) / sizeof(kbd_layouts[0]);

// Keyboard layout state (persists across dialog invocations)
static int kbd_layout = 0;

using namespace UTILS::HL_TEXT;
namespace UTILS
{
    namespace UI
    {
        // Constants for dialog layout
        constexpr int DIALOG_WIDTH = 180;
        constexpr int DIALOG_HEIGHT = 80;
        constexpr int DIALOG_CORNER_RADIUS = 8;
        constexpr int BUTTON_HEIGHT = 20;
        constexpr int BUTTON_WIDTH = 60;
        constexpr int BUTTON_CORNER_RADIUS = 4;
        constexpr int BUTTON_SPACING = 20;
        constexpr int VERTICAL_SPACING = 16;

        int show_dialog(HAL::Hal* hal,
                        const std::string& title,
                        uint32_t title_color,
                        const std::string& message,
                        uint32_t message_color,
                        const std::vector<DialogButton_t>& buttons,
                        uint32_t close_timeout_ms,
                        uint8_t scroll_speed,
                        uint32_t scroll_pause_ms)
        {
            ESP_LOGW(TAG, "show_dialog: title=%s, message=%s", title.c_str(), message.c_str());
            hal->displayWakeup();
            int brightness = hal->settings()->getNumber("system", "brightness");
            hal->display()->setBrightness(brightness == 0 ? 100 : brightness);
            // set font
            hal->canvas()->setFont(FONT_16);
            // Calculate dialog position
            int dialog_x = (hal->canvas()->width() - DIALOG_WIDTH) / 2;
            int dialog_y = (hal->canvas()->height() - DIALOG_HEIGHT) / 2;

            // Draw dialog box
            hal->canvas()->fillRoundRect(dialog_x, dialog_y, DIALOG_WIDTH, DIALOG_HEIGHT, DIALOG_CORNER_RADIUS, THEME_COLOR_BG);
            hal->canvas()->drawRoundRect(dialog_x, dialog_y, DIALOG_WIDTH, DIALOG_HEIGHT, DIALOG_CORNER_RADIUS, TFT_WHITE);

            // Initialize scroll contexts for title and message
            SCROLL_TEXT::ScrollTextContext_t title_scroll_ctx;
            SCROLL_TEXT::ScrollTextContext_t message_scroll_ctx;

            // Calculate button layout
            int total_buttons_width = buttons.size() * BUTTON_WIDTH + (buttons.size() - 1) * BUTTON_SPACING;
            int buttons_start_x = dialog_x + (DIALOG_WIDTH - total_buttons_width) / 2;
            int buttons_y = dialog_y + DIALOG_HEIGHT - BUTTON_HEIGHT - 10;

            int selected_button = 0;
            uint32_t start_time = millis();
            bool title_fits = hal->canvas()->textWidth(title.c_str()) <= DIALOG_WIDTH - 20;
            bool message_fits = hal->canvas()->textWidth(message.c_str()) <= DIALOG_WIDTH - 20;
            if (title_fits)
            {
                // draw title
                hal->canvas()->setTextColor(title_color, THEME_COLOR_BG);
                hal->canvas()->drawCenterString(title.c_str(), dialog_x + DIALOG_WIDTH / 2, dialog_y + 10);
            }
            else
            {
                SCROLL_TEXT::scroll_text_init(&title_scroll_ctx,
                                              hal->canvas(),
                                              DIALOG_WIDTH - 20,
                                              16,
                                              scroll_speed,
                                              scroll_pause_ms);
            }
            if (message_fits)
            {
                // draw message
                hal->canvas()->setTextColor(message_color, THEME_COLOR_BG);
                hal->canvas()->drawCenterString(message.c_str(), dialog_x + DIALOG_WIDTH / 2, dialog_y + 10 + VERTICAL_SPACING);
            }
            else
            {

                SCROLL_TEXT::scroll_text_init(&message_scroll_ctx,
                                              hal->canvas(),
                                              DIALOG_WIDTH - 20,
                                              16,
                                              scroll_speed,
                                              scroll_pause_ms);
            }
            // show canles hint
            if (close_timeout_ms > 0)
            {
                hal->canvas()->setFont(FONT_10);
                hal->canvas()->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
                hal->canvas()->drawCenterString("[DEL] CANCEL", dialog_x + DIALOG_WIDTH / 2, dialog_y + DIALOG_HEIGHT - 10);
                hal->canvas()->setFont(FONT_16);
            }
            bool need_update = true;
            is_repeat = false;
            next_fire_ts = 0xFFFFFFFF;
            while (true)
            {
                // Handle timeout
                uint32_t now = millis();
                if (close_timeout_ms > 0 && (now - start_time >= close_timeout_ms))
                {
                    return selected_button;
                }

                // check it title fits
                if (!title_fits)
                {
                    // Draw scrolling title
                    need_update |= SCROLL_TEXT::scroll_text_render(&title_scroll_ctx,
                                                                   title.c_str(),
                                                                   dialog_x + 10,
                                                                   dialog_y + 10,
                                                                   title_color,
                                                                   THEME_COLOR_BG);
                }
                // check it message fits
                if (!message_fits)
                {
                    // Draw scrolling message
                    need_update |= SCROLL_TEXT::scroll_text_render(
                        &message_scroll_ctx,
                        close_timeout_ms > 0
                            ? std::format("{} {} sec", message, (uint32_t)((close_timeout_ms - (now - start_time)) / 1000))
                                  .c_str()
                            : message.c_str(),
                        dialog_x + 10,
                        dialog_y + 10 + VERTICAL_SPACING,
                        message_color,
                        THEME_COLOR_BG);
                }
                else if (close_timeout_ms > 0)
                {
                    hal->canvas()->setTextColor(message_color, THEME_COLOR_BG);
                    hal->canvas()->drawCenterString(
                        std::format("{} {} sec", message, (uint32_t)((close_timeout_ms - (now - start_time)) / 1000)).c_str(),
                        dialog_x + DIALOG_WIDTH / 2,
                        dialog_y + 10 + VERTICAL_SPACING);
                    need_update = true;
                }
                if (hal->home_button()->is_pressed())
                {
                    if (!title_fits)
                    {
                        SCROLL_TEXT::scroll_text_free(&title_scroll_ctx);
                    }
                    if (!message_fits)
                    {
                        SCROLL_TEXT::scroll_text_free(&message_scroll_ctx);
                    }
                    return -1;
                }
                // Handle input
                hal->keyboard()->updateKeyList();
                hal->keyboard()->updateKeysState();
                // Screenshot support
                UTILS::SCREENSHOT_TOOLS::check_and_handle_screenshot(hal, nullptr);
                if (hal->keyboard()->isPressed())
                {
                    if (hal->keyboard()->isKeyPressing(KEY_NUM_LEFT))
                    {
                        if (key_repeat_check(is_repeat, next_fire_ts, millis()))
                        {
                            hal->playNextSound();
                            selected_button--;
                            if (selected_button == -1)
                            {
                                selected_button = buttons.size() - 1;
                            }
                            need_update = true;
                        }
                    }
                    else if (hal->keyboard()->isKeyPressing(KEY_NUM_RIGHT))
                    {
                        if (key_repeat_check(is_repeat, next_fire_ts, millis()))
                        {
                            hal->playNextSound();
                            selected_button++;
                            if (selected_button == buttons.size())
                            {
                                selected_button = 0;
                            }
                            need_update = true;
                        }
                    }
                    else if (hal->keyboard()->isKeyPressing(KEY_NUM_ENTER))
                    {
                        hal->playNextSound();
                        hal->keyboard()->waitForRelease(KEY_NUM_ENTER);

                        if (!title_fits)
                        {
                            SCROLL_TEXT::scroll_text_free(&title_scroll_ctx);
                        }
                        if (!message_fits)
                        {
                            SCROLL_TEXT::scroll_text_free(&message_scroll_ctx);
                        }
                        return selected_button;
                    }
                    else if (hal->keyboard()->isKeyPressing(KEY_NUM_BACKSPACE))
                    {
                        hal->playNextSound();
                        hal->keyboard()->waitForRelease(KEY_NUM_BACKSPACE);

                        if (!title_fits)
                        {
                            SCROLL_TEXT::scroll_text_free(&title_scroll_ctx);
                        }
                        if (!message_fits)
                        {
                            SCROLL_TEXT::scroll_text_free(&message_scroll_ctx);
                        }
                        return -1;
                    }
                    else if (hal->keyboard()->isKeyPressing(KEY_NUM_ESC))
                    {
                        hal->playNextSound();
                        hal->keyboard()->waitForRelease(KEY_NUM_ESC);

                        if (!title_fits)
                        {
                            SCROLL_TEXT::scroll_text_free(&title_scroll_ctx);
                        }
                        if (!message_fits)
                        {
                            SCROLL_TEXT::scroll_text_free(&message_scroll_ctx);
                        }
                        return -1;
                    }
                }
                else
                {
                    is_repeat = false;
                }

                if (need_update)
                {
                    // Draw buttons
                    for (size_t i = 0; i < buttons.size(); i++)
                    {
                        int btn_x = buttons_start_x + i * (BUTTON_WIDTH + BUTTON_SPACING);
                        bool is_selected = (i == selected_button);

                        // Draw button background
                        hal->canvas()->fillRoundRect(btn_x,
                                                     buttons_y,
                                                     BUTTON_WIDTH,
                                                     BUTTON_HEIGHT,
                                                     BUTTON_CORNER_RADIUS,
                                                     is_selected ? THEME_COLOR_BG_SELECTED : buttons[i].bg_color);

                        // Draw button border
                        hal->canvas()
                            ->drawRoundRect(btn_x, buttons_y, BUTTON_WIDTH, BUTTON_HEIGHT, BUTTON_CORNER_RADIUS, TFT_WHITE);

                        // Draw button text
                        hal->canvas()->setTextColor(is_selected ? TFT_BLACK : buttons[i].text_color,
                                                    is_selected ? THEME_COLOR_BG_SELECTED : buttons[i].bg_color);
                        hal->canvas()->drawCenterString(buttons[i].text.c_str(), btn_x + BUTTON_WIDTH / 2, buttons_y + 2);
                    }

                    hal->canvas_update();
                    need_update = false;
                }
                hal->updateMesh();
                delay(5);
            }
        }

        bool show_confirmation_dialog(HAL::Hal* hal,
                                      const std::string& title,
                                      const std::string& message,
                                      const std::string& ok_text,
                                      const std::string& cancel_text)
        {
            std::vector<DialogButton_t> buttons;
            buttons.push_back(DialogButton_t(ok_text, THEME_COLOR_BG, TFT_WHITE));
            buttons.push_back(DialogButton_t(cancel_text, THEME_COLOR_BG, TFT_WHITE));

            int result = show_dialog(hal,
                                     title,
                                     lgfx::v1::convert_to_rgb888(TFT_CYAN),
                                     message,
                                     lgfx::v1::convert_to_rgb888(TFT_LIGHTGREY),
                                     buttons);
            ESP_LOGI(TAG, "show_confirmation_dialog: result=%d", result);
            return result == 0; // First button (OK) was pressed
        }

        void
        show_error_dialog(HAL::Hal* hal, const std::string& title, const std::string& message, const std::string& button_text)
        {
            std::vector<DialogButton_t> buttons;
            buttons.push_back(DialogButton_t(button_text, THEME_COLOR_BG, TFT_WHITE));

            hal->playErrorSound();
            show_dialog(hal,
                        title,
                        lgfx::v1::convert_to_rgb888(TFT_RED),
                        message,
                        lgfx::v1::convert_to_rgb888(TFT_WHITE),
                        buttons);
        }

        int show_message_dialog(HAL::Hal* hal, const std::string& title, const std::string& message, uint32_t close_timeout_ms)
        {
            std::vector<DialogButton_t> buttons;
            buttons.push_back(DialogButton_t("OK", THEME_COLOR_BG, TFT_WHITE));

            return show_dialog(hal,
                               title,
                               lgfx::v1::convert_to_rgb888(TFT_CYAN),
                               message,
                               lgfx::v1::convert_to_rgb888(TFT_WHITE),
                               buttons,
                               close_timeout_ms);
        }

        void show_progress(HAL::Hal* hal, const std::string& title, int progress, const std::string& message)
        {
            hal->canvas()->setFont(FONT_16);
            // Dialog dimensions - same as regular dialog
            int dialog_x = (hal->canvas()->width() - DIALOG_WIDTH) / 2;
            int dialog_y = (hal->canvas()->height() - DIALOG_HEIGHT) / 2;

            // Draw dialog box with rounded corners
            hal->canvas()->fillRoundRect(dialog_x, dialog_y, DIALOG_WIDTH, DIALOG_HEIGHT, DIALOG_CORNER_RADIUS, THEME_COLOR_BG);
            hal->canvas()->drawRoundRect(dialog_x, dialog_y, DIALOG_WIDTH, DIALOG_HEIGHT, DIALOG_CORNER_RADIUS, TFT_WHITE);

            // Truncate title if too long
            std::string display_title = title;
            if (hal->canvas()->textWidth(display_title.c_str()) > DIALOG_WIDTH - 20)
            {
                display_title = display_title.substr(0, 19) + ">";
            }

            // Draw title at top of dialog
            hal->canvas()->setTextColor(TFT_CYAN, THEME_COLOR_BG);
            hal->canvas()->drawCenterString(display_title.c_str(), dialog_x + DIALOG_WIDTH / 2, dialog_y + 10);

            // Progress bar dimensions
            int bar_w = DIALOG_WIDTH - 40; // Padding on both sides
            int bar_h = 18;
            int bar_x = dialog_x + 20; // Center in dialog
            int bar_y = dialog_y + 35; // Below title

            if (progress >= 0)
            {
                // Draw progress bar outline
                hal->canvas()->drawRoundRect(bar_x, bar_y, bar_w, bar_h, 4, THEME_COLOR_BG_SELECTED);
                // Calculate and draw progress bar fill
                int fill_width = (progress * bar_w) / 100;
                if (fill_width > 0)
                {
                    hal->canvas()->fillRoundRect(bar_x, bar_y, fill_width, bar_h, 4, THEME_COLOR_BG_SELECTED);
                }
                // create sprite for transparent text
                LGFX_Sprite* text = new LGFX_Sprite(hal->canvas());
                if (text)
                {
                    text->createSprite(bar_w, bar_h);
                    text->fillScreen(TFT_TRANSPARENT);
                    // Draw percentage text centered in progress bar
                    text->setTextColor(fill_width > bar_w / 2 ? TFT_BLACK : TFT_WHITE, TFT_TRANSPARENT);
                    // + 1);
                    text->setFont(FONT_16);
                    text->drawCenterString(std::format("{}%", progress).c_str(), text->width() / 2, 1);
                    text->pushSprite(hal->canvas(), bar_x, bar_y, TFT_TRANSPARENT);
                    text->deleteSprite();
                    delete text;
                }
            }
            else
            {
                // Draw the pattern (diagonal stripes) on background
                uint8_t step = 10;
                for (int x = bar_x - bar_h; x < bar_x + bar_w; x += step)
                {
                    for (int w = 0; w < 4; w++)
                        hal->canvas()->drawLine(x + w,
                                                bar_y + 1,
                                                x + w + bar_h - 2,
                                                bar_y + 1 + bar_h - 2,
                                                THEME_COLOR_BG_SELECTED);
                }
                // create bar sprite
                LGFX_Sprite* bar = new LGFX_Sprite(hal->canvas());
                if (bar)
                {
                    bar->createSprite(bar_w + bar_h * 2, bar_h);
                    bar->fillScreen(THEME_COLOR_BG);
                    // frame wider then
                    bar->drawRoundRect(bar_h, 0, bar_w, bar_h, 4, THEME_COLOR_BG_SELECTED);
                    // transparent mask, to see background pattern
                    bar->fillRoundRect(bar_h + 1, 1, bar_w - 2, bar_h - 2, 4, TFT_TRANSPARENT);
                    bar->pushSprite(hal->canvas(), bar_x - bar_h, bar_y, TFT_TRANSPARENT);
                    bar->deleteSprite();
                    delete bar;
                }
            }
            // Draw status message below progress bar
            hal->canvas()->setTextColor(TFT_LIGHTGREY, THEME_COLOR_BG);
            std::string status = message;
            if (hal->canvas()->textWidth(status.c_str()) > DIALOG_WIDTH - 20)
            {
                status = status.substr(0, 19) + ">";
            }
            hal->canvas()->drawCenterString(status.c_str(), dialog_x + DIALOG_WIDTH / 2, bar_y + bar_h + 6);

            hal->canvas_update();
        }

        bool show_edit_bool_dialog(HAL::Hal* hal, const std::string& title, bool& value)
        {
            std::vector<DialogButton_t> buttons = {{"True", THEME_COLOR_BG, TFT_WHITE}, {"False", THEME_COLOR_BG, TFT_WHITE}};

            int result = show_dialog(hal,
                                     title,
                                     lgfx::v1::convert_to_rgb888(TFT_ORANGE),
                                     "Select value",
                                     lgfx::v1::convert_to_rgb888(TFT_WHITE),
                                     buttons);
            if (result >= 0)
            {
                value = (result == 0);
                return true;
            }
            return false;
        }

        bool show_edit_number_dialog(HAL::Hal* hal, const std::string& title, int& value, int min_value, int max_value)
        {
            std::string input = std::to_string(value);
            bool editing = true;
            bool result = false;
            bool is_negative = false;
            int cursor_pos = input.length();
            int scroll_offset = 0;

            const uint8_t key_nums[] =
                {KEY_NUM_1, KEY_NUM_2, KEY_NUM_3, KEY_NUM_4, KEY_NUM_5, KEY_NUM_6, KEY_NUM_7, KEY_NUM_8, KEY_NUM_9, KEY_NUM_0};
            const std::string keyboard_chars = "1234567890";
            // create hint highlight context
            HLTextContext_t hint_ctx;
            hl_text_init(&hint_ctx, hal->canvas(), 20, 1500);
            is_repeat = false;
            next_fire_ts = 0xFFFFFFFF;
            while (editing)
            {
                hal->canvas()->fillScreen(THEME_COLOR_BG);

                // Draw title
                hal->canvas()->setTextColor(TFT_CYAN, THEME_COLOR_BG);
                hal->canvas()->setFont(FONT_16);
                hal->canvas()->drawString(title.c_str(), 5, 5);

                // Draw input box
                int box_x = 5;
                int box_y = 30;
                int box_w = hal->canvas()->width() - 10;
                int box_h = 25;
                hal->canvas()->drawRect(box_x, box_y, box_w, box_h, TFT_WHITE);

                // Calculate visible portion of text
                int max_visible_chars = (box_w - 10) / 8; // Assuming 8 pixels per character
                if (input.length() > max_visible_chars)
                {
                    if (cursor_pos > scroll_offset + max_visible_chars - 1)
                    {
                        scroll_offset = cursor_pos - max_visible_chars + 1;
                    }
                    else if (cursor_pos < scroll_offset)
                    {
                        scroll_offset = cursor_pos;
                    }
                }
                else
                {
                    scroll_offset = 0;
                }

                // auto keys_state = hal->keyboard()->keysState();

                // Draw visible text
                std::string visible_text = input.substr(scroll_offset, max_visible_chars);
                hal->canvas()->setTextColor(TFT_WHITE, THEME_COLOR_BG);
                hal->canvas()->drawString(visible_text.c_str(), box_x + 5, box_y + 5);

                // Draw cursor
                if ((millis() / 500) % 2 == 0)
                {
                    int cursor_x = box_x + 5 + (cursor_pos - scroll_offset) * 8;
                    hal->canvas()->drawFastVLine(cursor_x, box_y + 3, box_h - 6, TFT_WHITE);
                }

                // Draw min/max hint
                hal->canvas()->setFont(FONT_10);
                hal->canvas()->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
                hal->canvas()->drawString(std::format("Range: {} to {}", min_value, max_value).c_str(),
                                          box_x,
                                          box_y + box_h + 5);
                // Handle input
                hal->keyboard()->updateKeyList();
                hal->keyboard()->updateKeysState();
                // Screenshot support
                UTILS::SCREENSHOT_TOOLS::check_and_handle_screenshot(hal, nullptr);
                auto keys_state = hal->keyboard()->keysState();
                // Draw controls hint
                hl_text_render(&hint_ctx,
                               keys_state.fn ? "[DEL]" : "[\u2191][\u2193][\u2190][\u2192] [DEL] [ESC] [ENTER]",
                               box_x,
                               hal->canvas()->height() - 12,
                               TFT_DARKGREY,
                               TFT_WHITE,
                               THEME_COLOR_BG);

                hal->canvas_update();

                if (hal->home_button()->is_pressed())
                {
                    result = false;
                    break;
                }

                // Handle input
                if (hal->keyboard()->isPressed())
                {
                    if (keys_state.fn)
                    {
                        if (hal->keyboard()->isKeyPressing(KEY_NUM_BACKSPACE))
                        {
                            hal->playNextSound();
                            hal->keyboard()->waitForRelease(KEY_NUM_BACKSPACE);
                            result = false;
                            break;
                        }
                    }
                    else if (hal->keyboard()->isKeyPressing(KEY_NUM_BACKSPACE))
                    {
                        if (!key_repeat_check(is_repeat, next_fire_ts, millis()))
                            continue;

                        hal->playNextSound();
                        if (cursor_pos > 0)
                        {
                            cursor_pos--;
                            if (cursor_pos < input.length())
                                input.erase(cursor_pos, 1);
                        }
                    }
                    else if (hal->keyboard()->isKeyPressing(KEY_NUM_LEFT))
                    {
                        if (!key_repeat_check(is_repeat, next_fire_ts, millis()))
                            continue;

                        hal->playNextSound();
                        if (cursor_pos > 0)
                            cursor_pos--;
                    }
                    else if (hal->keyboard()->isKeyPressing(KEY_NUM_RIGHT))
                    {
                        if (!key_repeat_check(is_repeat, next_fire_ts, millis()))
                            continue;

                        hal->playNextSound();
                        if (cursor_pos < input.length())
                            cursor_pos++;
                    }
                    else if (hal->keyboard()->isKeyPressing(KEY_NUM_UP))
                    {
                        hal->playNextSound();
                        hal->keyboard()->waitForRelease(KEY_NUM_UP);

                        cursor_pos = input.length();
                    }
                    else if (hal->keyboard()->isKeyPressing(KEY_NUM_DOWN))
                    {
                        hal->playNextSound();
                        hal->keyboard()->waitForRelease(KEY_NUM_DOWN);
                        cursor_pos = 0;
                    }
                    else if (hal->keyboard()->isKeyPressing(KEY_NUM_ENTER))
                    {
                        hal->playNextSound();
                        hal->keyboard()->waitForRelease(KEY_NUM_ENTER);
                        hal->playLastSound();

                        if (!input.empty())
                        {
                            int new_value;
                            // Convert string to integer without exceptions
                            char* end;
                            errno = 0;
                            long temp_value = strtol(input.c_str(), &end, 10);

                            // Check for conversion errors
                            if (end == input.c_str() || *end != '\0' || errno == ERANGE)
                            {
                                show_error_dialog(hal, "Invalid input", "Please enter a valid number");
                                input = std::to_string(value); // Reset to original value
                                cursor_pos = input.length();
                                continue;
                            }

                            new_value = static_cast<int>(temp_value);
                            if (new_value >= min_value && new_value <= max_value)
                            {
                                value = new_value;
                                result = true;
                                break;
                            }
                            else
                            {
                                show_error_dialog(hal,
                                                  "Invalid value",
                                                  std::format("Enter value between {} and {}", min_value, max_value).c_str());
                                input = std::to_string(value); // Reset to original value
                                cursor_pos = input.length();
                            }
                        }
                    }
                    else if (hal->keyboard()->isKeyPressing(KEY_NUM_ESC))
                    {
                        hal->playNextSound();
                        hal->keyboard()->waitForRelease(KEY_NUM_ESC);
                        result = false;
                        break;
                    }
                    else
                    {
                        // Handle numeric input
                        const std::string& chars = keyboard_chars;
                        for (size_t i = 0; i < sizeof(key_nums); i++)
                        {
                            if (hal->keyboard()->isKeyPressing(key_nums[i]))
                            {
                                if (!key_repeat_check(is_repeat, next_fire_ts, millis()))
                                    break;

                                hal->playNextSound();
                                input.insert(cursor_pos, 1, chars[i]);
                                cursor_pos++;
                                break;
                            }
                        }

                        // Handle minus sign
                        if (!is_negative && cursor_pos == 0 && hal->keyboard()->isKeyPressing(KEY_NUM_UNDERSCORE) &&
                            min_value < 0)
                        {
                            if (!key_repeat_check(is_repeat, next_fire_ts, millis()))
                                break;

                            hal->playNextSound();
                            input.insert(0, 1, '-');
                            cursor_pos++;
                            is_negative = true;
                        }
                    }
                }
                else
                {
                    is_repeat = false;
                }

                hal->updateMesh();
                delay(5);
            }
            hl_text_free(&hint_ctx);
            return result;
        }

        bool
        show_edit_string_dialog(HAL::Hal* hal, const std::string& title, std::string& value, bool is_password, int max_length)
        {
            std::string input = value;
            bool editing = true;
            bool result = false;
            int cursor_pos = utf8_strlen(input);
            int scroll_offset = 0;

            const uint8_t key_nums[] = {
                KEY_NUM_1,         KEY_NUM_2,    KEY_NUM_3,    KEY_NUM_4,     KEY_NUM_5,          KEY_NUM_6,
                KEY_NUM_7,         KEY_NUM_8,    KEY_NUM_9,    KEY_NUM_0,     KEY_NUM_UNDERSCORE, KEY_NUM_EQUAL,
                KEY_NUM_Q,         KEY_NUM_W,    KEY_NUM_E,    KEY_NUM_R,     KEY_NUM_T,          KEY_NUM_Y,
                KEY_NUM_U,         KEY_NUM_I,    KEY_NUM_O,    KEY_NUM_P,     KEY_NUM_LEFTBRACE,  KEY_NUM_RIGHTBRACE,
                KEY_NUM_BACKSLASH, KEY_NUM_A,    KEY_NUM_S,    KEY_NUM_D,     KEY_NUM_F,          KEY_NUM_G,
                KEY_NUM_H,         KEY_NUM_J,    KEY_NUM_K,    KEY_NUM_L,     KEY_NUM_UP,         KEY_NUM_APOSTROPHE,
                KEY_NUM_Z,         KEY_NUM_X,    KEY_NUM_C,    KEY_NUM_V,     KEY_NUM_B,          KEY_NUM_N,
                KEY_NUM_M,         KEY_NUM_LEFT, KEY_NUM_DOWN, KEY_NUM_RIGHT, KEY_NUM_SPACE};
            // Keyboard layouts are defined at file scope (kbd_en, kbd_en_shift, kbd_ua, kbd_ua_shift)
            // create hint highlight context
            HLTextContext_t hint_ctx;
            hl_text_init(&hint_ctx, hal->canvas(), 20, 1500);
            is_repeat = false;
            next_fire_ts = 0xFFFFFFFF;
            while (editing)
            {
                hal->canvas()->fillScreen(THEME_COLOR_BG);

                // Draw title
                hal->canvas()->setTextColor(TFT_CYAN, THEME_COLOR_BG);
                hal->canvas()->setFont(FONT_16);
                hal->canvas()->drawString(title.c_str(), 5, 5);

                // Draw input box
                int box_x = 5;
                int box_y = 30;
                int box_w = hal->canvas()->width() - 10;
                int box_h = 25;
                hal->canvas()->drawRect(box_x, box_y, box_w, box_h, TFT_WHITE);

                // Calculate visible portion of text
                int max_visible_chars = (box_w - 10) / 8;
                size_t input_chars = utf8_strlen(input);
                if ((int)input_chars > max_visible_chars)
                {
                    if (cursor_pos > scroll_offset + max_visible_chars - 1)
                    {
                        scroll_offset = cursor_pos - max_visible_chars + 1;
                    }
                    else if (cursor_pos < scroll_offset)
                    {
                        scroll_offset = cursor_pos;
                    }
                }
                else
                {
                    scroll_offset = 0;
                }

                // Draw visible text
                std::string display_text =
                    is_password ? std::string(input_chars, '*') : utf8_substr(input, scroll_offset, max_visible_chars);
                hal->canvas()->setTextColor(TFT_WHITE, THEME_COLOR_BG);
                hal->canvas()->drawString(display_text.c_str(), box_x + 5, box_y + 5);

                // Draw cursor
                if ((millis() / 500) % 2 == 0)
                {
                    int cursor_x = box_x + 5 + (cursor_pos - scroll_offset) * 8;
                    hal->canvas()->drawFastVLine(cursor_x, box_y + 3, box_h - 6, TFT_WHITE);
                }
                // Handle input
                hal->keyboard()->updateKeyList();
                hal->keyboard()->updateKeysState();
                // Screenshot support
                UTILS::SCREENSHOT_TOOLS::check_and_handle_screenshot(hal, nullptr);
                auto keys_state = hal->keyboard()->keysState();

                // Draw keyboard mode indicator
                hal->canvas()->setFont(FONT_10);
                const auto& layout = kbd_layouts[kbd_layout];
                const char* mode_label = keys_state.fn ? "Fn" : keys_state.shift ? layout.label_upper : layout.label_lower;
                uint32_t mode_color = keys_state.fn      ? THEME_COLOR_KEYBOARD_FN
                                      : keys_state.shift ? layout.color_upper
                                                         : layout.color_lower;
                hal->canvas()->setTextColor(mode_color, THEME_COLOR_BG);
                hal->canvas()->drawString(mode_label, box_x, box_y + box_h + 5);
                // draw number of symbols
                hal->canvas()->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
                hal->canvas()->drawRightString(std::format("{} / {}", input.size(), max_length).c_str(),
                                               box_x + box_w - 2,
                                               box_y + box_h + 5);
                // draw keyboard fo non-latin layout
                if (kbd_layout != 0 && !keys_state.fn)
                {
                    const char* const* lat = kbd_layouts[0].chars_shift; // using uppercase more visually readable
                    const char* const* cyr = kbd_layouts[kbd_layout].chars_shift;
                    // const char* const* en_ref = keys_state.shift ? kbd_layouts[0].chars_shift : kbd_layouts[0].chars;
                    // keys_state.shift ? kbd_layouts[kbd_layout].chars_shift : kbd_layouts[kbd_layout].chars;
                    static const int max_keys = 13;
                    static const int rk[] = {12, 13, 12, 11};
                    static const int ro[] = {0, 12, 24, 36};
                    int hy = box_y + box_h + 16;
                    int rh = 10;
                    int x_off = 38;
                    int sw = hal->canvas()->width() - x_off * 2;

                    for (int r = 1; r < 4; r++)
                    {
                        int nk = rk[r];
                        int cw = sw / max_keys;
                        int y = hy + (r - 1) * rh;

                        for (int k = 0; k < nk; k++)
                        {
                            int idx = ro[r] + k;
                            int x = x_off + k * cw;

                            // if (strcmp(en_ref[idx], cyr[idx]) == 0)
                            //     continue;

                            hal->canvas()->setFont(FONT_10);
                            hal->canvas()->setTextColor(TFT_WHITE, THEME_COLOR_BG);
                            hal->canvas()->drawString(lat[idx], x, y);

                            hal->canvas()->setFont(FONT_12);
                            hal->canvas()->setTextColor(TFT_ORANGE, THEME_COLOR_BG);
                            hal->canvas()->drawString(cyr[idx], x + 4, y + 1);
                        }
                    }
                    // draw rect
                    hal->canvas()->drawRoundRect(x_off - 8, hy - 2, sw + 4, rh * 3 + 7, 4, TFT_DARKGREY);
                }
                // else
                {
                    hl_text_render(&hint_ctx,
                                   keys_state.fn ? "[\u2191][\u2193][\u2190][\u2192] [DEL]"
                                                 : "[Aa] [Fn] [OPT] [DEL] [ESC] [ENTER]",
                                   box_x,
                                   hal->canvas()->height() - 9,
                                   TFT_DARKGREY,
                                   TFT_WHITE,
                                   THEME_COLOR_BG);
                }

                hal->canvas_update();

                if (hal->home_button()->is_pressed())
                {
                    result = false;
                    break;
                }
                if (hal->keyboard()->isPressed())
                {
                    if (keys_state.fn)
                    {
                        if (hal->keyboard()->isPressed() == 1)
                            is_repeat = false;
                        if (hal->keyboard()->isKeyPressing(KEY_NUM_LEFT))
                        {
                            if (!key_repeat_check(is_repeat, next_fire_ts, millis()))
                                continue;

                            hal->playNextSound();
                            if (cursor_pos > 0)
                                cursor_pos--;
                        }
                        else if (hal->keyboard()->isKeyPressing(KEY_NUM_RIGHT))
                        {
                            if (!key_repeat_check(is_repeat, next_fire_ts, millis()))
                                continue;

                            hal->playNextSound();
                            if (cursor_pos < (int)utf8_strlen(input))
                                cursor_pos++;
                        }
                        else if (hal->keyboard()->isKeyPressing(KEY_NUM_UP))
                        {
                            hal->playNextSound();
                            hal->keyboard()->waitForRelease(KEY_NUM_UP);
                            cursor_pos = utf8_strlen(input);
                        }
                        else if (hal->keyboard()->isKeyPressing(KEY_NUM_DOWN))
                        {
                            hal->playNextSound();
                            hal->keyboard()->waitForRelease(KEY_NUM_DOWN);
                            cursor_pos = 0;
                        }
                        else if (hal->keyboard()->isKeyPressing(KEY_NUM_BACKSPACE))
                        {
                            hal->playNextSound();
                            hal->keyboard()->waitForRelease(KEY_NUM_BACKSPACE);
                            result = false;
                            break;
                        }
                    } // end of fn mode
                    else if (hal->keyboard()->isKeyPressing(KEY_NUM_BACKSPACE))
                    {
                        if (!key_repeat_check(is_repeat, next_fire_ts, millis()))
                            continue;

                        hal->playNextSound();
                        if (cursor_pos > 0)
                        {
                            cursor_pos--;
                            size_t byte_off = utf8_byte_offset(input, cursor_pos);
                            int char_bytes = utf8_char_len((unsigned char)input[byte_off]);
                            input.erase(byte_off, char_bytes);
                        }
                    }
                    else if (hal->keyboard()->isKeyPressing(KEY_NUM_ENTER))
                    {
                        hal->playNextSound();
                        hal->keyboard()->waitForRelease(KEY_NUM_ENTER);
                        value = input;
                        result = true;
                        break;
                    }
                    else if (hal->keyboard()->isKeyPressing(KEY_NUM_ESC))
                    {
                        hal->playNextSound();
                        hal->keyboard()->waitForRelease(KEY_NUM_ESC);
                        result = false;
                        break;
                    }
                    else if (hal->keyboard()->isKeyPressing(KEY_NUM_OPT))
                    {
                        hal->playNextSound();
                        hal->keyboard()->waitForRelease(KEY_NUM_OPT);
                        kbd_layout = (kbd_layout + 1) % KBD_LAYOUT_COUNT;
                    }
                    else
                    {
                        // Handle character input - select layout
                        const char* const* chars =
                            keys_state.shift ? kbd_layouts[kbd_layout].chars_shift : kbd_layouts[kbd_layout].chars;
                        for (size_t i = 0; i < sizeof(key_nums); i++)
                        {
                            if (hal->keyboard()->isKeyPressing(key_nums[i]))
                            {
                                if (!key_repeat_check(is_repeat, next_fire_ts, millis()))
                                    break;

                                hal->playNextSound();
                                if (input.size() < (size_t)max_length)
                                {
                                    size_t byte_off = utf8_byte_offset(input, cursor_pos);
                                    input.insert(byte_off, chars[i]);
                                    cursor_pos++;
                                }
                                break;
                            }
                        }
                    }
                }
                else
                {
                    is_repeat = false;
                }
                hal->updateMesh();
                delay(5);
            }
            hl_text_free(&hint_ctx);
            return result;
        }

        int
        show_select_dialog(HAL::Hal* hal, const std::string& title, const std::vector<std::string>& items, int default_index)
        {
            if (items.empty())
            {
                return -1;
            }

            // wake up screen
            hal->displayWakeup();
            int brightness = hal->settings()->getNumber("system", "brightness");
            hal->display()->setBrightness(brightness == 0 ? 100 : brightness);

            int selected_index = default_index >= 0 && default_index < items.size() ? default_index : 0;
            bool selecting = true;
            int scroll_offset = 0;
            int line_height = hal->canvas()->fontHeight(FONT_16) + 2 + 1;
            const int y_start = 20;
            const int max_visible_items = (hal->canvas()->height() - y_start - 12) / line_height;
            int scrollbar_height = line_height * max_visible_items;
            int scrollbar_width = 6;
            // check selected index and change scroll offset
            if (selected_index >= max_visible_items)
            {
                scroll_offset = selected_index - max_visible_items + 1;
            }

            is_repeat = false;
            // create hint highlight context
            HLTextContext_t hint_ctx;
            hl_text_init(&hint_ctx, hal->canvas(), 20, 1500);
            is_repeat = false;
            next_fire_ts = 0xFFFFFFFF;

            // Scroll context for long selected item (same as app_nodes)
            const int text_area_width = hal->canvas()->width() - 10 - scrollbar_width - 5 - 2;
            UTILS::SCROLL_TEXT::ScrollTextContext_t item_scroll_ctx;
            UTILS::SCROLL_TEXT::scroll_text_init_ex(&item_scroll_ctx,
                                                    hal->canvas(),
                                                    text_area_width,
                                                    line_height - 2,
                                                    20,
                                                    2000,
                                                    FONT_16);
            int prev_selected_index = -1; // to force initial render
            bool need_update = true;
            const int text_x = 10;

            while (selecting)
            {
                bool selection_changed = (selected_index != prev_selected_index);
                if (selection_changed)
                {
                    UTILS::SCROLL_TEXT::scroll_text_reset(&item_scroll_ctx);
                    prev_selected_index = selected_index;
                    need_update = true;

                    hal->canvas()->fillScreen(THEME_COLOR_BG);

                    // Draw title
                    hal->canvas()->setTextColor(TFT_CYAN, THEME_COLOR_BG);
                    hal->canvas()->setFont(FONT_16);
                    hal->canvas()->drawString(title.c_str(), 5, 0);

                    // Draw list of items (only when selection changed)
                    hal->canvas()->setFont(FONT_16);
                    int y_offset = y_start;
                    for (int i = scroll_offset; i < items.size() && i < scroll_offset + max_visible_items; i++)
                    {
                        if (i == selected_index)
                        {
                            hal->canvas()->fillRect(5,
                                                    y_offset + 1,
                                                    hal->canvas()->width() - 5 - scrollbar_width - 2 - 1,
                                                    line_height - 2,
                                                    THEME_COLOR_BG_SELECTED);
                            hal->canvas()->setTextColor(THEME_COLOR_SELECTED, THEME_COLOR_BG_SELECTED);
                        }
                        else
                        {
                            hal->canvas()->setTextColor(THEME_COLOR_UNSELECTED, THEME_COLOR_BG);
                        }

                        bool text_fits = hal->canvas()->textWidth(items[i].c_str()) <= text_area_width;
                        if (i == selected_index && !text_fits)
                        {
                            UTILS::SCROLL_TEXT::scroll_text_render(&item_scroll_ctx,
                                                                   items[i].c_str(),
                                                                   text_x,
                                                                   y_offset + 1,
                                                                   THEME_COLOR_SELECTED,
                                                                   THEME_COLOR_BG_SELECTED);
                        }
                        else
                        {
                            std::string display_name = items[i];
                            if (!text_fits)
                            {
                                int char_w = hal->canvas()->textWidth("0");
                                display_name = display_name.substr(0, char_w > 0 ? text_area_width / char_w : 20) + ">";
                            }
                            hal->canvas()->drawString(display_name.c_str(), text_x, y_offset + 1);
                        }
                        y_offset += line_height;
                    }

                    // Draw scrollbar
                    UTILS::UI::draw_scrollbar(hal->canvas(),
                                              hal->canvas()->width() - scrollbar_width - 2,
                                              y_start,
                                              scrollbar_width,
                                              scrollbar_height,
                                              (int)items.size(),
                                              max_visible_items,
                                              scroll_offset);
                }
                else
                {
                    // Selection unchanged: only update selected item's scroll (if long) and hint
                    int sel_vis = selected_index - scroll_offset;
                    if (sel_vis >= 0 && sel_vis < max_visible_items)
                    {
                        int sel_y = y_start + sel_vis * line_height + 1;
                        bool text_fits = hal->canvas()->textWidth(items[selected_index].c_str()) <= text_area_width;
                        if (!text_fits)
                        {
                            hal->canvas()->setFont(FONT_16);
                            need_update |= UTILS::SCROLL_TEXT::scroll_text_render(&item_scroll_ctx,
                                                                                  items[selected_index].c_str(),
                                                                                  text_x,
                                                                                  sel_y,
                                                                                  THEME_COLOR_SELECTED,
                                                                                  THEME_COLOR_BG_SELECTED);
                        }
                    }
                }

                need_update |= hl_text_render(&hint_ctx,
                                              "[\u2191][\u2193][\u2190][\u2192] [DEL] [ESC] [ENTER]",
                                              0,
                                              hal->canvas()->height() - 12,
                                              TFT_DARKGREY,
                                              TFT_WHITE,
                                              THEME_COLOR_BG);

                if (need_update)
                {
                    hal->canvas_update();
                    need_update = false;
                }
                if (hal->home_button()->is_pressed())
                {
                    selected_index = -1;
                    selecting = false;
                }
                // Handle input
                hal->keyboard()->updateKeyList();
                hal->keyboard()->updateKeysState();
                // Screenshot support
                UTILS::SCREENSHOT_TOOLS::check_and_handle_screenshot(hal, nullptr);
                if (hal->keyboard()->isPressed())
                {
                    if (hal->keyboard()->isKeyPressing(KEY_NUM_UP))
                    {
                        if (!key_repeat_check(is_repeat, next_fire_ts, millis()))
                            continue;

                        if (selected_index > 0)
                        {
                            hal->playNextSound();
                            selected_index--;
                            if (selected_index < scroll_offset)
                            {
                                scroll_offset = selected_index;
                            }
                            need_update = true;
                        }
                    }
                    else if (hal->keyboard()->isKeyPressing(KEY_NUM_DOWN))
                    {
                        if (!key_repeat_check(is_repeat, next_fire_ts, millis()))
                            continue;

                        if (selected_index < items.size() - 1)
                        {
                            hal->playNextSound();
                            selected_index++;
                            if (selected_index >= scroll_offset + max_visible_items)
                            {
                                scroll_offset = selected_index - max_visible_items + 1;
                            }
                            need_update = true;
                        }
                    }
                    else if (hal->keyboard()->isKeyPressing(KEY_NUM_LEFT))
                    {
                        if (!key_repeat_check(is_repeat, next_fire_ts, millis()))
                            continue;

                        if (selected_index > 0)
                        {
                            hal->playNextSound();
                            // Jump up by visible_items count (page up)
                            int jump = max_visible_items;
                            selected_index = std::max(0, selected_index - jump);
                            scroll_offset = std::max(0, selected_index - (max_visible_items - 1));
                            need_update = true;
                        }
                    }
                    else if (hal->keyboard()->isKeyPressing(KEY_NUM_RIGHT))
                    {
                        if (!key_repeat_check(is_repeat, next_fire_ts, millis()))
                            continue;

                        if (selected_index < items.size() - 1)
                        {
                            hal->playNextSound();
                            int jump = max_visible_items;
                            selected_index = std::min((int)items.size() - 1, selected_index + jump);
                            scroll_offset = std::min(std::max(0, (int)items.size() - max_visible_items), selected_index);
                            need_update = true;
                        }
                    }
                    else if (hal->keyboard()->isKeyPressing(KEY_NUM_ENTER))
                    {
                        hal->playNextSound();
                        hal->keyboard()->waitForRelease(KEY_NUM_ENTER);

                        if (selected_index >= 0)
                        {
                            selecting = false;
                        }
                    }
                    else if (hal->keyboard()->isKeyPressing(KEY_NUM_BACKSPACE))
                    {
                        hal->playNextSound();
                        hal->keyboard()->waitForRelease(KEY_NUM_BACKSPACE);

                        selected_index = -1;
                        selecting = false;
                    }
                    else if (hal->keyboard()->isKeyPressing(KEY_NUM_ESC))
                    {
                        hal->playNextSound();
                        hal->keyboard()->waitForRelease(KEY_NUM_ESC);

                        selected_index = -1;
                        selecting = false;
                    }
                }
                else
                {
                    is_repeat = false;
                }

                hal->updateMesh();
                delay(5);
            }

            UTILS::SCROLL_TEXT::scroll_text_free(&item_scroll_ctx);
            hl_text_free(&hint_ctx);
            return selected_index;
        }

    } // namespace UI
} // namespace UTILS