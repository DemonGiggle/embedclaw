#include "ec_api.h"
#include "ec_config.h"

#include <stdio.h>
#include <string.h>

static int run_demo(const char *host, int port, const char *api_key,
                    const char *model, const char *user_prompt)
{
    ec_api_config_t config = {
        .base_url = host,
        .port     = (uint16_t)port,
        .api_key  = api_key,
        .use_tls  = EC_CONFIG_USE_TLS,
    };

    ec_api_message_t messages[] = {
        { .role = "system",  .content = "You are a helpful assistant." },
        { .role = "user",    .content = user_prompt },
    };

    char reply[EC_CONFIG_REPLY_BUF];
    printf("Sending request to %s:%d (model: %s)...\n", host, port, model);

    int rc = ec_api_chat_completion(
        &config, model, messages, 2, reply, sizeof(reply));

    if (rc == 0) {
        printf("Assistant: %s\n", reply);
    } else {
        printf("Error: chat completion failed (rc=%d)\n", rc);
        if (rc == EC_API_ERR_API) {
            printf("API response: %s\n", reply);
        }
    }
    return rc;
}

#if defined(EC_PLATFORM_FREERTOS)

void vEmbedClawTask(void *pvParameters)
{
    (void)pvParameters;

    run_demo(EC_CONFIG_API_HOST, EC_CONFIG_API_PORT,
             EC_CONFIG_API_KEY, EC_CONFIG_MODEL,
             "Say hello in one sentence.");

    for (;;) {}
}

#else /* POSIX */

#include <stdlib.h>

int main(int argc, char **argv)
{
    const char *api_key = getenv("EC_API_KEY");
    if (!api_key || *api_key == '\0') api_key = EC_CONFIG_API_KEY;

    const char *host = getenv("EC_API_HOST");
    if (!host) host = EC_CONFIG_API_HOST;

    const char *port_str = getenv("EC_API_PORT");
    int port = port_str ? atoi(port_str) : EC_CONFIG_API_PORT;

    const char *model = getenv("EC_MODEL");
    if (!model) model = EC_CONFIG_MODEL;

    const char *user_prompt = "Say hello in one sentence.";
    if (argc > 1) {
        user_prompt = argv[1];
    }

    return run_demo(host, port, api_key, model, user_prompt);
}

#endif
