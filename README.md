# EmbedClaw

An embedded AI agent runtime for FreeRTOS. EmbedClaw runs on constrained hardware and acts as an intelligent automation layer: it accepts user input over a serial interface (UART, Telnet, or similar), forwards it to a remote OpenAI-compatible LLM, executes tool calls returned by the LLM (such as reading and writing hardware registers), and returns the final answer to the user.

It is the embedded counterpart to OpenClaw — same agentic loop, same OpenAI tool-call protocol, different execution environment.

---

## Features

- **OpenAI-compatible** — uses the standard `/v1/chat/completions` API with `tool_calls` JSON format; no custom protocol
- **TLS/HTTPS** — mbedTLS integration with embedded CA bundle; no filesystem required
- **Agentic loop** — dispatches tool calls from the LLM, feeds results back, and loops until a final text response
- **Skill system** — compile-time capability bundles; each skill contributes tools and LLM system context
- **Built-in tools** — hardware register read/write, web search (Brave API), and web page fetch
- **Extensible tool framework** — register new tools with a name, JSON Schema, and a C handler function
- **Persistent conversation** — session history survives UART/Telnet reconnects across the device lifetime
- **Transport-agnostic I/O** — swap between UART and Telnet (or add new transports) without touching the agent logic
- **No dynamic allocation in hot paths** — all buffers are statically sized; predictable memory usage on embedded targets
- **Single-threaded** — no RTOS threading required; the entire agent loop runs to completion in one task
- **POSIX build** — full host build for development and testing (no hardware required)

---

## Architecture

```
  User
   │
   │  UART / Telnet / (extensible)
   ▼
┌─────────────────────────┐
│  I/O Layer  (ec_io)     │  read_line / write ops
└────────────┬────────────┘
             │
             ▼
┌─────────────────────────┐
│  Session  (ec_session)  │  conversation history, persistent across reconnects
└────────────┬────────────┘
             │
             ▼
┌─────────────────────────┐
│  Agent Loop (ec_agent)  │  user → LLM → tool dispatch → loop → response
└──────┬──────────┬───────┘
       │          │
       ▼          ▼
┌────────────┐  ┌─────────────────────────────────────┐
│  Chat API  │  │  Skill System  (ec_skill)            │
│  (ec_api)  │  │  ┌───────────────────────────────┐   │
│  JSON +    │  │  │ hw_register_control            │   │
│  HTTP POST │  │  │  hw_register_read/write        │   │
│            │  │  ├───────────────────────────────┤   │
│            │  │  │ web_browsing                   │   │
│            │  │  │  web_search, web_fetch         │   │
│            │  │  └───────────────────────────────┘   │
└─────┬──────┘  └──────────────┬──────────────────────┘
      │                        │  (web tools also use HTTP)
      ├────────────────────────┘
      ▼
┌─────────────────────────┐
│  HTTP Client (ec_http)  │  HTTP/1.1, chunked transfer encoding
└────────────┬────────────┘
             │
             ▼
┌─────────────────────────┐
│  Socket  (ec_socket)    │  TCP + optional TLS (mbedTLS)
│                         │  FreeRTOS+TCP / POSIX
└─────────────────────────┘
```

---

## Building

### Requirements

- CMake ≥ 3.10
- C99 compiler (GCC or Clang)
- mbedTLS (included as a git submodule for TLS/HTTPS support)
- For FreeRTOS builds: FreeRTOS+TCP and a cross-compiler toolchain

### Cloning

```sh
git clone --recurse-submodules https://github.com/user/embedclaw.git
# Or if already cloned:
git submodule update --init
```

### Host build (POSIX)

```sh
mkdir build && cd build
cmake -DEC_PLATFORM=POSIX ..
make
```

To build without TLS (no mbedTLS dependency):

```sh
cmake -DEC_PLATFORM=POSIX -DEC_ENABLE_TLS=OFF ..
```

This produces `embedclaw_demo`, `libembedclaw.a`, and `embedclaw_tests`.

### Running tests

From the build directory (POSIX only):

```sh
make embedclaw_tests
ctest --verbose
```

The test suite (`tests/test_e2e.c`) runs the full agent stack with a mock HTTP layer instead of real networking.

### FreeRTOS build

```sh
mkdir build && cd build
cmake -DEC_PLATFORM=FREERTOS -DFREERTOS_PATH=/path/to/freertos ..
make
```

---

