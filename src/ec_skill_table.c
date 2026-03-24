/*
 * ============================================================================
 * EmbedClaw Skill Table
 *
 * This is the single file to edit when configuring firmware capabilities.
 *
 * HOW TO ADD A SKILL
 * ------------------
 * 1. Implement your tool handler(s) as static functions below.
 * 2. Declare them in a static ec_tool_def_t array.
 * 3. Add an ec_skill_t entry to s_skill_table[] at the bottom.
 *
 * Each skill contributes:
 *   system_context — text appended to the LLM system prompt; describe what
 *                    the hardware does so the model uses the tools correctly.
 *   tools[]        — functions the LLM can invoke for this skill.
 * ============================================================================
 */

#include "ec_skill.h"
#include "ec_json.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ============================================================================
 * Base system prompt
 *
 * Describes the device personality before any skill contexts are appended.
 * Edit this string to change the overall agent behaviour.
 * ============================================================================ */

const char EC_BASE_SYSTEM_PROMPT[] =
    "You are an embedded systems assistant running on FreeRTOS. "
    "Answer concisely. "
    "When the user asks about hardware state or configuration, "
    "use your tools to inspect and control the device directly. "
    "Always report register values in hexadecimal.";

/* ============================================================================
 * Skill: hw_register_control
 *
 * Provides 32-bit memory-mapped register read/write.
 * On POSIX builds a small mock register bank is used for testing.
 * ============================================================================ */

#if defined(EC_PLATFORM_POSIX)

#define MOCK_REG_BASE   0x40000000UL
#define MOCK_REG_COUNT  16

static uint32_t s_mock_regs[MOCK_REG_COUNT];

static int mock_read(uint32_t addr, uint32_t *val)
{
    if (addr < MOCK_REG_BASE) return -1;
    size_t idx = (addr - MOCK_REG_BASE) / 4;
    if (idx >= MOCK_REG_COUNT) return -1;
    *val = s_mock_regs[idx];
    return 0;
}

static int mock_write(uint32_t addr, uint32_t val)
{
    if (addr < MOCK_REG_BASE) return -1;
    size_t idx = (addr - MOCK_REG_BASE) / 4;
    if (idx >= MOCK_REG_COUNT) return -1;
    s_mock_regs[idx] = val;
    return 0;
}

#endif /* EC_PLATFORM_POSIX */

/* hw_register_read --------------------------------------------------------- */
/* args:   { "address": "0x40000000" }                                        */
/* result: { "address": "0x40000000", "value": "0x00000000" }                 */

static int hw_reg_read_fn(const char *args_json,
                          char *out_json, size_t out_size)
{
    char addr_str[24];
    if (ec_json_find_string(args_json, strlen(args_json),
                            "address", addr_str, sizeof(addr_str)) < 0) {
        snprintf(out_json, out_size, "{\"error\":\"missing address\"}");
        return -1;
    }

    unsigned long addr = strtoul(addr_str, NULL, 0);
    uint32_t val = 0;

#if defined(EC_PLATFORM_POSIX)
    if (mock_read((uint32_t)addr, &val) != 0) {
        snprintf(out_json, out_size,
                 "{\"error\":\"0x%08lx out of mock range\"}", addr);
        return -1;
    }
#else
    /* SECURITY: add an address allowlist here before deploying */
    val = *(volatile uint32_t *)(uintptr_t)addr;
#endif

    snprintf(out_json, out_size,
             "{\"address\":\"0x%08lx\",\"value\":\"0x%08x\"}",
             addr, (unsigned)val);
    return 0;
}

/* hw_register_write -------------------------------------------------------- */
/* args:   { "address": "0x40000000", "value": "0x00000001" }                 */
/* result: { "ok": true }                                                      */

