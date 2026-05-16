#include "ec_model.h"
#include "ec_agent.h"
#include "ec_session.h"
#include "ec_skill.h"
#include "ec_io.h"
#include "ec_config.h"
#include "ec_log.h"

#if !defined(EC_PLATFORM_FREERTOS)
#error "ec_freertos_entry.c must only be built for EC_PLATFORM=FREERTOS"
#endif

#include <stdio.h>
#include <string.h>

static ec_session_t s_session;
static ec_agent_t s_agent;

static void run_agent_loop(const ec_model_config_t *config, const char *model)
{
    ec_log_init();
    ec_skill_init();

    ec_session_init(&s_session, ec_skill_get_system_prompt());
    ec_agent_init(&s_agent, config, model, &s_session);

    char line[EC_CONFIG_IO_LINE_BUF];
    char response[EC_CONFIG_CONTENT_BUF];

    ec_io_write("EmbedClaw ready. Type /reset to clear history, /quit to exit.\n> ");

    for (;;) {
        int n = ec_io_read_line(line, sizeof(line));
        if (n < 0) break;

        if (line[0] == '\0') {
            ec_io_write("> ");
            continue;
        }
        if (strcmp(line, "/reset") == 0) {
            ec_session_reset(&s_session);
            ec_io_write("Session reset.\n> ");
            continue;
        }
        if (strcmp(line, "/quit") == 0) {
            ec_io_write("Goodbye.\n");
            break;
        }

        int rc = ec_agent_run_turn(&s_agent, line, response, sizeof(response));
        if (rc == 0) {
            ec_io_write(response);
            ec_io_write("\n> ");
        } else {
            char err[64];
            snprintf(err, sizeof(err), "[error: agent rc=%d]\n> ", rc);
            ec_io_write(err);
        }
    }
}

void vEmbedClawTask(void *pvParameters)
{
    (void)pvParameters;

    ec_model_config_t config = {
        .provider = EC_MODEL_PROVIDER_OPENAI_CHAT,
        .host     = EC_CONFIG_API_HOST,
        .port     = EC_CONFIG_API_PORT,
        .api_key  = EC_CONFIG_API_KEY,
        .use_tls  = EC_CONFIG_USE_TLS,
    };

    ec_io_init(&ec_io_telnet_ops);
    run_agent_loop(&config, EC_CONFIG_MODEL);

    for (;;) {}
}
