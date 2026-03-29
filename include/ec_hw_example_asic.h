#ifndef EC_HW_EXAMPLE_ASIC_H
#define EC_HW_EXAMPLE_ASIC_H

/**
 * Example ASIC Register Map
 *
 * This file demonstrates how to describe an ASIC's register map using the
 * ec_hw_datasheet.h data structures. Copy this file, rename it for your
 * chip, and replace the contents with your actual register definitions.
 *
 * The LLM will be able to query these tables at runtime via:
 *   hw_module_list      → lists all modules
 *   hw_register_lookup  → returns registers and bit fields for a module
 *
 * Include this header from ec_skill_table.c to activate it.
 */

#include "ec_hw_datasheet.h"

/* =========================================================================
 * Module: UART0
 * Base address: 0x40001000
 * ========================================================================= */

static const ec_hw_bitfield_t s_uart0_ctrl_fields[] = {
    { "EN",       0, 0, "RW", "UART enable. 1 = module active, 0 = disabled." },
    { "TX_EN",    1, 1, "RW", "Transmit enable." },
    { "RX_EN",    2, 2, "RW", "Receive enable." },
    { "LOOPBACK", 3, 3, "RW", "Internal loopback mode for testing." },
    { "BAUD_DIV", 15, 8, "RW", "Baud rate divisor. Baud = PCLK / (16 * (BAUD_DIV + 1))." },
};

static const ec_hw_bitfield_t s_uart0_status_fields[] = {
    { "TX_FULL",  0, 0, "RO", "TX FIFO full." },
    { "TX_EMPTY", 1, 1, "RO", "TX FIFO empty." },
    { "RX_FULL",  2, 2, "RO", "RX FIFO full." },
    { "RX_EMPTY", 3, 3, "RO", "RX FIFO empty. 0 = data available to read." },
    { "OVERRUN",  4, 4, "W1C", "RX overrun error. Write 1 to clear." },
    { "FRAME_ERR",5, 5, "W1C", "Framing error. Write 1 to clear." },
};

static const ec_hw_bitfield_t s_uart0_data_fields[] = {
    { "DATA", 7, 0, "RW", "TX/RX data byte. Write to transmit, read to receive." },
};

static const ec_hw_register_t s_uart0_regs[] = {
    {
        .name        = "CTRL",
        .offset      = 0x00,
        .reset_value = 0x00000000,
        .description = "UART control register.",
        .fields      = s_uart0_ctrl_fields,
        .num_fields  = sizeof(s_uart0_ctrl_fields) / sizeof(s_uart0_ctrl_fields[0]),
    },
    {
        .name        = "STATUS",
        .offset      = 0x04,
        .reset_value = 0x0000000A, /* TX_EMPTY=1, RX_EMPTY=1 */
        .description = "UART status register (read-only except W1C bits).",
        .fields      = s_uart0_status_fields,
        .num_fields  = sizeof(s_uart0_status_fields) / sizeof(s_uart0_status_fields[0]),
    },
    {
        .name        = "DATA",
        .offset      = 0x08,
        .reset_value = 0x00000000,
        .description = "UART data register for TX/RX.",
        .fields      = s_uart0_data_fields,
        .num_fields  = sizeof(s_uart0_data_fields) / sizeof(s_uart0_data_fields[0]),
    },
};

/* =========================================================================
 * Module: GPIO
 * Base address: 0x40002000
 * ========================================================================= */

static const ec_hw_bitfield_t s_gpio_dir_fields[] = {
    { "DIR", 31, 0, "RW", "Direction bits. 1 = output, 0 = input. One bit per pin." },
};

static const ec_hw_bitfield_t s_gpio_data_fields[] = {
    { "DATA", 31, 0, "RW", "Pin data. Read returns pin state; write sets output pins." },
};

static const ec_hw_bitfield_t s_gpio_set_fields[] = {
    { "SET", 31, 0, "WO", "Write 1 to set corresponding output pin high. Read returns 0." },
};

static const ec_hw_bitfield_t s_gpio_clr_fields[] = {
    { "CLR", 31, 0, "WO", "Write 1 to clear corresponding output pin low. Read returns 0." },
};

static const ec_hw_register_t s_gpio_regs[] = {
    {
        .name        = "DIR",
        .offset      = 0x00,
        .reset_value = 0x00000000,
        .description = "GPIO direction register.",
        .fields      = s_gpio_dir_fields,
        .num_fields  = sizeof(s_gpio_dir_fields) / sizeof(s_gpio_dir_fields[0]),
    },
    {
        .name        = "DATA",
        .offset      = 0x04,
        .reset_value = 0x00000000,
        .description = "GPIO data register. Read pin state or write output value.",
        .fields      = s_gpio_data_fields,
        .num_fields  = sizeof(s_gpio_data_fields) / sizeof(s_gpio_data_fields[0]),
    },
    {
        .name        = "SET",
        .offset      = 0x08,
        .reset_value = 0x00000000,
        .description = "GPIO set register. Write-only; sets output pins high.",
        .fields      = s_gpio_set_fields,
        .num_fields  = sizeof(s_gpio_set_fields) / sizeof(s_gpio_set_fields[0]),
    },
    {
        .name        = "CLR",
        .offset      = 0x0C,
        .reset_value = 0x00000000,
        .description = "GPIO clear register. Write-only; drives output pins low.",
        .fields      = s_gpio_clr_fields,
        .num_fields  = sizeof(s_gpio_clr_fields) / sizeof(s_gpio_clr_fields[0]),
    },
};

/* =========================================================================
 * Module Table
 * ========================================================================= */

static const ec_hw_module_t s_example_modules[] = {
    {
        .name          = "uart0",
        .base_addr     = 0x40001000,
        .description   = "UART serial port 0.",
        .notes         = "To enable UART0: write CTRL.EN=1, CTRL.TX_EN=1, CTRL.RX_EN=1, "
                         "set CTRL.BAUD_DIV for desired baud rate. "
                         "Check STATUS.TX_EMPTY before writing DATA. "
                         "Check STATUS.RX_EMPTY==0 before reading DATA.",
        .registers     = s_uart0_regs,
        .num_registers = sizeof(s_uart0_regs) / sizeof(s_uart0_regs[0]),
    },
    {
        .name          = "gpio",
        .base_addr     = 0x40002000,
        .description   = "General-purpose I/O controller. 32 pins.",
        .notes         = "Set DIR bit to 1 for output, 0 for input. "
                         "Use SET/CLR registers for atomic pin toggling. "
                         "Reading DATA always returns the current pin level.",
        .registers     = s_gpio_regs,
        .num_registers = sizeof(s_gpio_regs) / sizeof(s_gpio_regs[0]),
    },
};

/* Wire up the global symbols */
const ec_hw_module_t *EC_HW_MODULES      = s_example_modules;
const size_t          EC_HW_MODULE_COUNT =
    sizeof(s_example_modules) / sizeof(s_example_modules[0]);

#endif /* EC_HW_EXAMPLE_ASIC_H */
