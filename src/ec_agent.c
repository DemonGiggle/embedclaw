#include "ec_agent.h"
#include "ec_tool.h"
#include "ec_log.h"

#include <string.h>
#include <stdio.h>

/* Static response buffer — single-threaded, one request in flight at a time */
static ec_api_response_t s_response;

/* Per-tool result buffer reused across dispatches each turn */
static char s_tool_result[EC_CONFIG_TOOL_RESULT_BUF];

void ec_agent_init(ec_agent_t *agent,
                   const ec_api_config_t *api_config,
                   const char *model,
                   ec_session_t *session)
{
    agent->api_config = api_config;
    agent->model      = model;
    agent->session    = session;
}

int ec_agent_run_turn(ec_agent_t *agent,
                      const char *user_input,
                      char *out_response, size_t out_size)
{
    /* 1. Add the user message to history */
    if (ec_session_append(agent->session, "user", user_input) != 0)
        return EC_AGENT_ERR_SESSION_FULL;

    size_t num_tools = 0;
    const ec_api_tool_def_t *tools = ec_tool_api_defs(&num_tools);

    EC_LOG_DEBUG("=== agent turn start: \"%.*s\"",
                 (int)(strlen(user_input) > 120 ? 120 : strlen(user_input)),
                 user_input);

    for (int iter = 0; iter < EC_CONFIG_MAX_AGENT_ITERS; iter++) {
        EC_LOG_DEBUG("--- iteration %d/%d ---", iter + 1,
                     EC_CONFIG_MAX_AGENT_ITERS);

        /* 2. Build message array from session and call LLM */
        size_t num_msgs = 0;
        const ec_api_message_t *msgs =
            ec_session_messages(agent->session, &num_msgs);

        EC_LOG_DEBUG("sending %zu messages to LLM", num_msgs);

        memset(&s_response, 0, sizeof(s_response));
        int rc = ec_api_chat_completion(
            agent->api_config, agent->model,
            msgs, num_msgs,
            tools, num_tools,
            &s_response);

        if (rc != 0) {
            EC_LOG_DEBUG("LLM API error: rc=%d", rc);
            return EC_AGENT_ERR_API;
        }

        if (s_response.type == EC_API_RESP_MESSAGE) {
            /* 3a. Final text response — store in session and return */
            EC_LOG_DEBUG("LLM returned final text (%zu bytes)",
                         strlen(s_response.content));
            ec_session_append(agent->session, "assistant", s_response.content);

            size_t copy = strlen(s_response.content);
            if (copy >= out_size) copy = out_size - 1;
            memcpy(out_response, s_response.content, copy);
            out_response[copy] = '\0';
            EC_LOG_DEBUG("=== agent turn complete (iter %d) ===", iter + 1);
            return 0;
        }

        /* 3b. Tool calls — dispatch each one, append results, loop */
        EC_LOG_DEBUG("LLM requested %d tool call(s)", s_response.num_tool_calls);

        if (ec_session_append_tool_calls(agent->session,
                                          s_response.tool_calls,
                                          s_response.num_tool_calls) != 0)
            return EC_AGENT_ERR_SESSION_FULL;

        for (int j = 0; j < s_response.num_tool_calls; j++) {
            const ec_api_tool_call_t *tc = &s_response.tool_calls[j];

            EC_LOG_DEBUG("dispatching tool [%d]: %s (id=%s)", j, tc->name, tc->id);
            EC_LOG_DEBUG("  args: %s", tc->arguments);

            memset(s_tool_result, 0, sizeof(s_tool_result));
            ec_tool_dispatch(tc, s_tool_result, sizeof(s_tool_result));
            /* dispatch failure produces a JSON error string, keep going */

            EC_LOG_DEBUG("  result: %.*s",
                         (int)(strlen(s_tool_result) > 512 ? 512 : strlen(s_tool_result)),
                         s_tool_result);

            if (ec_session_append_tool_result(agent->session,
                                               tc->id,
                                               s_tool_result) != 0)
                return EC_AGENT_ERR_SESSION_FULL;
        }
        /* Loop: send updated history back to LLM */
    }

    EC_LOG_DEBUG("=== agent turn hit max iterations ===");

    return EC_AGENT_ERR_MAX_ITERS;
}
