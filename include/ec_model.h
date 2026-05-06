#ifndef EC_MODEL_H
#define EC_MODEL_H

#include "ec_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EC_MODEL_PROVIDER_OPENAI_CHAT = 0,
} ec_model_provider_t;

typedef struct {
    ec_model_provider_t provider;
    const char         *host;
    uint16_t            port;
    const char         *api_key;
    int                 use_tls;
} ec_model_config_t;

/*
 * The model-facing request and response types intentionally mirror the current
 * legacy chat backend so the agent loop can depend on a provider-agnostic
 * boundary without changing its core control flow yet.
 */
typedef ec_api_tool_def_t   ec_model_tool_def_t;
typedef ec_api_tool_call_t  ec_model_tool_call_t;
typedef ec_api_message_t    ec_model_message_t;
typedef ec_api_resp_type_t  ec_model_resp_type_t;
typedef ec_api_response_t   ec_model_response_t;

int ec_model_complete(const ec_model_config_t *config,
                      const char *model,
                      const ec_model_message_t *messages,
                      size_t num_messages,
                      const ec_model_tool_def_t *tools,
                      size_t num_tools,
                      ec_model_response_t *out);

#define EC_MODEL_RESP_MESSAGE    EC_API_RESP_MESSAGE
#define EC_MODEL_RESP_TOOL_CALLS EC_API_RESP_TOOL_CALLS

#define EC_MODEL_ERR_JSON_BUILD EC_API_ERR_JSON_BUILD
#define EC_MODEL_ERR_HTTP       EC_API_ERR_HTTP
#define EC_MODEL_ERR_JSON_PARSE EC_API_ERR_JSON_PARSE
#define EC_MODEL_ERR_API        EC_API_ERR_API
#define EC_MODEL_ERR_OVERFLOW   EC_API_ERR_OVERFLOW

#ifdef __cplusplus
}
#endif

#endif /* EC_MODEL_H */
