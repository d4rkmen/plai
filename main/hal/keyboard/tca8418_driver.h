/**
 * @file tca8418_driver.h
 * @brief TCA8418 I2C Keyboard Controller Driver for ESP-IDF
 * @version 0.2
 * @date 2025-11-09
 *
 * @copyright Copyright (c) 2025
 */
#pragma once
#include <driver/i2c_master.h>
#include "hal.h"
#include <stdint.h>

namespace KEYBOARD
{
// TCA8418 Register addresses
#define TCA8418_REG_CFG 0x01
#define TCA8418_REG_INT_STAT 0x02
#define TCA8418_REG_KEY_LCK_EC 0x03
#define TCA8418_REG_KEY_EVENT_A 0x04
#define TCA8418_REG_KEY_EVENT_B 0x05
#define TCA8418_REG_KEY_EVENT_C 0x06
#define TCA8418_REG_KEY_EVENT_D 0x07
#define TCA8418_REG_KEY_EVENT_E 0x08
#define TCA8418_REG_KEY_EVENT_F 0x09
#define TCA8418_REG_KEY_EVENT_G 0x0A
#define TCA8418_REG_KEY_EVENT_H 0x0B
#define TCA8418_REG_KEY_EVENT_I 0x0C
#define TCA8418_REG_KEY_EVENT_J 0x0D
#define TCA8418_REG_KP_LCK_TIMER 0x0E
#define TCA8418_REG_UNLOCK_1 0x0F
#define TCA8418_REG_UNLOCK_2 0x10
#define TCA8418_REG_GPIO_INT_STAT_1 0x11
#define TCA8418_REG_GPIO_INT_STAT_2 0x12
#define TCA8418_REG_GPIO_INT_STAT_3 0x13
#define TCA8418_REG_GPIO_DAT_STAT_1 0x14
#define TCA8418_REG_GPIO_DAT_STAT_2 0x15
#define TCA8418_REG_GPIO_DAT_STAT_3 0x16
#define TCA8418_REG_GPIO_DAT_OUT_1 0x17
#define TCA8418_REG_GPIO_DAT_OUT_2 0x18
#define TCA8418_REG_GPIO_DAT_OUT_3 0x19
#define TCA8418_REG_GPIO_INT_EN_1 0x1A
#define TCA8418_REG_GPIO_INT_EN_2 0x1B
#define TCA8418_REG_GPIO_INT_EN_3 0x1C
#define TCA8418_REG_KP_GPIO_1 0x1D
#define TCA8418_REG_KP_GPIO_2 0x1E
#define TCA8418_REG_KP_GPIO_3 0x1F
#define TCA8418_REG_GPI_EM_1 0x20
#define TCA8418_REG_GPI_EM_2 0x21
#define TCA8418_REG_GPI_EM_3 0x22
#define TCA8418_REG_GPIO_DIR_1 0x23
#define TCA8418_REG_GPIO_DIR_2 0x24
#define TCA8418_REG_GPIO_DIR_3 0x25
#define TCA8418_REG_GPIO_INT_LVL_1 0x26
#define TCA8418_REG_GPIO_INT_LVL_2 0x27
#define TCA8418_REG_GPIO_INT_LVL_3 0x28
#define TCA8418_REG_DEBOUNCE_DIS_1 0x29
#define TCA8418_REG_DEBOUNCE_DIS_2 0x2A
#define TCA8418_REG_DEBOUNCE_DIS_3 0x2B
#define TCA8418_REG_GPIO_PULL_1 0x2C
#define TCA8418_REG_GPIO_PULL_2 0x2D
#define TCA8418_REG_GPIO_PULL_3 0x2E

// CFG Register bit definitions
#define TCA8418_REG_CFG_AI (1 << 7)           // Auto-increment for read/write
#define TCA8418_REG_CFG_GPI_E_CGF (1 << 6)    // Event mode config
#define TCA8418_REG_CFG_OVR_FLOW_M (1 << 5)   // Overflow mode enable
#define TCA8418_REG_CFG_INT_CFG (1 << 4)      // Interrupt config
#define TCA8418_REG_CFG_OVR_FLOW_IEN (1 << 3) // Overflow interrupt enable
#define TCA8418_REG_CFG_K_LCK_IEN (1 << 2)    // Keypad lock interrupt enable
#define TCA8418_REG_CFG_GPI_IEN (1 << 1)      // GPI interrupt enable
#define TCA8418_REG_CFG_KE_IEN (1 << 0)       // Key events interrupt enable

// Interrupt status bit definitions
#define TCA8418_REG_INT_STAT_CAD_INT (1 << 4)
#define TCA8418_REG_INT_STAT_OVR_FLOW_INT (1 << 3)
#define TCA8418_REG_INT_STAT_K_LCK_INT (1 << 2)
#define TCA8418_REG_INT_STAT_GPI_INT (1 << 1)
#define TCA8418_REG_INT_STAT_K_INT (1 << 0)

// Default I2C address
#define TCA8418_I2C_ADDR 0x34
#define TCA8418_RETRY_COUNT 5
    /**                                                                                                                   \
     * @brief TCA8418 I2C Keyboard Controller Driver                                                                      \
     */
    class tca8418_driver
    {
    public:
        tca8418_driver(HAL::Hal* hal, uint8_t addr = TCA8418_I2C_ADDR);
        ~tca8418_driver();

        /**
         * @brief Initialize the TCA8418 device
         * @return true if initialization successful
         */
        bool begin();

        /**
         * @brief Configure the keyboard matrix size
         * @param rows Number of rows (1-8)
         * @param cols Number of columns (1-10)
         */
        void set_matrix(uint8_t rows, uint8_t cols);

        /**
         * @brief Enable key event + GPIO interrupts
         */
        void enable_interrupts();

        /**
         * @brief Disable key events + GPIO interrupts
         */
        void disable_interrupts();

        /**
         * @brief Enable matrix overflow interrupt
         */
        void enable_matrix_overflow();

        /**
         * @brief Disable matrix overflow interrupt
         */
        void disable_matrix_overflow();

        /**
         * @brief Enable key debounce
         */
        void enable_debounce();

        /**
         * @brief Disable key debounce
         */
        void disable_debounce();

        /**
         * @brief Flush the event FIFO and GPIO events
         */
        void flush();

        /**
         * @brief Get the number of available keys
         * @return Number of available keys
         */
        uint8_t available();

        /**
         * @brief Get the next key event from FIFO
         * @return Key event byte (0x80 = pressed, 0x00 = released, lower 7 bits = key code)
         */
        uint8_t get_event();

        /**
         * @brief Write to a register
         * @param reg Register address
         * @param value Value to write
         * @return true if successful
         */
        bool write_register(uint8_t reg, uint8_t value);

        /**
         * @brief Read from a register
         * @param reg Register address
         * @param value Pointer to store read value
         * @return true if successful
         */
        bool read_register(uint8_t reg, uint8_t* value);

        inline HAL::Hal* hal() { return _hal; }

    private:
        HAL::Hal* _hal;
        // i2c_master_bus_handle_t _bus_handle;
        i2c_master_dev_handle_t _dev_handle;
        uint8_t _i2c_addr;
        bool _initialized;
    };

} // namespace KEYBOARD
