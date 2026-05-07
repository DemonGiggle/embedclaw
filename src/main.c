#include "ec_model.h"
#include "ec_agent.h"
#include "ec_session.h"
#include "ec_skill.h"
#include "ec_io.h"
#include "ec_config.h"
#include "ec_log.h"

#include <stdio.h>
#include <string.h>

/* Static allocation — kept out of the stack */
static ec_session_t s_session;
static ec_agent_t   s_agent;

static void run_agent_loop(const ec_model_config_t *config, const char *model)
{
    /* Initialise debug logging (checks EC_DEBUG env on POSIX) */
    ec_log_init();

    /* Initialise skills: registers all tools and builds the system prompt */
    ec_skill_init();

    ec_session_init(&s_session, ec_skill_get_system_prompt());
    ec_agent_init(&s_agent, config, model, &s_session);

    char line[EC_CONFIG_IO_LINE_BUF];
    char response[EC_CONFIG_CONTENT_BUF];

#if defined(EC_PLATFORM_POSIX) && defined(EC_CONFIG_HOST_SIM) && EC_CONFIG_HOST_SIM
    ec_io_write("EmbedClaw host simulation ready. Type /reset to clear history, /quit to exit.\n> ");
#else
    ec_io_write("EmbedClaw ready. Type /reset to clear history, /quit to exit.\n> ");
#endif

    for (;;) {
        int n = ec_io_read_line(line, sizeof(line));
        if (n < 0) break; /* I/O closed */

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

/* =========================================================================
 * Platform entry points
 * ========================================================================= */

#if defined(EC_PLATFORM_FREERTOS)

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

    /* Use Telnet I/O on FreeRTOS — swap for ec_io_uart_ops for UART */
    ec_io_init(&ec_io_telnet_ops);
    run_agent_loop(&config, EC_CONFIG_MODEL);

    for (;;) {} /* task must not return */
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

    /* Select I/O backend:
     *   EC_IO=telnet  — TCP server on EC_CONFIG_TELNET_PORT (default 2323)
     *   EC_IO=uart    — stdin/stdout (default)                            */
    const char *io_mode = getenv("EC_IO");
    if (io_mode && strcmp(io_mode, "telnet") == 0) {
        ec_io_init(&ec_io_telnet_ops);
    } else {
        ec_io_init(&ec_io_uart_ops);
    }

    ec_model_config_t config = {
        .provider = EC_MODEL_PROVIDER_OPENAI_CHAT,
        .host     = host,
        .port     = (uint16_t)port,
        .api_key  = api_key,
        .use_tls  = EC_CONFIG_USE_TLS,
    };

    (void)argc; (void)argv;
    run_agent_loop(&config, model);
    return 0;
}

#endif
