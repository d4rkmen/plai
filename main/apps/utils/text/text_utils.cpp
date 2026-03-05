/**
 * @file text_utils.cpp
 * @brief Shared UTF-8 text utilities implementation
 */

#include "text_utils.h"
#include <algorithm>
#include <format>
#include <time.h>
#include <cstdio>

namespace UTILS
{
    namespace TEXT
    {
        std::vector<std::string> wrap_text(const std::string& text, int chars_per_line)
        {
            std::vector<std::string> lines;
            if (text.empty())
            {
                lines.push_back("");
                return lines;
            }

            size_t pos = 0;
            while (pos < text.size())
            {
                size_t nl = text.find('\n', pos);
                if (nl != std::string::npos)
                {
                    int nl_chars = utf8_count(text, pos, nl);
                    if (nl_chars <= chars_per_line)
                    {
                        lines.push_back(text.substr(pos, nl - pos));
                        pos = nl + 1;
                        continue;
                    }
                }

                size_t end_byte = utf8_advance(text, pos, chars_per_line);

                if (end_byte < text.size() && text[end_byte] != '\n')
                {
                    size_t last_space = text.rfind(' ', end_byte);
                    if (last_space != std::string::npos && last_space > pos)
                    {
                        end_byte = last_space + 1;
                    }
                }

                lines.push_back(text.substr(pos, end_byte - pos));
                pos = end_byte;
            }
            return lines;
        }

        uint16_t count_wrapped_lines(const std::string& text, int chars_per_line)
        {
            if (text.empty())
                return 1;

            uint16_t lines = 0;
            size_t pos = 0;
            while (pos < text.size())
            {
                size_t nl = text.find('\n', pos);
                if (nl != std::string::npos)
                {
                    int nl_chars = utf8_count(text, pos, nl);
                    if (nl_chars <= chars_per_line)
                    {
                        lines++;
                        pos = nl + 1;
                        continue;
                    }
                }

                size_t end_byte = utf8_advance(text, pos, chars_per_line);

                if (end_byte < text.size() && text[end_byte] != '\n')
                {
                    size_t last_space = text.rfind(' ', end_byte);
                    if (last_space != std::string::npos && last_space > pos)
                    {
                        end_byte = last_space + 1;
                    }
                }

                lines++;
                pos = end_byte;
            }
            return lines > 0 ? lines : 1;
        }

        std::string format_timestamp(uint32_t timestamp)
        {
            struct tm ti;
            time_t ts = (time_t)timestamp;
            localtime_r(&ts, &ti);
            // char buf[20];
            // snprintf(buf, sizeof(buf), "%02d.%02d.%04d %02d:%02d",
            //          ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900,
            //          ti.tm_hour, ti.tm_min);
            return std::format("{:02d}.{:02d}.{:04d} {:02d}:{:02d}",
                ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900,
                ti.tm_hour, ti.tm_min);
        }

    } // namespace TEXT
} // namespace UTILS
