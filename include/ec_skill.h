#ifndef EC_SKILL_H
#define EC_SKILL_H

#include "ec_tool.h"
#include "ec_config.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EC_CAPABILITY_POLICY_LOCAL = 0,
    EC_CAPABILITY_POLICY_PRIVILEGED,
    EC_CAPABILITY_POLICY_EXTERNAL,
} ec_capability_policy_t;

/*
 * A capability bundle is a named, compiled-in capability group. It contributes:
 *   - system_context  — appended to the LLM system prompt so the model
 *                       knows what this bundle can do and how to use it.
 *   - policy_note     — explicit operating boundary appended alongside the
 *                       bundle context so privileged or external tools are
 *                       used more carefully.
 *   - tools[]         — tool definitions registered with the tool
 *                       framework and advertised to the LLM.
 *
 * Bundles are defined in ec_skill_table.c. That is the only file you
 * need to edit to add, remove, or reconfigure capabilities.
 */
typedef struct {
    const char               *name;
    const char               *description;    /* one-line summary */
    const char               *system_context; /* injected into system prompt */
    const char               *policy_note;    /* injected policy boundary */
    const ec_tool_def_t      *tools;          /* array of tool definitions */
    size_t                    num_tools;
    ec_capability_policy_t    policy;
} ec_capability_bundle_t;

/* Backward-compatible alias while the rest of the codebase transitions. */
typedef ec_capability_bundle_t ec_skill_t;

/* -------------------------------------------------------------------------
 * Skill table — defined in ec_skill_table.c, referenced here.
 * ------------------------------------------------------------------------- */

extern const ec_capability_bundle_t  *EC_CAPABILITY_TABLE;
extern const size_t                   EC_CAPABILITY_TABLE_COUNT;

/* Backward-compatible aliases while callers migrate to the new terminology. */
#define EC_SKILL_TABLE       EC_CAPABILITY_TABLE
#define EC_SKILL_TABLE_COUNT EC_CAPABILITY_TABLE_COUNT

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
 * Iterates EC_CAPABILITY_TABLE, registers every tool with ec_tool, and builds
 * the combined system prompt.  Call once at startup before the agent loop.
 */
void ec_skill_init(void);

/**
 * Return the combined system prompt built by ec_skill_init():
 *   EC_BASE_SYSTEM_PROMPT + each capability bundle's system_context/policy_note
 * The returned pointer is to a static buffer valid for the lifetime of
 * the program.
 */
const char *ec_skill_get_system_prompt(void);

const ec_capability_bundle_t *ec_capability_bundles(size_t *count);
const char *ec_capability_policy_name(ec_capability_policy_t policy);

#ifdef __cplusplus
}
#endif

#endif /* EC_SKILL_H */
