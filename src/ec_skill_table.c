/*
 * ============================================================================
 * EmbedClaw Skill Table
 *
 * This is the single file to edit when configuring firmware capabilities.
 *
 * HOW TO ADD A SKILL
 * ------------------
 * 1. Implement your tool handler(s) as static functions below.
 * 2. Declare them in a static ec_tool_def_t array.
 * 3. Add an ec_skill_t entry to s_skill_table[] at the bottom.
 *
 * Each skill contributes:
 *   system_context — text appended to the LLM system prompt; describe what
 *                    the hardware does so the model uses the tools correctly.
 *   tools[]        — functions the LLM can invoke for this skill.
 * ============================================================================
 */

#include "ec_skill.h"
#include "ec_json.h"
#include "ec_http.h"
#include "ec_config.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ============================================================================
 * Base system prompt
 *
 * Describes the device personality before any skill contexts are appended.
 * Edit this string to change the overall agent behaviour.
 * ============================================================================ */

const char EC_BASE_SYSTEM_PROMPT[] =
    "You are an embedded systems assistant running on FreeRTOS. "
    "Answer concisely. "
    "When the user asks about hardware state or configuration, "
    "use your tools to inspect and control the device directly. "
    "Always report register values in hexadecimal.";

/* ============================================================================
 * Skill: hw_register_control
 *
 * Provides 32-bit memory-mapped register read/write.
 * On POSIX builds a small mock register bank is used for testing.
 * ============================================================================ */

#if defined(EC_PLATFORM_POSIX)

#define MOCK_REG_BASE   0x40000000UL
#define MOCK_REG_COUNT  16

static uint32_t s_mock_regs[MOCK_REG_COUNT];

static int mock_read(uint32_t addr, uint32_t *val)
{
    if (addr < MOCK_REG_BASE) return -1;
    size_t idx = (addr - MOCK_REG_BASE) / 4;
    if (idx >= MOCK_REG_COUNT) return -1;
    *val = s_mock_regs[idx];
    return 0;
}

static int mock_write(uint32_t addr, uint32_t val)
{
    if (addr < MOCK_REG_BASE) return -1;
    size_t idx = (addr - MOCK_REG_BASE) / 4;
    if (idx >= MOCK_REG_COUNT) return -1;
    s_mock_regs[idx] = val;
    return 0;
}

#endif /* EC_PLATFORM_POSIX */

/* hw_register_read --------------------------------------------------------- */
/* args:   { "address": "0x40000000" }                                        */
/* result: { "address": "0x40000000", "value": "0x00000000" }                 */

static int hw_reg_read_fn(const char *args_json,
                          char *out_json, size_t out_size)
{
    char addr_str[24];
    if (ec_json_find_string(args_json, strlen(args_json),
                            "address", addr_str, sizeof(addr_str)) < 0) {
        snprintf(out_json, out_size, "{\"error\":\"missing address\"}");
        return -1;
    }

    unsigned long addr = strtoul(addr_str, NULL, 0);
    uint32_t val = 0;

#if defined(EC_PLATFORM_POSIX)
    if (mock_read((uint32_t)addr, &val) != 0) {
        snprintf(out_json, out_size,
                 "{\"error\":\"0x%08lx out of mock range\"}", addr);
        return -1;
    }
#else
    /* SECURITY: add an address allowlist here before deploying */
    val = *(volatile uint32_t *)(uintptr_t)addr;
#endif

    snprintf(out_json, out_size,
             "{\"address\":\"0x%08lx\",\"value\":\"0x%08x\"}",
             addr, (unsigned)val);
    return 0;
}

/* hw_register_write -------------------------------------------------------- */
/* args:   { "address": "0x40000000", "value": "0x00000001" }                 */
/* result: { "ok": true }                                                      */

