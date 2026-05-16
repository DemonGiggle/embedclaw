#include "ec_session.h"

#include <string.h>
#include <stddef.h>

void ec_session_init(ec_session_t *s, const char *system_prompt)
{
    memset(s, 0, sizeof(*s));
    if (system_prompt && *system_prompt) {
        strncpy(s->system_prompt, system_prompt,
                EC_CONFIG_SYSTEM_PROMPT_BUF - 1);
    }
}

void ec_session_reset(ec_session_t *s)
{
    s->count = 0;
    memset(s->entries, 0, sizeof(s->entries));
}

int ec_session_append(ec_session_t *s, const char *role, const char *content)
{
    if (s->count >= EC_CONFIG_MAX_HISTORY) return -1;

    ec_session_entry_t *e = &s->entries[s->count];
    memset(e, 0, sizeof(*e));
    strncpy(e->role, role, sizeof(e->role) - 1);
    if (content) {
        strncpy(e->content, content, sizeof(e->content) - 1);
    }
    s->count++;
    return 0;
}

int ec_session_append_tool_calls(ec_session_t *s,
                                  const ec_model_tool_call_t *calls,
                                  int num_calls)
{
    if (s->count >= EC_CONFIG_MAX_HISTORY) return -1;
    if (num_calls <= 0 || num_calls > EC_CONFIG_MAX_TOOL_CALLS) return -1;

    ec_session_entry_t *e = &s->entries[s->count];
    memset(e, 0, sizeof(*e));
    strncpy(e->role, "assistant", sizeof(e->role) - 1);
    e->num_tool_calls = num_calls;
    memcpy(e->tool_calls, calls,
           (size_t)num_calls * sizeof(ec_model_tool_call_t));
    s->count++;
    return 0;
}

int ec_session_append_tool_result(ec_session_t *s,
                                   const char *tool_call_id,
                                   const char *content)
{
    if (s->count >= EC_CONFIG_MAX_HISTORY) return -1;

    ec_session_entry_t *e = &s->entries[s->count];
    memset(e, 0, sizeof(*e));
    strncpy(e->role, "tool", sizeof(e->role) - 1);
    if (tool_call_id) {
        strncpy(e->tool_call_id, tool_call_id,
                sizeof(e->tool_call_id) - 1);
    }
    if (content) {
        strncpy(e->content, content, sizeof(e->content) - 1);
    }
    s->count++;
    return 0;
}

const ec_model_message_t *ec_session_messages(ec_session_t *s, size_t *count)
{
    int idx = 0;

    /* First slot: system prompt (even if empty, some APIs require it) */
    if (s->system_prompt[0] != '\0') {
        ec_model_message_t *m = &s->msg_view[idx++];
        memset(m, 0, sizeof(*m));
        m->role    = "system";
        m->content = s->system_prompt;
    }

    /* Remaining slots: conversation entries */
    for (int i = 0; i < s->count; i++) {
        ec_session_entry_t   *e = &s->entries[i];
        ec_model_message_t   *m = &s->msg_view[idx++];
        memset(m, 0, sizeof(*m));

        m->role = e->role;

        if (e->num_tool_calls > 0) {
            /* assistant + tool_calls */
            m->content        = NULL;
            m->tool_calls     = e->tool_calls;
            m->num_tool_calls = e->num_tool_calls;
        } else if (e->tool_call_id[0] != '\0') {
            /* tool result */
            m->content      = e->content;
            m->tool_call_id = e->tool_call_id;
        } else {
            /* regular text message */
            m->content = e->content;
        }
    }

    *count = (size_t)idx;
    return s->msg_view;
}
