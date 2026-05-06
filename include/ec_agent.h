#ifndef EC_AGENT_H
#define EC_AGENT_H

#include "ec_model.h"
#include "ec_session.h"
#include "ec_config.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const ec_model_config_t *model_config;
    const char              *model;
    ec_session_t            *session;
} ec_agent_t;

/**
 * Initialise the agent.
 *
 * @param agent      Agent struct to initialise.
 * @param api_config API endpoint configuration.
 * @param model      Model name string.
 * @param session    Conversation session (already initialised).
 */
void ec_agent_init(ec_agent_t *agent,
                   const ec_model_config_t *model_config,
                   const char *model,
                   ec_session_t *session);

/**
 * Process one user turn through the full agentic loop.
 *
 * Appends user_input to the session, calls the LLM, dispatches tool calls
 * (repeating up to EC_CONFIG_MAX_AGENT_ITERS times), and writes the final
 * text response into out_response.
 *
 * @param agent        Agent context.
 * @param user_input   Null-terminated user message.
 * @param out_response Buffer for the final assistant text response.
 * @param out_size     Size of out_response.
 * @return 0 on success, negative error code on failure.
 */
int ec_agent_run_turn(ec_agent_t *agent,
                      const char *user_input,
                      char *out_response, size_t out_size);

/* Error codes */
#define EC_AGENT_ERR_SESSION_FULL  (-20)
#define EC_AGENT_ERR_API           (-21)
#define EC_AGENT_ERR_MAX_ITERS     (-22)
#define EC_AGENT_ERR_TOOL          (-23)

#ifdef __cplusplus
}
#endif

#endif /* EC_AGENT_H */
