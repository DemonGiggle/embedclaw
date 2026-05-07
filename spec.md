EmbedClaw
===

## Purpose

EmbedClaw is an embedded AI agent runtime for FreeRTOS. It runs on constrained
hardware and acts as an intelligent automation layer: it accepts user input over
a serial interface (UART, Telnet, or similar), forwards it to a remote
OpenAI-compatible LLM, and executes tool calls returned by the LLM — such as
reading and writing hardware registers or searching the web — before returning
the final answer to the user.

It is the embedded counterpart to OpenClaw: same agentic loop, same tool-call
protocol, different execution environment.

---

## Design Principles

- **Single-threaded**: No RTOS threads or callbacks. The agent loop runs to
  completion in a single task. All I/O is blocking with timeouts.
- **No dynamic allocation in hot paths**: All buffers are caller-provided or
  statically declared. No `malloc` in the agent or tool layers.
- **Minimal external dependencies**: JSON, HTTP, and the agent loop are all
  built in. mbedTLS is the only third-party dependency (for TLS/HTTPS).
- **OpenAI-compatible protocol**: Tool calls use the standard OpenAI
  `tool_calls` JSON format. No custom protocol invented.
- **Extensible via skills**: New capabilities are added as skills — compile-time
  bundles that contribute tools and LLM system context.
- **Extensible I/O**: New input sources can be added without touching the agent
  core.
- **No streaming**: Responses are buffered in full before processing. Streaming
  (SSE) is deferred — tool calls cannot be dispatched until the full response
  is received anyway, so streaming adds complexity with no benefit to the
  agentic loop.
- **Embedded CA bundle**: TLS certificate validation uses a compiled-in CA
  bundle — no filesystem required (suitable for FreeRTOS/baremetal).

---

## System Overview

```
  User
   │
   │  UART / Telnet / (future: USB CDC, BLE UART, ...)
   ▼
┌─────────────────────────────────────────────────────┐
│  I/O Abstraction Layer  (ec_io)                     │
│  ec_io_read_line(), ec_io_write()                   │
└────────────────────┬────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────┐
│  Session Layer  (ec_session)                        │
│  Conversation history (system + prior turns)        │
│  Persistent across connect/disconnect               │
└────────────────────┬────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────┐
│  Agent Loop  (ec_agent)                             │
│  1. Append user message to history                  │
│  2. Send full history to LLM                        │
│  3. Receive response                                │
│  4. If tool_calls → dispatch to skill/tool layer    │
│     append tool results → go to 2                   │
│  5. If text response → send to user via I/O layer   │
│                                                     │
│  Debug logging via ec_log (EC_DEBUG=1 / compile)    │
└──────────┬──────────────────────┬───────────────────┘
           │                      │
           ▼                      ▼
┌────────────────────┐  ┌─────────────────────────────┐
│  Chat API Layer    │  │  Skill System  (ec_skill)    │
│  (ec_api)          │  │  ┌────────────────────────┐  │
│  JSON build/parse  │  │  │ hw_register_control    │  │
│  /v1/chat/complete │  │  │  hw_register_read/     │  │
│                    │  │  │  hw_register_write     │  │
│                    │  │  ├────────────────────────┤  │
│                    │  │  │ web_browsing           │  │
│                    │  │  │  web_search (Brave)    │  │
│                    │  │  │  web_fetch (HTTP GET)  │  │
│                    │  │  └────────────────────────┘  │
└────────┬───────────┘  └───────────┬─────────────────┘
         │                          │ (web tools also use HTTP)
         ├──────────────────────────┘
         ▼
┌─────────────────────────────────────────────────────┐
│  HTTP Client  (ec_http)                             │
│  HTTP/1.1, chunked transfer encoding                │
└────────────────────┬────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────┐
│  Socket Abstraction  (ec_socket)                    │
│  TCP + optional TLS (mbedTLS, embedded CA bundle)   │
│  FreeRTOS+TCP backend  /  POSIX shim (host testing) │
└─────────────────────────────────────────────────────┘
```

---

## Components

### 1. Socket Abstraction (`ec_socket.h` / `ec_socket.c`)

