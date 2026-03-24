#include "ec_tool.h"
#include "ec_json.h"
#include "ec_config.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* =========================================================================
 * Registry
 * ========================================================================= */

static const ec_tool_def_t *s_tools[EC_CONFIG_MAX_TOOLS];
static size_t                s_tool_count = 0;

/* Parallel array of ec_api_tool_def_t views for passing to ec_api */
static ec_api_tool_def_t s_api_defs[EC_CONFIG_MAX_TOOLS];

int ec_tool_register(const ec_tool_def_t *def)
{
    if (s_tool_count >= EC_CONFIG_MAX_TOOLS) return -1;
    s_tools[s_tool_count] = def;
    s_api_defs[s_tool_count].name              = def->name;
    s_api_defs[s_tool_count].description       = def->description;
    s_api_defs[s_tool_count].parameters_schema = def->parameters_schema;
    s_tool_count++;
    return 0;
}

int ec_tool_dispatch(const ec_api_tool_call_t *call,
                     char *out_json, size_t out_size)
{
    for (size_t i = 0; i < s_tool_count; i++) {
        if (strcmp(s_tools[i]->name, call->name) == 0) {
            return s_tools[i]->fn(call->arguments, out_json, out_size);
        }
    }
    /* Tool not found — return a JSON error */
    snprintf(out_json, out_size,
             "{\"error\":\"unknown tool: %s\"}", call->name);
    return -1;
}

const ec_api_tool_def_t *ec_tool_api_defs(size_t *count)
{
    *count = s_tool_count;
    return s_api_defs;
}

/* =========================================================================
 * Built-in: hw_register_read / hw_register_write
 *
 * On POSIX: a small mock register file at base address 0x40000000.
 * On FreeRTOS: direct volatile memory-mapped register access.
 *
 * SECURITY NOTE: Production builds should restrict valid address ranges
 * to prevent the LLM from accessing arbitrary memory.
 * ========================================================================= */

#if defined(EC_PLATFORM_POSIX)

#define MOCK_REG_BASE  0x40000000UL
#define MOCK_REG_COUNT 16

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

/* -------------------------------------------------------------------------
 * hw_register_read
 * args:  { "address": "0x40000000" }
 * result: { "address": "0x40000000", "value": "0x00000000" }
 * ------------------------------------------------------------------------- */

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
                 "{\"error\":\"address 0x%08lx out of mock range\"}", addr);
        return -1;
    }
#else
    val = *(volatile uint32_t *)(uintptr_t)addr;
#endif

    snprintf(out_json, out_size,
             "{\"address\":\"0x%08lx\",\"value\":\"0x%08x\"}",
             addr, (unsigned)val);
    return 0;
}

/* -------------------------------------------------------------------------
 * hw_register_write
 * args:  { "address": "0x40000000", "value": "0x00000001" }
 * result: { "ok": true } or error
 * ------------------------------------------------------------------------- */

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
                 "{\"error\":\"address 0x%08lx out of mock range\"}", addr);
        return -1;
    }
#else
    *(volatile uint32_t *)(uintptr_t)addr = (uint32_t)val;
#endif

    snprintf(out_json, out_size, "{\"ok\":true}");
    return 0;
}

/* -------------------------------------------------------------------------
 * Registration
 * ------------------------------------------------------------------------- */

static const ec_tool_def_t s_hw_read_def = {
    .name        = "hw_register_read",
    .description = "Read a 32-bit hardware register at the given address.",
    .parameters_schema =
        "{"
          "\"type\":\"object\","
          "\"properties\":{"
            "\"address\":{"
              "\"type\":\"string\","
              "\"description\":\"Register address as a hex string, e.g. '0x40000000'\""
            "}"
          "},"
          "\"required\":[\"address\"]"
        "}",
    .fn = hw_reg_read_fn,
};

static const ec_tool_def_t s_hw_write_def = {
    .name        = "hw_register_write",
    .description = "Write a 32-bit value to a hardware register at the given address.",
    .parameters_schema =
        "{"
          "\"type\":\"object\","
          "\"properties\":{"
            "\"address\":{"
              "\"type\":\"string\","
              "\"description\":\"Register address as a hex string, e.g. '0x40000000'\""
            "},"
            "\"value\":{"
              "\"type\":\"string\","
              "\"description\":\"Value to write as a hex string, e.g. '0x00000001'\""
            "}"
          "},"
          "\"required\":[\"address\",\"value\"]"
        "}",
    .fn = hw_reg_write_fn,
};

static int s_hw_tools_registered = 0;

void ec_tool_register_hw_tools(void)
{
    if (s_hw_tools_registered) return;
    ec_tool_register(&s_hw_read_def);
    ec_tool_register(&s_hw_write_def);
    s_hw_tools_registered = 1;
}