## Running the demo

Set your API credentials and start the agent:

```sh
export EC_API_KEY=sk-...
export EC_API_HOST=api.openai.com
export EC_API_PORT=443
export EC_MODEL=gpt-4o
export EC_BRAVE_API_KEY=BSA-...   # optional, for web_search

./build/embedclaw_demo
```

The demo defaults to **stdin/stdout** (UART mode). To use the Telnet backend instead, connect on port 2323:

```sh
EC_IO=telnet ./build/embedclaw_demo &
telnet localhost 2323
```

### Session commands

| Command  | Effect                          |
|----------|---------------------------------|
| `/reset` | Clear conversation history      |
| `/quit`  | Exit the agent loop             |

---

## Configuration

All limits are compile-time constants in `include/ec_config.h`:

| Constant | Default | Description |
|---|---|---|
| **API endpoint** | | |
| `EC_CONFIG_API_HOST` | `api.openai.com` | LLM API hostname |
| `EC_CONFIG_API_PORT` | `443` | LLM API port |
| `EC_CONFIG_USE_TLS` | `1` | Enable TLS (set to 0 for plain HTTP) |
| `EC_CONFIG_MODEL` | `gpt-4o` | Model name |
| **HTTP / buffers** | | |
| `EC_CONFIG_REQUEST_BUF` | `4096` | Outgoing JSON request body (bytes) |
| `EC_CONFIG_RESPONSE_BUF` | `8192` | Raw HTTP response body (bytes) |
| `EC_CONFIG_CONTENT_BUF` | `2048` | Extracted LLM text response (bytes) |
| `EC_CONFIG_TOOL_ARG_BUF` | `256` | Per-tool-call arguments JSON (bytes) |
| `EC_CONFIG_TOOL_RESULT_BUF` | `4096` | Per-tool result buffer (bytes) |
| **Session** | | |
| `EC_CONFIG_SESSION_CONTENT_BUF` | `512` | Per-message content in history |
| `EC_CONFIG_MAX_HISTORY` | `64` | Max messages in conversation history |
| `EC_CONFIG_MAX_TOOL_CALLS` | `4` | Max tool calls per LLM response |
| `EC_CONFIG_MAX_AGENT_ITERS` | `8` | Max tool-call iterations per turn |
| **Tool / skill framework** | | |
| `EC_CONFIG_MAX_TOOLS` | `16` | Max registered tools |
| `EC_CONFIG_MAX_SKILLS` | `16` | Max registered skills |
| `EC_CONFIG_SYSTEM_PROMPT_BUF` | `2048` | Combined system prompt buffer (bytes) |
| **I/O layer** | | |
| `EC_CONFIG_IO_LINE_BUF` | `256` | User input line buffer (bytes) |
| `EC_CONFIG_TELNET_PORT` | `2323` | Telnet listen port |
| **Web browsing skill** | | |
| `EC_CONFIG_BRAVE_API_HOST` | `api.search.brave.com` | Brave Search API hostname |
| `EC_CONFIG_BRAVE_API_PORT` | `443` | Brave Search API port |
| `EC_CONFIG_BRAVE_API_KEY` | `BSA-CHANGE-ME` | Brave Search subscription token |
| `EC_CONFIG_WEB_FETCH_MAX` | `4096` | Max bytes returned by web_fetch |
| `EC_CONFIG_WEB_SEARCH_COUNT` | `5` | Number of search results to return |
| **Debug** | | |
| `EC_CONFIG_DEBUG_LOG` | `0` | Enable debug logging (FreeRTOS; POSIX uses `EC_DEBUG` env) |

---

## Adding a tool

Implement `ec_tool_fn_t`, fill in an `ec_tool_def_t`, and call `ec_tool_register()` at startup:

```c
#include "ec_tool.h"

static int my_tool_fn(const char *args_json, char *out_json, size_t out_size)
{
    /* parse args_json with ec_json_find_string(), do work, write result */
    snprintf(out_json, out_size, "{\"status\":\"ok\"}");
    return 0;
}

static const ec_tool_def_t my_tool = {
    .name        = "my_tool",
    .description = "Does something useful on the device.",
    .parameters_schema =
        "{"
          "\"type\":\"object\","
          "\"properties\":{"
            "\"param\":{\"type\":\"string\",\"description\":\"A parameter\"}"
          "},"
          "\"required\":[\"param\"]"
        "}",
    .fn = my_tool_fn,
};

/* Call once at startup, before the agent loop */
ec_tool_register(&my_tool);
```

