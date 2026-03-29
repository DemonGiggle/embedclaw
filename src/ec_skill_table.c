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
#include "ec_hw_datasheet.h"

/*
 * Include the ASIC-specific register map header here.
 * Replace with your own chip's header (e.g. ec_hw_my_asic.h).
 * If no ASIC header is included, define EC_HW_NO_DATASHEET to compile
 * with an empty register map.
 */
#if !defined(EC_HW_NO_DATASHEET)
#include "ec_hw_example_asic.h"
#else
const ec_hw_module_t *EC_HW_MODULES      = NULL;
const size_t          EC_HW_MODULE_COUNT = 0;
#endif

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
 * Skill: hw_datasheet
 *
 * Provides lookup tools for the ASIC register map so the LLM can discover
 * modules, registers, and bit fields on demand without a huge system prompt.
 * ============================================================================ */

/* hw_module_list ----------------------------------------------------------
 * args:   {} (no arguments)
 * result: { "modules": [ { "name": "uart0", "base_addr": "0x40001000",
 *                           "description": "..." }, ... ] }              */

static int hw_module_list_fn(const char *args_json,
                             char *out_json, size_t out_size)
{
    (void)args_json;

    ec_json_writer_t w;
    ec_json_writer_init(&w, out_json, out_size);
    ec_json_obj_start(&w);
    ec_json_array_start(&w, "modules");

    for (size_t i = 0; i < EC_HW_MODULE_COUNT; i++) {
        const ec_hw_module_t *m = &EC_HW_MODULES[i];
        ec_json_array_obj_start(&w);
        ec_json_add_string(&w, "name", m->name);

        char addr_str[16];
        snprintf(addr_str, sizeof(addr_str), "0x%08x", (unsigned)m->base_addr);
        ec_json_add_string(&w, "base_addr", addr_str);
        ec_json_add_string(&w, "description", m->description);
        ec_json_obj_end(&w);
    }

    ec_json_array_end(&w);
    ec_json_obj_end(&w);
    return ec_json_writer_finish(&w) < 0 ? -1 : 0;
}

/* hw_register_lookup ------------------------------------------------------
 * args:   { "module": "uart0" }
 *     or: { "module": "uart0", "register": "CTRL" }
 * result: full register + bitfield details                                */