static int hw_reg_write_fn(const char *args_json,
                           char *out_json, size_t out_size)
{
    char addr_str[24], val_str[24];

    if (ec_json_find_string(args_json, strlen(args_json),
                            "address", addr_str, sizeof(addr_str)) < 0) {
        snprintf(out_json, out_size, "{\"error\":\"missing address\"}");
        return -1;
    }
    if (ec_json_find_string(args_json, strlen(args_json),
                            "value", val_str, sizeof(val_str)) < 0) {
        snprintf(out_json, out_size, "{\"error\":\"missing value\"}");
        return -1;
    }

    unsigned long addr = strtoul(addr_str, NULL, 0);
    unsigned long val  = strtoul(val_str,  NULL, 0);

#if defined(EC_PLATFORM_POSIX)
    if (mock_write((uint32_t)addr, (uint32_t)val) != 0) {
        snprintf(out_json, out_size,
                 "{\"error\":\"0x%08lx out of mock range\"}", addr);
        return -1;
    }
#else
    /* SECURITY: add an address allowlist here before deploying */
    *(volatile uint32_t *)(uintptr_t)addr = (uint32_t)val;
#endif

    snprintf(out_json, out_size, "{\"ok\":true}");
    return 0;
}

static const ec_tool_def_t s_hw_tools[] = {
    {
        .name        = "hw_register_read",
        .description = "Read a 32-bit hardware register at the given address.",
        .parameters_schema =
            "{"
              "\"type\":\"object\","
              "\"properties\":{"
                "\"address\":{"
                  "\"type\":\"string\","
                  "\"description\":"
                    "\"Register address as a hex string, e.g. '0x40000000'\""
                "}"
              "},"
              "\"required\":[\"address\"]"
            "}",
        .fn = hw_reg_read_fn,
    },
    {
        .name        = "hw_register_write",
        .description = "Write a 32-bit value to a hardware register.",
        .parameters_schema =
            "{"
              "\"type\":\"object\","
              "\"properties\":{"
                "\"address\":{"
                  "\"type\":\"string\","
                  "\"description\":"
                    "\"Register address as a hex string, e.g. '0x40000000'\""
                "},"
                "\"value\":{"
                  "\"type\":\"string\","
                  "\"description\":"
                    "\"Value to write as a hex string, e.g. '0x00000001'\""
                "}"
              "},"
              "\"required\":[\"address\",\"value\"]"
            "}",
        .fn = hw_reg_write_fn,
    },
};

/* ============================================================================
 * Skill Table
 *
 * Add or remove ec_skill_t entries here to control what capabilities are
 * compiled into this firmware build.
 * ============================================================================ */

static const ec_skill_t s_skill_table[] = {

    /* ---------------------------------------------------------------------- */
    {
        .name        = "hw_register_control",
        .description = "Read and write 32-bit memory-mapped hardware registers.",
        .system_context =
            "Hardware register access is available via tools.\n"
            "Registers are 32-bit, word-aligned, addressed as hex strings "
            "(e.g. '0x40000000').\n"
            "Read a register to inspect hardware state; write to configure it.",
        .tools     = s_hw_tools,
        .num_tools = sizeof(s_hw_tools) / sizeof(s_hw_tools[0]),
    },
    /* ---------------------------------------------------------------------- */
    /*  ADD MORE SKILLS HERE                                                   */
    /*                                                                         */
    /*  {                                                                      */
    /*      .name           = "my_skill",                                     */
    /*      .description    = "...",                                          */
    /*      .system_context = "...",                                          */
    /*      .tools          = s_my_tools,                                     */
    /*      .num_tools      = sizeof(s_my_tools)/sizeof(s_my_tools[0]),       */
    /*  },                                                                     */
    /* ---------------------------------------------------------------------- */

};

/* Exported symbols referenced by ec_skill.c */
const ec_skill_t *EC_SKILL_TABLE       = s_skill_table;
const size_t      EC_SKILL_TABLE_COUNT =
    sizeof(s_skill_table) / sizeof(s_skill_table[0]);
