/**
 * @file Battery.h
 * @brief Battery management class for ADC-based voltage and level reading
 * @author d4rkmen
 * @license Apache License 2.0
 */
#pragma once

#include <stdint.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"

namespace HAL
{
    class Battery
    {
    private:
        adc_oneshot_unit_handle_t _adc1_handle;
        adc_cali_handle_t _adc_cali_handle;
        bool _do_calibration;
        int _adc_raw;
        int _voltage_mv;
        float _voltage = 0.0f;

        bool _adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t* out_handle);
        void _adc_calibration_deinit(adc_cali_handle_t handle);

    public:
        Battery();
        ~Battery();

        constexpr static const adc_unit_t ADC_UNIT = ADC_UNIT_1;
        constexpr static const adc_channel_t ADC_CHANNEL = ADC_CHANNEL_9;
        constexpr static const adc_atten_t ADC_ATTEN = ADC_ATTEN_DB_12;
        constexpr static const float MEASUREMENT_OFFSET = 1.5f;
        /**
         * @brief Initialize the battery ADC
         */
        void init();

        /**
         * @brief Deinitialize the battery ADC
         */
        void deinit();

        /**
         * @brief Get battery voltage in volts
         * @return Battery voltage in volts
         */
        float get_voltage();

        /**
         * @brief Get battery level percentage
         * @return Battery level (0, 25, 50, 75, or 100)
         */
        uint8_t get_level();

        /**
         * @brief Get battery level based on voltage
         * @param voltage Voltage in volts
         * @return Battery level (0, 25, 50, 75, or 100)
         */
        uint8_t get_level(float voltage);
    };
} // namespace HAL