Wraps the platform TCP API and optional TLS into four functions:

```c
ec_socket_t *ec_socket_connect(const char *host, uint16_t port, int use_tls);
int          ec_socket_send(ec_socket_t *s, const void *data, size_t len);
int          ec_socket_recv(ec_socket_t *s, void *buf, size_t len, uint32_t timeout_ms);
void         ec_socket_close(ec_socket_t *s);
```

Two backends selected at compile time via `EC_PLATFORM`:
- `POSIX` — standard BSD sockets, used for host-side development and testing.
- `FREERTOS` — FreeRTOS+TCP sockets (targeting FreeRTOS+TCP 10.2.1).

**TLS support** (when `EC_CONFIG_USE_TLS=1`):
- mbedTLS v3.6.5 integrated as a git submodule (`third_party/mbedtls`).
- TLS is transparent to callers — `ec_socket_connect()` performs the handshake
  when `use_tls=1`, and `send`/`recv` route through `mbedtls_ssl_write`/`read`.
- Certificate validation uses an embedded CA bundle (`ec_cacerts.h`) — no
  filesystem paths. Suitable for FreeRTOS/baremetal targets.
- SNI (Server Name Indication) is set for virtual hosting support.

### 2. HTTP Client (`ec_http.h` / `ec_http.c`)

Minimal HTTP/1.1 client. Supports:
- `POST` and `GET` with arbitrary headers and body
- Response status line and header parsing
- Chunked transfer-encoding (in-place decode)
- Caller-provided request and response buffers (no malloc)
- `use_tls` field in request struct — passed through to `ec_socket_connect()`

Non-streaming only. The full response body is buffered before returning.

### 3. JSON Builder / Parser (`ec_json.h` / `ec_json.c`)

Purpose-built for the OpenAI message format. No malloc, no DOM.

- **Builder** (`ec_json_writer_t`): Writes JSON tokens into a fixed-size buffer.
  Helpers: `ec_json_obj_start/end`, `ec_json_array_start/end`,
  `ec_json_add_string`, `ec_json_add_int`, `ec_json_add_raw`, etc.
- **Parser**: Pull-parser that finds values by key path (e.g.,
  `"choices[0].message.content"`, `"choices[0].message.tool_calls[0].id"`).
  Scans the raw JSON string without constructing any in-memory tree.

### 4. Chat API Layer (`ec_api.h` / `ec_api.c`)

Builds the OpenAI `/v1/chat/completions` request JSON, sends it via the HTTP
layer, and parses the response. Handles both plain text responses and
`tool_calls` responses.

```c
typedef enum {
    EC_API_RESP_MESSAGE,    /* choices[0].message.content is set */
    EC_API_RESP_TOOL_CALLS, /* choices[0].message.tool_calls[] is set */
} ec_api_resp_type_t;

typedef struct {
    char    id[64];
    char    name[64];
    char    arguments[EC_CONFIG_TOOL_ARG_BUF]; /* raw JSON string */
} ec_api_tool_call_t;

typedef struct {
    ec_api_resp_type_t  type;
    char                content[EC_CONFIG_CONTENT_BUF]; /* if MESSAGE */
    ec_api_tool_call_t  tool_calls[EC_CONFIG_MAX_TOOL_CALLS]; /* if TOOL_CALLS */
    int                 num_tool_calls;
} ec_api_response_t;

int ec_api_chat_completion(
    const ec_api_config_t  *config,
    const char             *model,
    const ec_api_message_t *messages,
    size_t                  num_messages,
    const ec_api_tool_def_t *tools,
    size_t                  num_tools,
    ec_api_response_t      *out
);
```

Debug logging (`ec_log.h`) traces the full request and response JSON bodies
when enabled.

### 5. Tool Framework (`ec_tool.h` / `ec_tool.c`)

Provides a registry of named tools and a dispatcher. Each tool is a C function
that receives a JSON arguments string and writes a JSON result string.

