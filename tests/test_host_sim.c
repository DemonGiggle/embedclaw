#include "test_runner.h"
#include "mock_http.h"
#include "ec_agent.h"
#include "ec_mmio.h"
#include "ec_session.h"
#include "ec_skill.h"

#include <stdint.h>
#include <string.h>

#define RESP_TEXT(content) \
    "{\"choices\":[{" \
      "\"message\":{\"role\":\"assistant\",\"content\":\"" content "\"}," \
      "\"finish_reason\":\"stop\"}]}"

#define RESP_REG_WRITE(call_id, addr, val) \
    "{\"choices\":[{" \
      "\"message\":{\"role\":\"assistant\",\"content\":null," \
        "\"tool_calls\":[{" \
          "\"id\":\"" call_id "\"," \
          "\"type\":\"function\"," \
          "\"function\":{" \
            "\"name\":\"hw_register_write\"," \
            "\"arguments\":\"{\\\"address\\\":\\\"" addr "\\\"," \
                            "\\\"value\\\":\\\"" val "\\\"}\"" \
          "}" \
        "}]}," \
      "\"finish_reason\":\"tool_calls\"}]}"

static ec_session_t s_session;
static ec_agent_t   s_agent;

static const ec_model_config_t s_mock_cfg = {
    .provider = EC_MODEL_PROVIDER_SIM_MOCK,
    .host     = "unused",
    .port     = 0,
    .api_key  = "unused",
    .use_tls  = 0,
};

static const ec_model_config_t s_real_cfg = {
    .provider = EC_MODEL_PROVIDER_OPENAI_CHAT,
    .host     = "mock-host",
    .port     = 80,
    .api_key  = "test-key",
    .use_tls  = 0,
};

static void setup(const ec_model_config_t *cfg)
{
    mock_http_reset();
    ec_mmio_reset();
    ec_session_init(&s_session, ec_skill_get_system_prompt());
    ec_agent_init(&s_agent, cfg, "test-model", &s_session);
}

static int test_mock_model_register_bookkeeping(void)
{
    setup(&s_mock_cfg);

    char response[EC_CONFIG_CONTENT_BUF];
    int rc = ec_agent_run_turn(&s_agent,
                               "write register 0x40001000 to 0x00000005",
                               response, sizeof(response));
    ASSERT_EQ(rc, 0, "mock-model write turn should succeed");
    ASSERT_STR(response, "write completed", "mock-model write should complete");
    ASSERT_EQ(mock_http_call_count(), 0, "mock-model path should not use HTTP");
    ASSERT_EQ(ec_mmio_entry_count(), 1, "write should create one MMIO entry");

    uint32_t address = 0;
    uint32_t value = 0;
    ASSERT_EQ(ec_mmio_entry_at(0, &address, &value), 0,
              "first MMIO entry should be readable");
    ASSERT_EQ(address, 0x40001000U, "entry address should match write target");
    ASSERT_EQ(value, 0x00000005U, "entry value should match written value");

    rc = ec_agent_run_turn(&s_agent,
                           "read register 0x40001000",
                           response, sizeof(response));
    ASSERT_EQ(rc, 0, "mock-model read turn should succeed");
    ASSERT_STR(response, "0x00000005", "read should report bookkeeping value");
    ASSERT_EQ(mock_http_call_count(), 0, "mock-model read should still avoid HTTP");
    return 1;
}

static int test_mock_model_denies_unknown_register(void)
{
    setup(&s_mock_cfg);

    char response[EC_CONFIG_CONTENT_BUF];
    int rc = ec_agent_run_turn(&s_agent,
                               "write register 0x40009999 to 0x00000001",
                               response, sizeof(response));
    ASSERT_EQ(rc, 0, "denied write should still complete the turn");
    ASSERT_STR(response, "policy denied write", "response should surface policy denial");
    ASSERT_EQ(ec_mmio_entry_count(), 0, "denied access should not mutate MMIO state");
    return 1;
}

static int test_real_provider_path_in_host_sim(void)
{
    setup(&s_real_cfg);

    mock_http_queue(RESP_REG_WRITE("call_w1", "0x40001000", "0x00000002"), 200);
    mock_http_queue(RESP_TEXT("Written successfully."), 200);

    char response[EC_CONFIG_CONTENT_BUF];
    int rc = ec_agent_run_turn(&s_agent,
                               "write register 0x40001000 to 0x00000002",
                               response, sizeof(response));
    ASSERT_EQ(rc, 0, "real-provider path should succeed in host sim");
    ASSERT_EQ(mock_http_call_count(), 2, "real-provider path should use mocked HTTP");
    ASSERT_STR(response, "Written successfully", "final response should come from HTTP backend");
    ASSERT_EQ(ec_mmio_entry_count(), 1, "real-provider tool path should update MMIO");

    uint32_t address = 0;
    uint32_t value = 0;
    ASSERT_EQ(ec_mmio_entry_at(0, &address, &value), 0,
              "host-sim MMIO entry should be inspectable");
    ASSERT_EQ(address, 0x40001000U, "real-provider path should touch expected address");
    ASSERT_EQ(value, 0x00000002U, "real-provider path should write expected value");

    const char *req2 = mock_http_req_body(1);
    ASSERT(req2 != NULL, "second request body should be captured");
    ASSERT_STR(req2, "\"role\":\"tool\"", "tool result should be fed back to the provider");
    ASSERT_STR(req2, "0x00000002", "tool result should carry the written value");
    return 1;
}

int main(void)
{
    ec_skill_init();

    printf("=== EmbedClaw Host Simulation Tests ===\n\n");

    RUN_TEST(test_mock_model_register_bookkeeping);
    RUN_TEST(test_mock_model_denies_unknown_register);
    RUN_TEST(test_real_provider_path_in_host_sim);

    PRINT_RESULTS();
    return (_tests_pass == _tests_run) ? 0 : 1;
}
