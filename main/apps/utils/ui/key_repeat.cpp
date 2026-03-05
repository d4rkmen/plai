#include "key_repeat.h"

bool key_repeat_check(bool& is_repeat, uint32_t& next_fire, uint32_t now)
{
    if (!is_repeat)
    {
        is_repeat = true;
        next_fire = now + KEY_HOLD_MS;
        return true;
    }
    if (now >= next_fire)
    {
        next_fire = now + KEY_REPEAT_MS;
        return true;
    }
    return false;
}