static int hw_reg_write_fn(const char *args_json,
                           char *out_json, size_t out_size)
{
    char addr_str[24], val_str[24];

    if (ec_json_find_string(args_json, strlen(args_json),
                            "address", addr_str, sizeof(addr_str)) < 0) {
        snprintf(out_json, out_size, "{\"error\":\"missing address\"}");
        return -1;
    }
    if (ec_json_find_string(args_json, strlen(args_json),
                            "value", val_str, sizeof(val_str)) < 0) {
        snprintf(out_json, out_size, "{\"error\":\"missing value\"}");
        return -1;
    }

    unsigned long addr = strtoul(addr_str, NULL, 0);
    unsigned long val  = strtoul(val_str,  NULL, 0);

#if defined(EC_PLATFORM_POSIX)
    if (mock_write((uint32_t)addr, (uint32_t)val) != 0) {
        snprintf(out_json, out_size,
                 "{\"error\":\"0x%08lx out of mock range\"}", addr);
        return -1;
    }
#else
    /* SECURITY: add an address allowlist here before deploying */
    *(volatile uint32_t *)(uintptr_t)addr = (uint32_t)val;
#endif

    snprintf(out_json, out_size, "{\"ok\":true}");
    return 0;
}

static const ec_tool_def_t s_hw_tools[] = {
    {
        .name        = "hw_register_read",
        .description = "Read a 32-bit hardware register at the given address.",
        .parameters_schema =
            "{"
              "\"type\":\"object\","
              "\"properties\":{"
                "\"address\":{"
                  "\"type\":\"string\","
                  "\"description\":"
                    "\"Register address as a hex string, e.g. '0x40000000'\""
                "}"
              "},"
              "\"required\":[\"address\"]"
            "}",
        .fn = hw_reg_read_fn,
    },
    {
        .name        = "hw_register_write",
        .description = "Write a 32-bit value to a hardware register.",
        .parameters_schema =
            "{"
              "\"type\":\"object\","
              "\"properties\":{"
                "\"address\":{"
                  "\"type\":\"string\","
                  "\"description\":"
                    "\"Register address as a hex string, e.g. '0x40000000'\""
                "},"
                "\"value\":{"
                  "\"type\":\"string\","
                  "\"description\":"
                    "\"Value to write as a hex string, e.g. '0x00000001'\""
                "}"
              "},"
              "\"required\":[\"address\",\"value\"]"
            "}",
        .fn = hw_reg_write_fn,
    },
};

/* ============================================================================
 * Skill: web_browsing
 *
 * Provides web_search (Brave Search API) and web_fetch (HTTP GET any URL).
 * On POSIX, EC_BRAVE_API_KEY overrides the compile-time default.
 * ============================================================================ */

static const char *brave_api_key(void)
{
#if defined(EC_PLATFORM_POSIX)
    const char *env = getenv("EC_BRAVE_API_KEY");
    if (env && *env) return env;
#endif
    return EC_CONFIG_BRAVE_API_KEY;
}

/* web_search --------------------------------------------------------------- */
/* args:   { "query": "FreeRTOS task priorities" }                             */
/* result: { "results": [ { "title": "...", "url": "...",                      */
/*                          "description": "..." }, ... ] }                    */

