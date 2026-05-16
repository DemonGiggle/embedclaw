#include "ec_model.h"
#include "ec_json.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const ec_model_message_t *find_last_user_message(
    const ec_model_message_t *messages, size_t num_messages, size_t *out_index)
{
    for (size_t i = num_messages; i > 0; i--) {
        const ec_model_message_t *msg = &messages[i - 1];
        if (msg->role && strcmp(msg->role, "user") == 0) {
            if (out_index) *out_index = i - 1;
            return msg;
        }
    }

    return NULL;
}

static const ec_model_message_t *find_last_tool_after(
    const ec_model_message_t *messages, size_t start_index, size_t num_messages)
{
    for (size_t i = num_messages; i > start_index; i--) {
        const ec_model_message_t *msg = &messages[i - 1];
        if (msg->role && strcmp(msg->role, "tool") == 0) {
            return msg;
        }
    }

    return NULL;
}

static void lowercase_copy(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0) return;

    size_t i = 0;
    for (; src[i] != '\0' && i + 1 < dst_size; i++) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

static int find_hex_literal(const char *text, int occurrence,
                            char *out, size_t out_size)
{
    int seen = 0;

    if (out_size == 0) return 0;

    while (*text) {
        if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
            const char *start = text;
            size_t len = 2;

            text += 2;
            while (isxdigit((unsigned char)*text)) {
                len++;
                text++;
            }

            if (len > 2) {
                if (seen == occurrence) {
                    if (len >= out_size) return 0;
                    memcpy(out, start, len);
                    out[len] = '\0';
                    return 1;
                }
                seen++;
                continue;
            }
        }

        text++;
    }

    return 0;
}

static void fill_mock_tool_call(ec_model_response_t *out,
                                size_t num_messages,
                                const char *tool_name,
                                const char *arguments_json)
{
    memset(out, 0, sizeof(*out));
    out->type = EC_MODEL_RESP_TOOL_CALLS;
    out->num_tool_calls = 1;
    snprintf(out->tool_calls[0].id, sizeof(out->tool_calls[0].id),
             "sim_%zu_0", num_messages);
    snprintf(out->tool_calls[0].name, sizeof(out->tool_calls[0].name),
             "%s", tool_name);
    snprintf(out->tool_calls[0].arguments, sizeof(out->tool_calls[0].arguments),
             "%s", arguments_json);
}

static int mock_complete_from_user(const char *user_content,
                                   size_t num_messages,
                                   ec_model_response_t *out)
{
    char lower[EC_CONFIG_SESSION_CONTENT_BUF];
    char address[24];
    char value[24];
    char args[EC_CONFIG_TOOL_ARG_BUF];

    lowercase_copy(lower, sizeof(lower), user_content ? user_content : "");

    if (strstr(lower, "write") &&
        find_hex_literal(user_content, 0, address, sizeof(address)) &&
        find_hex_literal(user_content, 1, value, sizeof(value))) {
        snprintf(args, sizeof(args),
                 "{\"address\":\"%s\",\"value\":\"%s\"}",
                 address, value);
        fill_mock_tool_call(out, num_messages, "hw_register_write", args);
        return 0;
    }

    if ((strstr(lower, "read") || strstr(lower, "show")) &&
        find_hex_literal(user_content, 0, address, sizeof(address))) {
        snprintf(args, sizeof(args),
                 "{\"address\":\"%s\"}",
                 address);
        fill_mock_tool_call(out, num_messages, "hw_register_read", args);
        return 0;
    }

    memset(out, 0, sizeof(*out));
    out->type = EC_MODEL_RESP_MESSAGE;
    snprintf(out->content, sizeof(out->content),
             "Mock simulation mode is active. Ask me to read or write a register using a hex address.");
    return 0;
}

static int mock_complete_from_tool(const ec_model_message_t *tool_msg,
                                   ec_model_response_t *out)
{
    char address[24];
    char value[24];
    char error[160];

    memset(out, 0, sizeof(*out));
    out->type = EC_MODEL_RESP_MESSAGE;

    if (tool_msg && tool_msg->content &&
        ec_json_find_string(tool_msg->content, strlen(tool_msg->content),
                            "error", error, sizeof(error)) >= 0) {
        snprintf(out->content, sizeof(out->content),
                 "Simulated tool error: %s", error);
        return 0;
    }

    if (tool_msg && tool_msg->content &&
        ec_json_find_string(tool_msg->content, strlen(tool_msg->content),
                            "address", address, sizeof(address)) >= 0 &&
        ec_json_find_string(tool_msg->content, strlen(tool_msg->content),
                            "value", value, sizeof(value)) >= 0) {
        snprintf(out->content, sizeof(out->content),
                 "Simulated register %s contains %s.", address, value);
        return 0;
    }

    if (tool_msg && tool_msg->content &&
        strstr(tool_msg->content, "\"ok\":true") != NULL) {
        snprintf(out->content, sizeof(out->content),
                 "Simulated register write completed.");
        return 0;
    }

    snprintf(out->content, sizeof(out->content),
             "Simulated tool result received.");
    return 0;
}

static int ec_model_mock_complete(const ec_model_message_t *messages,
                                  size_t num_messages,
                                  ec_model_response_t *out)
{
    size_t last_user_index = 0;
    const ec_model_message_t *last_user =
        find_last_user_message(messages, num_messages, &last_user_index);
    if (!last_user) return EC_MODEL_ERR_API;

    const ec_model_message_t *last_tool =
        find_last_tool_after(messages, last_user_index + 1, num_messages);
    if (last_tool) {
        return mock_complete_from_tool(last_tool, out);
    }

    return mock_complete_from_user(last_user->content, num_messages, out);
}

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
    case EC_MODEL_PROVIDER_SIM_MOCK:
        (void)config;
        (void)model;
        (void)tools;
        (void)num_tools;
        return ec_model_mock_complete(messages, num_messages, out);
    default:
        return EC_MODEL_ERR_API;
    }
}