static int hw_register_lookup_fn(const char *args_json,
                                 char *out_json, size_t out_size)
{
    char mod_name[64];
    if (ec_json_find_string(args_json, strlen(args_json),
                            "module", mod_name, sizeof(mod_name)) < 0) {
        snprintf(out_json, out_size, "{\"error\":\"missing module\"}");
        return -1;
    }

    /* Optional: filter to a single register */
    char reg_filter[64] = {0};
    ec_json_find_string(args_json, strlen(args_json),
                        "register", reg_filter, sizeof(reg_filter));

    /* Find the module */
    const ec_hw_module_t *mod = NULL;
    for (size_t i = 0; i < EC_HW_MODULE_COUNT; i++) {
        if (strcmp(EC_HW_MODULES[i].name, mod_name) == 0) {
            mod = &EC_HW_MODULES[i];
            break;
        }
    }
    if (!mod) {
        snprintf(out_json, out_size,
                 "{\"error\":\"unknown module '%s'\"}", mod_name);
        return -1;
    }

    ec_json_writer_t w;
    ec_json_writer_init(&w, out_json, out_size);
    ec_json_obj_start(&w);
    ec_json_add_string(&w, "module", mod->name);

    char addr_str[16];
    snprintf(addr_str, sizeof(addr_str), "0x%08x", (unsigned)mod->base_addr);
    ec_json_add_string(&w, "base_addr", addr_str);
    ec_json_add_string(&w, "description", mod->description);
    if (mod->notes)
        ec_json_add_string(&w, "notes", mod->notes);

    ec_json_array_start(&w, "registers");

    for (size_t r = 0; r < mod->num_registers; r++) {
        const ec_hw_register_t *reg = &mod->registers[r];

        /* If a register filter is specified, skip non-matching registers */
        if (reg_filter[0] && strcmp(reg->name, reg_filter) != 0)
            continue;

        ec_json_array_obj_start(&w);
        ec_json_add_string(&w, "name", reg->name);

        char off_str[16];
        snprintf(off_str, sizeof(off_str), "0x%02x", (unsigned)reg->offset);
        ec_json_add_string(&w, "offset", off_str);

        snprintf(addr_str, sizeof(addr_str), "0x%08x",
                 (unsigned)(mod->base_addr + reg->offset));
        ec_json_add_string(&w, "address", addr_str);

        char rst_str[16];
        snprintf(rst_str, sizeof(rst_str), "0x%08x", (unsigned)reg->reset_value);
        ec_json_add_string(&w, "reset_value", rst_str);
        ec_json_add_string(&w, "description", reg->description);

        /* Bit fields */
        if (reg->num_fields > 0) {
            ec_json_array_start(&w, "fields");
            for (size_t f = 0; f < reg->num_fields; f++) {
                const ec_hw_bitfield_t *bf = &reg->fields[f];
                ec_json_array_obj_start(&w);
                ec_json_add_string(&w, "name", bf->name);

                char bits[16];
                if (bf->hi == bf->lo)
                    snprintf(bits, sizeof(bits), "%u", bf->hi);
                else
                    snprintf(bits, sizeof(bits), "%u:%u", bf->hi, bf->lo);
                ec_json_add_string(&w, "bits", bits);
                ec_json_add_string(&w, "access", bf->access);
                ec_json_add_string(&w, "description", bf->description);
                ec_json_obj_end(&w);
            }
            ec_json_array_end(&w);
        }

        ec_json_obj_end(&w);
    }

    ec_json_array_end(&w);
    ec_json_obj_end(&w);
    return ec_json_writer_finish(&w) < 0 ? -1 : 0;
}

static const ec_tool_def_t s_datasheet_tools[] = {
    {
        .name        = "hw_module_list",
        .description =
            "List all hardware modules on this device. Returns module names, "
            "base addresses, and descriptions. Call this first to discover "
            "what hardware is available.",
        .parameters_schema =
            "{"
              "\"type\":\"object\","
              "\"properties\":{},"
              "\"required\":[]"
            "}",
        .fn = hw_module_list_fn,
    },
    {
        .name        = "hw_register_lookup",
        .description =
            "Look up registers and bit-field definitions for a hardware module. "
            "Returns register names, offsets, absolute addresses, reset values, "
            "bit-field ranges, access types, and descriptions. Optionally filter "
            "to a single register by name.",
        .parameters_schema =
            "{"
              "\"type\":\"object\","
              "\"properties\":{"
                "\"module\":{"
                  "\"type\":\"string\","
                  "\"description\":\"Module name, e.g. 'uart0', 'gpio'\""
                "},"
                "\"register\":{"
                  "\"type\":\"string\","
                  "\"description\":"
                    "\"Optional register name filter, e.g. 'CTRL', 'STATUS'\""
                "}"
              "},"
              "\"required\":[\"module\"]"
            "}",
        .fn = hw_register_lookup_fn,
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
        .name        = "hw_datasheet",
        .description = "Look up hardware module register maps and bit fields.",
        .system_context =
            "You have access to this device's hardware datasheet via tools.\n"
            "Use hw_module_list to discover available hardware modules.\n"
            "Use hw_register_lookup to get register addresses, bit-field "
            "definitions, access types, and programming notes for a module.\n"
            "Always look up register details before reading or writing "
            "hardware registers — do not guess addresses or bit layouts.",
        .tools     = s_datasheet_tools,
        .num_tools = sizeof(s_datasheet_tools) / sizeof(s_datasheet_tools[0]),
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