```c
typedef int (*ec_tool_fn_t)(
    const char *args_json,
    char       *out_json,
    size_t      out_size
);

typedef struct {
    const char    *name;
    const char    *description;
    const char    *parameters_schema; /* JSON Schema string, sent to LLM */
    ec_tool_fn_t   fn;
} ec_tool_def_t;

int ec_tool_register(const ec_tool_def_t *def);
int ec_tool_dispatch(const ec_api_tool_call_t *call, char *out_json, size_t out_size);
const ec_api_tool_def_t *ec_tool_api_defs(size_t *count);
```

### 6. Capability Bundle System (`ec_skill.h` / `ec_skill.c` / `ec_skill_table.c`)

Capability bundles are compile-time capability groups. Each bundle contributes:
- One or more tools (registered via `ec_tool_register`)
- A system prompt fragment (appended to the LLM system message)
- A policy note that marks the bundle as local, privileged, or external

```c
typedef struct {
    const char *name;
    const char *system_context;
    const char *policy_note;
    ec_capability_policy_t policy;
} ec_capability_bundle_t;
```

**Built-in capability bundles:**

- **`hw_register_control`** — `hw_register_read` and `hw_register_write` tools.
  On POSIX: 16-register mock array at base `0x40000000`.
  On FreeRTOS: direct memory-mapped register access.

- **`hw_datasheet`** — `hw_module_list` and `hw_register_lookup` tools.
  Provides on-demand access to the device's register map so the LLM can
  discover modules, register addresses, bit-field definitions, access types,
  and programming notes without a large system prompt. The register map is
  defined as compile-time const tables via `ec_hw_datasheet.h` data structures.
  Each ASIC has its own header (e.g., `ec_hw_example_asic.h`).

- **`web_browsing`** — `web_search` (Brave Search API) and `web_fetch` (HTTP
  GET). The Brave API key is configured via `EC_BRAVE_API_KEY` env (POSIX) or
  `EC_CONFIG_BRAVE_API_KEY` (compile-time).

### 7. Session Layer (`ec_session.h` / `ec_session.c`)

Maintains conversation history across user turns. The history is a fixed-size
ring of `ec_api_message_t` entries (role + content). When full, oldest entries
are evicted.

Roles in the history: `system`, `user`, `assistant`, `tool`.

The session is persistent for the lifetime of the device (not cleared on
UART/Telnet reconnect). A user command (e.g., `/reset`) can clear it explicitly.

```c
void ec_session_init(ec_session_t *s, const char *system_prompt);
int  ec_session_append(ec_session_t *s, const char *role, const char *content);
int  ec_session_append_tool_calls(ec_session_t *s,
                                   const ec_api_tool_call_t *calls, int count);
int  ec_session_append_tool_result(ec_session_t *s,
                                    const char *tool_call_id,
                                    const char *content);
void ec_session_reset(ec_session_t *s);
const ec_api_message_t *ec_session_messages(const ec_session_t *s, size_t *count);
```

### 8. Agent Loop (`ec_agent.h` / `ec_agent.c`)

The core agentic loop. Single-threaded, blocking.

```
ec_agent_run_turn(agent, user_input):
  1. session_append(user, user_input)
  2. loop (max EC_CONFIG_MAX_AGENT_ITERS):
       resp = ec_api_chat_completion(session.messages, tools)
       if resp.type == MESSAGE:
           session_append(assistant, resp.content)
           return resp.content
       if resp.type == TOOL_CALLS:
           session_append_tool_calls(resp.tool_calls)
           for each call in resp.tool_calls:
               result = ec_tool_dispatch(call)
               session_append_tool_result(call.id, result)
           // loop back: send updated history to LLM
```

Debug logging traces each iteration, tool dispatch, and LLM response type.

### 9. I/O Abstraction (`ec_io.h` / `ec_io.c`)

Decouples the agent from the physical input/output channel. New transports
(UART, Telnet, USB CDC, BLE UART) implement the same interface:

```c
typedef struct {
    int (*read_line)(char *buf, size_t size);
    int (*write)(const char *str);
} ec_io_ops_t;

void ec_io_init(const ec_io_ops_t *ops);
int  ec_io_read_line(char *buf, size_t size);
int  ec_io_write(const char *str);
```

Implementations:
- **UART** (`ec_io_uart.c`): wraps POSIX stdin/stdout on host builds and uses
  board-supplied FreeRTOS UART HAL hooks via `ec_io_uart_set_hal()` on
  embedded builds.
