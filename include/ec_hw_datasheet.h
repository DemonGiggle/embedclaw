#ifndef EC_HW_DATASHEET_H
#define EC_HW_DATASHEET_H

/**
 * Hardware Datasheet Descriptor Tables
 *
 * Compile-time register map for an ASIC/SoC. Lets the LLM look up module
 * names, register addresses, bit-field definitions, and programming notes
 * without stuffing the entire datasheet into the system prompt.
 *
 * HOW TO USE
 * ----------
 * 1. Create a header for your ASIC (see ec_hw_example_asic.h for the pattern).
 * 2. Define const arrays of ec_hw_bitfield_t, ec_hw_register_t, and
 *    ec_hw_module_t.
 * 3. Define EC_HW_MODULES and EC_HW_MODULE_COUNT pointing to your tables.
 * 4. Include the header from ec_skill_table.c (or set it via a CMake option).
 *
 * The hw_datasheet skill provides two tools that query these tables:
 *   hw_module_list  — list all modules (name + description)
 *   hw_register_lookup — look up registers and bit fields in a module
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bit-field descriptor: one field within a register */
typedef struct {
    const char *name;        /* e.g. "EN", "BAUD_DIV" */
    uint8_t     hi;          /* MSB position (inclusive) */
    uint8_t     lo;          /* LSB position (inclusive) */
    const char *access;      /* "RW", "RO", "WO", "W1C", etc. */
    const char *description; /* human-readable purpose */
} ec_hw_bitfield_t;

/* Register descriptor: one register within a module */
typedef struct {
    const char             *name;        /* e.g. "CTRL", "STATUS" */
    uint32_t                offset;      /* byte offset from module base */
    uint32_t                reset_value; /* value after reset */
    const char             *description; /* human-readable purpose */
    const ec_hw_bitfield_t *fields;      /* array of bit-field descriptors */
    size_t                  num_fields;
} ec_hw_register_t;

/* Module descriptor: one hardware module/peripheral */
typedef struct {
    const char              *name;        /* e.g. "uart0", "spi1", "gpio" */
    uint32_t                 base_addr;   /* base address of the module */
    const char              *description; /* human-readable purpose */
    const char              *notes;       /* programming notes (NULL if none) */
    const ec_hw_register_t  *registers;   /* array of register descriptors */
    size_t                   num_registers;
} ec_hw_module_t;

/**
 * These symbols must be defined by the ASIC-specific header.
 * If no ASIC header is included, ec_skill_table.c provides empty defaults.
 */
extern const ec_hw_module_t *EC_HW_MODULES;
extern const size_t          EC_HW_MODULE_COUNT;

#ifdef __cplusplus
}
#endif

#endif /* EC_HW_DATASHEET_H */