static int web_search_fn(const char *args_json,
                          char *out_json, size_t out_size)
{
    char query[256];
    if (ec_json_find_string(args_json, strlen(args_json),
                            "query", query, sizeof(query)) < 0) {
        snprintf(out_json, out_size, "{\"error\":\"missing query\"}");
        return -1;
    }

    /* URL-encode the query (minimal: spaces → +, keep alphanum and -._~) */
    char encoded[512];
    size_t ei = 0;
    for (const char *p = query; *p && ei < sizeof(encoded) - 4; p++) {
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '.' ||
            *p == '_' || *p == '~') {
            encoded[ei++] = *p;
        } else if (*p == ' ') {
            encoded[ei++] = '+';
        } else {
            ei += (size_t)snprintf(encoded + ei, sizeof(encoded) - ei,
                                    "%%%02X", (unsigned char)*p);
        }
    }
    encoded[ei] = '\0';

    /* Build path: /res/v1/web/search?q=<encoded>&count=N */
    char path[640];
    snprintf(path, sizeof(path),
             "/res/v1/web/search?q=%s&count=%d",
             encoded, EC_CONFIG_WEB_SEARCH_COUNT);

    /* Build auth header */
    char headers[256];
    snprintf(headers, sizeof(headers),
             "Accept: application/json\r\n"
             "X-Subscription-Token: %s\r\n",
             brave_api_key());

    ec_http_request_t req = {
        .method   = "GET",
        .host     = EC_CONFIG_BRAVE_API_HOST,
        .port     = EC_CONFIG_BRAVE_API_PORT,
        .path     = path,
        .headers  = headers,
        .body     = NULL,
        .body_len = 0,
        .use_tls  = EC_CONFIG_USE_TLS,
    };

    char resp_buf[EC_CONFIG_RESPONSE_BUF];
    ec_http_response_t resp;
    int rc = ec_http_request(&req, &resp, resp_buf, sizeof(resp_buf));
    if (rc != 0) {
        snprintf(out_json, out_size,
                 "{\"error\":\"Brave API request failed (rc=%d)\"}", rc);
        return -1;
    }
    if (resp.status_code < 200 || resp.status_code >= 300) {
        snprintf(out_json, out_size,
                 "{\"error\":\"Brave API HTTP %d\"}", resp.status_code);
        return -1;
    }

    /* Parse the Brave response and build a compact result array.
     * Brave returns: { "web": { "results": [ { "title", "url",
     *                  "description", ... }, ... ] } }
     * We extract up to EC_CONFIG_WEB_SEARCH_COUNT results. */
    ec_json_writer_t w;
    ec_json_writer_init(&w, out_json, out_size);
    ec_json_obj_start(&w);
    ec_json_array_start(&w, "results");

    for (int i = 0; i < EC_CONFIG_WEB_SEARCH_COUNT; i++) {
        char ppath[64];
        char title[128], url[256], desc[256];

        snprintf(ppath, sizeof(ppath), "web.results[%d].title", i);
        if (ec_json_find_string(resp.body, resp.body_len,
                                ppath, title, sizeof(title)) < 0)
            break; /* no more results */

        snprintf(ppath, sizeof(ppath), "web.results[%d].url", i);
        if (ec_json_find_string(resp.body, resp.body_len,
                                ppath, url, sizeof(url)) < 0)
            break;

        snprintf(ppath, sizeof(ppath), "web.results[%d].description", i);
        if (ec_json_find_string(resp.body, resp.body_len,
                                ppath, desc, sizeof(desc)) < 0)
            desc[0] = '\0'; /* description is optional */

        ec_json_array_obj_start(&w);
        ec_json_add_string(&w, "title", title);
        ec_json_add_string(&w, "url", url);
        ec_json_add_string(&w, "description", desc);
        ec_json_obj_end(&w);
    }

    ec_json_array_end(&w);
    ec_json_obj_end(&w);
    return ec_json_writer_finish(&w) < 0 ? -1 : 0;
}

/* web_fetch ---------------------------------------------------------------- */
/* args:   { "url": "http://example.com/data.json" }                           */
/* result: { "status": 200, "body": "<truncated page content>" }               */

static int parse_url(const char *url,
                     char *host, size_t host_size,
                     uint16_t *port,
                     char *path, size_t path_size,
                     int *use_tls)
{
    /* Skip scheme */
    const char *p = url;
    *port = 80;
    *use_tls = 0;
    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    } else if (strncmp(p, "https://", 8) == 0) {
        p += 8;
        *port = 443;
        *use_tls = 1;
    }

    /* Extract host */
    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');

    size_t hlen;
    if (colon && (!slash || colon < slash)) {
        /* Explicit port */
        hlen = (size_t)(colon - p);
        *port = (uint16_t)atoi(colon + 1);
    } else if (slash) {
        hlen = (size_t)(slash - p);
    } else {
        hlen = strlen(p);
    }
    if (hlen >= host_size) hlen = host_size - 1;
    memcpy(host, p, hlen);
    host[hlen] = '\0';

    /* Extract path */
    if (slash) {
        strncpy(path, slash, path_size - 1);
        path[path_size - 1] = '\0';
    } else {
        strncpy(path, "/", path_size - 1);
        path[path_size - 1] = '\0';
    }

    return 0;
}

