/**
 * @file hal_config.h
 * @brief HAL SDK configuration - bridges Kconfig options to HAL defines
 *
 * Component toggles are configured via `idf.py menuconfig` under
 * "HAL Configuration".  This header maps CONFIG_HAL_USE_xxx from
 * sdkconfig.h to the HAL_USE_xxx macros consumed by the HAL layer.
 */
#pragma once
#include "sdkconfig.h"

#ifdef CONFIG_HAL_USE_I2C
#define HAL_USE_I2C         1
#else
#define HAL_USE_I2C         0
#endif

#ifdef CONFIG_HAL_USE_DISPLAY
#define HAL_USE_DISPLAY     1
#else
#define HAL_USE_DISPLAY     0
#endif

#ifdef CONFIG_HAL_USE_KEYBOARD
#define HAL_USE_KEYBOARD    1
#else
#define HAL_USE_KEYBOARD    0
#endif

#ifdef CONFIG_HAL_USE_SPEAKER
#define HAL_USE_SPEAKER     1
#else
#define HAL_USE_SPEAKER     0
#endif

#ifdef CONFIG_HAL_USE_LED
#define HAL_USE_LED         1
#else
#define HAL_USE_LED         0
#endif

#ifdef CONFIG_HAL_USE_BUTTON
#define HAL_USE_BUTTON      1
#else
#define HAL_USE_BUTTON      0
#endif

#ifdef CONFIG_HAL_USE_BAT
#define HAL_USE_BAT         1
#else
#define HAL_USE_BAT         0
#endif

#ifdef CONFIG_HAL_USE_SDCARD
#define HAL_USE_SDCARD      1
#else
#define HAL_USE_SDCARD      0
#endif

#ifdef CONFIG_HAL_USE_USB
#define HAL_USE_USB         1
#else
#define HAL_USE_USB         0
#endif

#ifdef CONFIG_HAL_USE_WIFI
#define HAL_USE_WIFI        1
#else
#define HAL_USE_WIFI        0
#endif

#ifdef CONFIG_HAL_USE_BLE
#define HAL_USE_BLE         1
#else
#define HAL_USE_BLE         0
#endif

#ifdef CONFIG_HAL_USE_RADIO
#define HAL_USE_RADIO       1
#else
#define HAL_USE_RADIO       0
#endif

#ifdef CONFIG_HAL_USE_GPS
#define HAL_USE_GPS         1
#else
#define HAL_USE_GPS         0
#endif
