#include "ec_skill.h"

#include <string.h>
#include <stdio.h>

static char s_system_prompt[EC_CONFIG_SYSTEM_PROMPT_BUF];

void ec_skill_init(void)
{
    /* Build combined system prompt */
    memset(s_system_prompt, 0, sizeof(s_system_prompt));
    strncpy(s_system_prompt, EC_BASE_SYSTEM_PROMPT,
            sizeof(s_system_prompt) - 1);

    for (size_t i = 0; i < EC_SKILL_TABLE_COUNT; i++) {
        const ec_skill_t *skill = &EC_SKILL_TABLE[i];

        /* Register each tool this skill provides */
        for (size_t j = 0; j < skill->num_tools; j++) {
            ec_tool_register(&skill->tools[j]);
        }

        /* Append skill context to the system prompt */
        if (skill->system_context && skill->system_context[0]) {
            size_t used = strlen(s_system_prompt);
            size_t left = sizeof(s_system_prompt) - used - 1;
            if (left > 0) {
                strncat(s_system_prompt, "\n", left);
                left--;
            }
            if (left > 0) {
                strncat(s_system_prompt, skill->system_context, left);
            }
        }
    }
}

const char *ec_skill_get_system_prompt(void)
{
    return s_system_prompt;
}