static int web_fetch_fn(const char *args_json,
                         char *out_json, size_t out_size)
{
    char url[512];
    if (ec_json_find_string(args_json, strlen(args_json),
                            "url", url, sizeof(url)) < 0) {
        snprintf(out_json, out_size, "{\"error\":\"missing url\"}");
        return -1;
    }

    char host[128];
    uint16_t port;
    char path[384];
    int use_tls = 0;
    if (parse_url(url, host, sizeof(host), &port,
                  path, sizeof(path), &use_tls) != 0) {
        snprintf(out_json, out_size, "{\"error\":\"invalid url\"}");
        return -1;
    }

    ec_http_request_t req = {
        .method   = "GET",
        .host     = host,
        .port     = port,
        .path     = path,
        .headers  = "Accept: text/html, application/json, text/plain\r\n",
        .body     = NULL,
        .body_len = 0,
        .use_tls  = use_tls,
    };

    char resp_buf[EC_CONFIG_RESPONSE_BUF];
    ec_http_response_t resp;
    int rc = ec_http_request(&req, &resp, resp_buf, sizeof(resp_buf));
    if (rc != 0) {
        snprintf(out_json, out_size,
                 "{\"error\":\"fetch failed (rc=%d)\"}", rc);
        return -1;
    }

    /* Truncate body to fit in output and to EC_CONFIG_WEB_FETCH_MAX */
    size_t max_body = EC_CONFIG_WEB_FETCH_MAX;
    /* Reserve room for JSON wrapper: {"status":999,"body":"...'} + escaping */
    size_t wrapper_overhead = 64;
    if (max_body > out_size - wrapper_overhead)
        max_body = out_size - wrapper_overhead;
    if (resp.body_len > max_body) {
        resp.body[max_body] = '\0';
        resp.body_len = max_body;
    }

    ec_json_writer_t w;
    ec_json_writer_init(&w, out_json, out_size);
    ec_json_obj_start(&w);
    ec_json_add_int(&w, "status", resp.status_code);
    ec_json_add_string(&w, "body", resp.body);
    ec_json_obj_end(&w);
    return ec_json_writer_finish(&w) < 0 ? -1 : 0;
}

static const ec_tool_def_t s_web_tools[] = {
    {
        .name        = "web_search",
        .description =
            "Search the web using Brave Search. Returns a list of results "
            "with title, URL, and description snippet.",
        .parameters_schema =
            "{"
              "\"type\":\"object\","
              "\"properties\":{"
                "\"query\":{"
                  "\"type\":\"string\","
                  "\"description\":\"Search query string\""
                "}"
              "},"
              "\"required\":[\"query\"]"
            "}",
        .fn = web_search_fn,
    },
    {
        .name        = "web_fetch",
        .description =
            "Fetch the content of a web page by URL. Returns the HTTP "
            "status code and the response body (truncated if too large).",
        .parameters_schema =
            "{"
              "\"type\":\"object\","
              "\"properties\":{"
                "\"url\":{"
                  "\"type\":\"string\","
                  "\"description\":"
                    "\"Full URL to fetch, e.g. 'http://example.com/page'\""
                "}"
              "},"
              "\"required\":[\"url\"]"
            "}",
        .fn = web_fetch_fn,
    },
};

/* ============================================================================
 * Skill Table
 *
 * Add or remove ec_skill_t entries here to control what capabilities are
 * compiled into this firmware build.
 * ============================================================================ */

static const ec_skill_t s_skill_table[] = {

    /* ---------------------------------------------------------------------- */
    {
        .name        = "hw_register_control",
        .description = "Read and write 32-bit memory-mapped hardware registers.",
        .system_context =
            "Hardware register access is available via tools.\n"
            "Registers are 32-bit, word-aligned, addressed as hex strings "
            "(e.g. '0x40000000').\n"
            "Read a register to inspect hardware state; write to configure it.",
        .tools     = s_hw_tools,
        .num_tools = sizeof(s_hw_tools) / sizeof(s_hw_tools[0]),
    },
    /* ---------------------------------------------------------------------- */
    {
        .name        = "web_browsing",
        .description = "Search the web and fetch page content.",
        .system_context =
            "You have web access via tools.\n"
            "Use web_search to find information on the internet using "
            "Brave Search. Use web_fetch to retrieve the full content of "
            "a specific URL.\n"
            "web_fetch returns raw HTML/text — summarise the relevant "
            "parts for the user rather than dumping the whole page.",
        .tools     = s_web_tools,
        .num_tools = sizeof(s_web_tools) / sizeof(s_web_tools[0]),
    },
    /* ---------------------------------------------------------------------- */

};

/* Exported symbols referenced by ec_skill.c */
const ec_skill_t *EC_SKILL_TABLE       = s_skill_table;
const size_t      EC_SKILL_TABLE_COUNT =
    sizeof(s_skill_table) / sizeof(s_skill_table[0]);
