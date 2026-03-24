#include "ec_tool.h"

#include <string.h>
#include <stdio.h>

/* =========================================================================
 * Tool registry and dispatcher.
 *
 * This file is pure infrastructure — it knows nothing about specific tools.
 * All tool definitions and implementations live in ec_skill_table.c and are
 * registered via ec_skill_init() at startup.
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
    snprintf(out_json, out_size,
             "{\"error\":\"unknown tool: %s\"}", call->name);
    return -1;
}

const ec_api_tool_def_t *ec_tool_api_defs(size_t *count)
{
    *count = s_tool_count;
    return s_api_defs;
}
