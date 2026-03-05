/**
 * @file text_utils.h
 * @brief Shared UTF-8 text utilities: character counting, truncation, wrapping, timestamp formatting
 */
#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace UTILS
{
    namespace TEXT
    {
        // UTF-8 byte length from leading byte
        inline int utf8_char_len(unsigned char c)
        {
            if ((c & 0x80) == 0)
                return 1;
            if ((c & 0xE0) == 0xC0)
                return 2;
            if ((c & 0xF0) == 0xE0)
                return 3;
            if ((c & 0xF8) == 0xF0)
                return 4;
            return 1;
        }

        // Count UTF-8 codepoints in a null-terminated string
        inline size_t utf8_char_count(const char* str)
        {
            size_t count = 0;
            while (*str)
            {
                str += utf8_char_len((unsigned char)*str);
                count++;
            }
            return count;
        }

        // Count UTF-8 codepoints in a std::string
        inline size_t utf8_strlen(const std::string& str)
        {
            size_t count = 0;
            size_t i = 0;
            while (i < str.size())
            {
                i += utf8_char_len((unsigned char)str[i]);
                count++;
            }
            return count;
        }

        // Byte offset of the Nth UTF-8 codepoint (0-based)
        inline size_t utf8_byte_offset(const std::string& str, size_t char_pos)
        {
            size_t byte_pos = 0;
            size_t chars = 0;
            while (byte_pos < str.size() && chars < char_pos)
            {
                byte_pos += utf8_char_len((unsigned char)str[byte_pos]);
                chars++;
            }
            return byte_pos;
        }

        // Byte length needed for first max_chars UTF-8 characters of a C-string
        inline size_t utf8_truncate_len(const char* str, size_t max_chars)
        {
            size_t byte_pos = 0;
            size_t count = 0;
            while (str[byte_pos] && count < max_chars)
            {
                byte_pos += utf8_char_len((unsigned char)str[byte_pos]);
                count++;
            }
            return byte_pos;
        }

        // Advance byte position by n_chars codepoints, clamped to string length
        inline size_t utf8_advance(const std::string& s, size_t byte_pos, int n_chars)
        {
            size_t len = s.size();
            for (int i = 0; i < n_chars && byte_pos < len; i++)
                byte_pos += utf8_char_len((unsigned char)s[byte_pos]);
            return byte_pos < len ? byte_pos : len;
        }

        // Count UTF-8 codepoints in byte range [from, to)
        inline int utf8_count(const std::string& s, size_t from, size_t to)
        {
            int count = 0;
            for (size_t p = from; p < to;)
            {
                p += utf8_char_len((unsigned char)s[p]);
                count++;
            }
            return count;
        }

        // Substring by UTF-8 character positions
        inline std::string utf8_substr(const std::string& str, size_t char_start, size_t char_count)
        {
            size_t byte_start = utf8_byte_offset(str, char_start);
            size_t byte_end = byte_start;
            size_t chars = 0;
            while (byte_end < str.size() && chars < char_count)
            {
                byte_end += utf8_char_len((unsigned char)str[byte_end]);
                chars++;
            }
            return str.substr(byte_start, byte_end - byte_start);
        }

        // Word-wrap text into lines of max chars_per_line visible characters (UTF-8 aware)
        std::vector<std::string> wrap_text(const std::string& text, int chars_per_line);

        // Count wrapped lines without allocating strings (UTF-8 aware)
        uint16_t count_wrapped_lines(const std::string& text, int chars_per_line);

        // Format a unix timestamp as "dd.mm.yyyy hh:mm"
        std::string format_timestamp(uint32_t timestamp);

    } // namespace TEXT
} // namespace UTILS