- **Telnet** (`ec_io_telnet.c`): wraps a blocking single-client TCP server on
  POSIX and FreeRTOS+TCP builds.

### 10. Debug Logging (`ec_log.h` / `ec_log.c`)

Compile-time/runtime debug tracing. All output goes to stderr.

- On POSIX: enabled at runtime via `EC_DEBUG=1` environment variable.
- On FreeRTOS: enabled at compile time via `EC_CONFIG_DEBUG_LOG=1`.

Traces:
- Full JSON request/response bodies for LLM API calls
- Tool call dispatches: name, arguments, and result
- Agent loop iterations and turn boundaries
- API errors

```c
EC_LOG_DEBUG(fmt, ...)           /* single-line debug message */
EC_LOG_BODY(label, buf, len)     /* large buffer dump with label */
```

---

## Build System

CMake-based. Two build profiles:

```
cmake -DEC_PLATFORM=POSIX ..        # host build for development/testing
cmake -DEC_PLATFORM=FREERTOS \
      -DFREERTOS_PATH=... ..        # cross-compile for embedded target
```

Optional flags:
- `-DEC_ENABLE_TLS=OFF` — disable TLS (removes mbedTLS dependency)

Output: static library `libembedclaw.a`, demo executable `embedclaw_demo`,
and test executable `embedclaw_tests` (POSIX only).

mbedTLS is included as a git submodule at `third_party/mbedtls`. Tests compile
with `EC_CONFIG_USE_TLS=0` and use a mock HTTP layer, so they never touch the
socket/TLS stack.

---

## Buffer Sizing (configurable in `ec_config.h`)

| Constant                      | Default | Purpose                              |
|-------------------------------|---------|--------------------------------------|
| `EC_CONFIG_REQUEST_BUF`       | 8192    | HTTP request body (outgoing JSON)    |
| `EC_CONFIG_RESPONSE_BUF`      | 8192    | HTTP response body (incoming JSON)   |
| `EC_CONFIG_CONTENT_BUF`       | 2048    | Extracted LLM text content           |
| `EC_CONFIG_TOOL_ARG_BUF`      | 256     | Tool call arguments JSON             |
| `EC_CONFIG_TOOL_RESULT_BUF`   | 4096    | Per-tool result buffer               |
| `EC_CONFIG_IO_LINE_BUF`       | 256     | One line of user input               |
| `EC_CONFIG_SESSION_CONTENT_BUF`| 512    | Per-message content in history       |
| `EC_CONFIG_SYSTEM_PROMPT_BUF` | 2048    | Combined system prompt               |
| `EC_CONFIG_MAX_TOOL_CALLS`    | 4       | Max tool_calls in one LLM response   |
| `EC_CONFIG_MAX_HISTORY`       | 64      | Max messages in conversation history |
| `EC_CONFIG_MAX_TOOLS`         | 16      | Max registered tools                 |
| `EC_CONFIG_MAX_SKILLS`        | 16      | Max registered capability bundles    |
| `EC_CONFIG_MAX_AGENT_ITERS`   | 8       | Max agentic loop iterations per turn |

---

## What Is Not in Scope (for now)

- **Streaming / SSE**: Deferred. Tool calls cannot be dispatched until the full
  response is received, so streaming offers no benefit to the agentic loop. It
  can be added later as a text-output optimization.
- **Multi-session / multi-user**: Single conversation, single I/O channel.
- **Tool output streaming back to LLM**: Tool results are always complete JSON
  strings, never partial.
- **History persistence across power cycles**: Currently in-RAM only. Flash/NVS
  persistence would require a serialize/deserialize step in `ec_session`.

---

## Validation Notes

- Fast regression coverage should continue to run through the mock-HTTP agent
  tests on POSIX.
- Socket-based I/O regressions should be covered with a POSIX Telnet smoke
  test so newline framing and Telnet command parsing are exercised against the
  real network path.
- FreeRTOS validation still requires target bring-up checks for networking,
  UART, Telnet, hardware safety policy, and one full agent turn against the
  configured provider.
