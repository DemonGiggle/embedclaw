#ifndef EC_SESSION_H
#define EC_SESSION_H

#include "ec_api.h"
#include "ec_config.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * One message slot in the conversation history.
 *
 * The role field determines how the slot is interpreted:
 *   "system" / "user" / "assistant"  — content holds the text.
 *   "tool"                           — content holds result JSON,
 *                                      tool_call_id is set.
 *   "assistant" + num_tool_calls > 0 — tool_calls[] holds the calls,
 *                                      content is unused.
 */
typedef struct {
    char role[16];
    char content[EC_CONFIG_SESSION_CONTENT_BUF];
    char tool_call_id[EC_CONFIG_TOOL_ID_BUF];
    ec_api_tool_call_t tool_calls[EC_CONFIG_MAX_TOOL_CALLS];
    int  num_tool_calls;
} ec_session_entry_t;

typedef struct {
    char system_prompt[EC_CONFIG_SESSION_CONTENT_BUF];
    ec_session_entry_t entries[EC_CONFIG_MAX_HISTORY];
    int  count;
    /* Scratch buffer rebuilt by ec_session_messages() — do not access directly */
    ec_api_message_t msg_view[EC_CONFIG_MAX_HISTORY + 1]; /* +1 for system */
} ec_session_t;

/**
 * Initialise (or reset) a session. Clears all history and stores the system
 * prompt. Pass NULL or "" for an empty system prompt.
 */
void ec_session_init(ec_session_t *s, const char *system_prompt);

/**
 * Append a plain text message ("user", "assistant", "system").
 * Returns 0 on success, -1 if the history is full.
 */
int ec_session_append(ec_session_t *s, const char *role, const char *content);

/**
 * Append an assistant message that contains tool_calls.
 * Returns 0 on success, -1 if full.
 */
int ec_session_append_tool_calls(ec_session_t *s,
                                  const ec_api_tool_call_t *calls,
                                  int num_calls);

/**
 * Append a "tool" role result message.
 * Returns 0 on success, -1 if full.
 */
int ec_session_append_tool_result(ec_session_t *s,
                                   const char *tool_call_id,
                                   const char *content);

/**
 * Clear all non-system messages from the history.
 */
void ec_session_reset(ec_session_t *s);

/**
 * Return the full message array (system prompt first, then history entries)
 * as an ec_api_message_t array suitable for passing directly to
 * ec_api_chat_completion. count is set to the number of messages.
 *
 * The returned pointer is into the session's internal msg_view buffer.
 * It remains valid until the next call to ec_session_messages().
 */
const ec_api_message_t *ec_session_messages(ec_session_t *s, size_t *count);

#ifdef __cplusplus
}
#endif

#endif /* EC_SESSION_H */
