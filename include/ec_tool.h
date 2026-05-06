#ifndef EC_TOOL_H
#define EC_TOOL_H

#include "ec_model.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Tool handler function type.
 *
 * @param args_json   Null-terminated JSON object string of arguments from LLM.
 * @param out_json    Caller-provided buffer for the JSON result string.
 * @param out_size    Size of out_json.
 * @return 0 on success, negative on error.
 */
typedef int (*ec_tool_fn_t)(const char *args_json,
                            char *out_json, size_t out_size);

typedef struct {
    const char   *name;               /* must match what the LLM calls */
    const char   *description;
    const char   *parameters_schema;  /* JSON Schema object string */
    ec_tool_fn_t  fn;
} ec_tool_def_t;

/**
 * Register a tool. Returns 0 on success, -1 if the registry is full.
 * Call during startup before the agent loop begins.
 */
int ec_tool_register(const ec_tool_def_t *def);

/**
 * Dispatch one tool call received from the LLM.
 * Looks up the tool by name and calls its handler.
 *
 * @param call     Tool call struct from the LLM response.
 * @param out_json Buffer for the JSON result.
 * @param out_size Size of out_json.
 * @return 0 on success, -1 if tool not found, or handler error code.
 */
int ec_tool_dispatch(const ec_model_tool_call_t *call,
                     char *out_json, size_t out_size);

/**
 * Return the registered tool table as ec_model_tool_def_t array for passing
 * to ec_model_complete. count is set to the number of entries.
 */
const ec_model_tool_def_t *ec_tool_model_defs(size_t *count);

/* Tool definitions and implementations live in ec_skill_table.c.
 * Register them by calling ec_skill_init() at startup. */

#ifdef __cplusplus
}
#endif

#endif /* EC_TOOL_H */
