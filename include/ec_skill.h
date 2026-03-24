#ifndef EC_SKILL_H
#define EC_SKILL_H

#include "ec_tool.h"
#include "ec_config.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A skill is a named, compiled-in capability.  It contributes:
 *   - system_context  — appended to the LLM system prompt so the model
 *                       knows what this skill can do and how to use it.
 *   - tools[]         — tool definitions registered with the tool
 *                       framework and advertised to the LLM.
 *
 * Skills are defined in ec_skill_table.c.  That is the only file you
 * need to edit to add, remove, or reconfigure capabilities.
 */
typedef struct {
    const char          *name;
    const char          *description;    /* one-line summary */
    const char          *system_context; /* injected into system prompt */
    const ec_tool_def_t *tools;          /* array of tool definitions */
    size_t               num_tools;
} ec_skill_t;

/* -------------------------------------------------------------------------
 * Skill table — defined in ec_skill_table.c, referenced here.
 * ------------------------------------------------------------------------- */

extern const ec_skill_t  *EC_SKILL_TABLE;
extern const size_t       EC_SKILL_TABLE_COUNT;

/*
 * Base system prompt — defined in ec_skill_table.c.
 * Describes the device personality before any skill contexts are appended.
 */
extern const char EC_BASE_SYSTEM_PROMPT[];

/* -------------------------------------------------------------------------
 * Skill framework API
 * ------------------------------------------------------------------------- */

/**
 * Initialise the skill framework.
 * Iterates EC_SKILL_TABLE, registers every tool with ec_tool, and builds
 * the combined system prompt.  Call once at startup before the agent loop.
 */
void ec_skill_init(void);

/**
 * Return the combined system prompt built by ec_skill_init():
 *   EC_BASE_SYSTEM_PROMPT + each skill's system_context
 * The returned pointer is to a static buffer valid for the lifetime of
 * the program.
 */
const char *ec_skill_get_system_prompt(void);

#ifdef __cplusplus
}
#endif

#endif /* EC_SKILL_H */