The tool is automatically advertised to the LLM and dispatched when called.

---

## Adding an I/O transport

Implement `ec_io_ops_t` and call `ec_io_init()`:

```c
#include "ec_io.h"

static int my_read_line(char *buf, size_t size) { /* ... */ }
static int my_write(const char *str)             { /* ... */ }

static const ec_io_ops_t my_io_ops = {
    .read_line = my_read_line,
    .write     = my_write,
};

ec_io_init(&my_io_ops);
```

---

## Web browsing tools

Two tools provide web access, registered via the `web_browsing` skill:

**`web_search`** — search the web using the Brave Search API.
```
arguments: { "query": "FreeRTOS task priorities" }
result:    { "results": [ { "title": "...", "url": "...", "description": "..." }, ... ] }
```

**`web_fetch`** — fetch the content of a URL via HTTP GET.
```
arguments: { "url": "http://example.com/data.json" }
result:    { "status": 200, "body": "<page content, truncated>" }
```

The Brave Search API key is configured via `EC_BRAVE_API_KEY` (environment variable on POSIX, or `EC_CONFIG_BRAVE_API_KEY` at compile time). Response bodies from `web_fetch` are truncated to `EC_CONFIG_WEB_FETCH_MAX` (default 4096 bytes).

---

## Hardware register tools

Two tools are registered via the `hw_register_control` skill:

**`hw_register_read`** — read a 32-bit memory-mapped register.
```
arguments: { "address": "0x40000000" }
result:    { "address": "0x40000000", "value": "0x00000001" }
```

**`hw_register_write`** — write a 32-bit value to a register.
```
arguments: { "address": "0x40000000", "value": "0x00000001" }
result:    { "ok": true }
```

On POSIX builds, a 16-register mock array at base `0x40000000` is used instead of real hardware.

> **Security note**: On production builds, restrict valid address ranges in `ec_tool.c` before deploying. The LLM controls the address argument.

---

## Debug logging

Set `EC_DEBUG=1` to trace the full agent loop — every LLM request/response JSON body, tool dispatches with arguments and results, and iteration counts. All output goes to stderr:

```sh
EC_DEBUG=1 ./build/embedclaw_demo 2>debug.log
```

On FreeRTOS, enable at compile time by setting `EC_CONFIG_DEBUG_LOG=1` in `ec_config.h`.

Example output:

```
[EC_DEBUG] === agent turn start: "read register 0x40000000"
[EC_DEBUG] --- iteration 1/8 ---
[EC_DEBUG] sending 2 messages to LLM
[EC_DEBUG] >>> LLM request: POST api.openai.com:443/v1/chat/completions
[EC_DEBUG] --- request body (1468 bytes) ---
{"model":"gpt-4o","messages":[...], "tools":[...]}
[EC_DEBUG] --- end request body ---
[EC_DEBUG] <<< LLM response: HTTP 200
[EC_DEBUG] --- response body (223 bytes) ---
{"choices":[{"message":{"tool_calls":[...]},"finish_reason":"tool_calls"}]}
[EC_DEBUG] --- end response body ---
[EC_DEBUG] LLM requested 1 tool call(s)
[EC_DEBUG] dispatching tool [0]: hw_register_read (id=call_001)
[EC_DEBUG]   args: {"address":"0x40000000"}
[EC_DEBUG]   result: {"address":"0x40000000","value":"0x00000000"}
[EC_DEBUG] --- iteration 2/8 ---
[EC_DEBUG] ...
[EC_DEBUG] === agent turn complete (iter 2) ===
```

---

## Roadmap

- [x] TLS/HTTPS via mbedTLS (POSIX, with embedded CA bundle)
- [x] Debug logging (`EC_DEBUG=1`) for LLM request/response inspection
- [ ] FreeRTOS+TCP socket backend (`ec_socket.c`)
- [ ] FreeRTOS UART and Telnet I/O backends
- [ ] FreeRTOS TLS support (socket layer already TLS-aware)
- [ ] Flash/NVS persistence for conversation history across power cycles
- [ ] Hardware register address allowlist for production safety
- [ ] Minimal mbedTLS config for reduced binary size on embedded targets

See [plan.md](plan.md) for the detailed implementation plan and [spec.md](spec.md) for the full design specification.

---

## License

MIT
