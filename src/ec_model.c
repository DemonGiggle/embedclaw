#include "ec_model.h"

int ec_model_complete(const ec_model_config_t *config,
                      const char *model,
                      const ec_model_message_t *messages,
                      size_t num_messages,
                      const ec_model_tool_def_t *tools,
                      size_t num_tools,
                      ec_model_response_t *out)
{
    ec_api_config_t api_config = {
        .base_url = config->host,
        .port     = config->port,
        .api_key  = config->api_key,
        .use_tls  = config->use_tls,
    };

    switch (config->provider) {
    case EC_MODEL_PROVIDER_OPENAI_CHAT:
        return ec_api_chat_completion(&api_config, model,
                                      messages, num_messages,
                                      tools, num_tools,
                                      out);
    default:
        return EC_MODEL_ERR_API;
    }
}
