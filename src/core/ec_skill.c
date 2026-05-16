#include "ec_skill.h"

#include <string.h>
#include <stdio.h>

static char s_system_prompt[EC_CONFIG_SYSTEM_PROMPT_BUF];

const char *ec_capability_policy_name(ec_capability_policy_t policy)
{
    switch (policy) {
    case EC_CAPABILITY_POLICY_LOCAL:
        return "local";
    case EC_CAPABILITY_POLICY_PRIVILEGED:
        return "privileged";
    case EC_CAPABILITY_POLICY_EXTERNAL:
        return "external";
    default:
        return "unknown";
    }
}

const ec_capability_bundle_t *ec_capability_bundles(size_t *count)
{
    if (count) {
        *count = EC_CAPABILITY_TABLE_COUNT;
    }
    return EC_CAPABILITY_TABLE;
}

void ec_skill_init(void)
{
    /* Build combined system prompt */
    memset(s_system_prompt, 0, sizeof(s_system_prompt));
    strncpy(s_system_prompt, EC_BASE_SYSTEM_PROMPT,
            sizeof(s_system_prompt) - 1);

    for (size_t i = 0; i < EC_CAPABILITY_TABLE_COUNT; i++) {
        const ec_capability_bundle_t *bundle = &EC_CAPABILITY_TABLE[i];

        /* Register each tool this bundle provides */
        for (size_t j = 0; j < bundle->num_tools; j++) {
            ec_tool_register(&bundle->tools[j]);
        }

        /* Append capability context to the system prompt */
        if (bundle->system_context && bundle->system_context[0]) {
            size_t used = strlen(s_system_prompt);
            size_t left = sizeof(s_system_prompt) - used - 1;
            if (left > 0) {
                strncat(s_system_prompt, "\n", left);
                left--;
            }
            if (left > 0) {
                strncat(s_system_prompt, bundle->system_context, left);
            }
        }

        if (bundle->policy_note && bundle->policy_note[0]) {
            size_t used = strlen(s_system_prompt);
            size_t left = sizeof(s_system_prompt) - used - 1;
            if (left > 0) {
                strncat(s_system_prompt, "\n", left);
                left--;
            }
            if (left > 0) {
                strncat(s_system_prompt, bundle->policy_note, left);
            }
        }
    }
}

const char *ec_skill_get_system_prompt(void)
{
    return s_system_prompt;
}
