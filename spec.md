EmbedClaw
===

## Purpose

EmbedClaw is an embedded AI agent runtime for FreeRTOS. It runs on constrained
hardware and acts as an intelligent automation layer: it accepts user input over
a serial interface (UART, Telnet, or similar), forwards it to a remote
OpenAI-compatible LLM, and executes tool calls returned by the LLM — such as
reading and writing hardware registers — before returning the final answer to
the user.

It is the embedded counterpart to OpenClaw: same agentic loop, same tool-call
protocol, different execution environment.

---

## Design Principles

- **Single-threaded**: No RTOS threads or callbacks. The agent loop runs to
  completion in a single task. All I/O is blocking with timeouts.
- **No dynamic allocation in hot paths**: All buffers are caller-provided or
  statically declared. No `malloc` in the agent or tool layers.
- **No external dependencies beyond FreeRTOS+TCP**: JSON, HTTP, and the agent
  loop are all built in. mbedTLS is an optional add-on for TLS.
- **OpenAI-compatible protocol**: Tool calls use the standard OpenAI
  `tool_calls` JSON format. No custom protocol invented.
- **Extensible I/O and tool layers**: New input sources and new tools can be
  added without touching the agent core.
- **No streaming**: Responses are buffered in full before processing. Streaming
  (SSE) is deferred — tool calls cannot be dispatched until the full response
  is received anyway, so streaming adds complexity with no benefit to the
  agentic loop.

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
│  4. If tool_calls → dispatch to tool layer          │
│     append tool results → go to 2                  │
│  5. If text response → send to user via I/O layer   │
└──────────┬──────────────────────┬───────────────────┘
           │                      │
           ▼                      ▼
┌────────────────────┐  ┌─────────────────────────────┐
│  Chat API Layer    │  │  Tool Framework  (ec_tool)   │
│  (ec_api)          │  │  Tool registry + dispatcher  │
│  JSON build/parse  │  │  Built-in: HW register R/W   │
│  /v1/chat/complete │  │  Extensible: add new tools   │
└────────┬───────────┘  └─────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────────────────────┐
│  HTTP Client  (ec_http)                             │
│  HTTP/1.1, chunked transfer encoding                │
└────────────────────┬────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────┐
│  Socket Abstraction  (ec_socket)                    │
│  FreeRTOS+TCP backend  /  POSIX shim (host testing) │
└─────────────────────────────────────────────────────┘
```

---

## Components

### 1. Socket Abstraction (`ec_socket.h` / `ec_socket.c`)

Wraps the platform TCP API into four functions:

```c
ec_socket_t *ec_socket_connect(const char *host, uint16_t port);
int          ec_socket_send(ec_socket_t *s, const char *data, size_t len);
int          ec_socket_recv(ec_socket_t *s, char *buf, size_t len, int timeout_ms);
void         ec_socket_close(ec_socket_t *s);
```

Two backends selected at compile time via `EC_PLATFORM`:
- `POSIX` — standard BSD sockets, used for host-side development and testing.
- `FREERTOS` — FreeRTOS+TCP sockets (currently stubbed; implementation pending).

### 2. HTTP Client (`ec_http.h` / `ec_http.c`)

Minimal HTTP/1.1 client. Supports:
- `POST` with arbitrary headers and body
- Response status line and header parsing
- Chunked transfer-encoding (in-place decode)
- Caller-provided request and response buffers (no malloc)

Non-streaming only. The full response body is buffered before returning.

### 3. JSON Builder / Parser (`ec_json.h` / `ec_json.c`)

Purpose-built for the OpenAI message format. No malloc, no DOM.

- **Builder** (`ec_json_writer_t`): Writes JSON tokens into a fixed-size buffer.
  Helpers: `ec_json_obj_start/end`, `ec_json_array_start/end`,
  `ec_json_add_string`, `ec_json_add_int`, etc.
- **Parser**: Pull-parser that finds values by key path (e.g.,
  `"choices[0].message.content"`, `"choices[0].message.tool_calls[0].id"`).
  Scans the raw JSON string without constructing any in-memory tree.

The parser must be extended to handle the full `tool_calls` array structure
(function name, arguments object) in addition to the existing string-path
extraction.

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
    char                content[EC_CONFIG_RESPONSE_BUF]; /* if MESSAGE */
    ec_api_tool_call_t  tool_calls[EC_CONFIG_MAX_TOOL_CALLS]; /* if TOOL_CALLS */
    int                 num_tool_calls;
} ec_api_response_t;

int ec_api_chat_completion(
    const ec_api_config_t  *config,
    const char             *model,
    const ec_api_message_t *messages,
    size_t                  num_messages,
    const ec_api_tool_def_t *tools,   /* tool schema sent to LLM */
    size_t                  num_tools,
    ec_api_response_t      *out
);
```

### 5. Tool Framework (`ec_tool.h` / `ec_tool.c`)

Provides a registry of named tools and a dispatcher. Each tool is a C function
that receives a JSON arguments string and writes a JSON result string.

```c
typedef int (*ec_tool_fn_t)(
    const char *args_json,   /* raw JSON arguments from LLM */
    char       *out_json,    /* result JSON to send back */
    size_t      out_size
);

typedef struct {
    const char    *name;
    const char    *description;
    const char    *parameters_schema; /* JSON Schema string, sent to LLM */
    ec_tool_fn_t   fn;
} ec_tool_def_t;

/* Register a tool at startup */
int ec_tool_register(const ec_tool_def_t *def);

/* Dispatch one tool_call from the LLM */
int ec_tool_dispatch(const ec_api_tool_call_t *call,
                     char *out_json, size_t out_size);

/* Return the tool definition table (for passing to ec_api) */
const ec_tool_def_t *ec_tool_table(size_t *count);
```

**Built-in tools (initial set):**

- `hw_register_read` — reads a hardware register at a given address.
  Arguments: `{ "address": <uint32 hex string> }`
  Result: `{ "value": <uint32 hex string> }`

- `hw_register_write` — writes a value to a hardware register.
  Arguments: `{ "address": <uint32 hex string>, "value": <uint32 hex string> }`
  Result: `{ "ok": true }` or error

### 6. Session Layer (`ec_session.h` / `ec_session.c`)

Maintains conversation history across user turns. The history is a fixed-size
ring of `ec_api_message_t` entries (role + content). When full, oldest entries
are evicted.

Roles in the history: `system`, `user`, `assistant`, `tool`.

The session is persistent for the lifetime of the device (not cleared on
UART/Telnet reconnect). A user command (e.g., `/reset`) can clear it explicitly.

```c
void ec_session_init(ec_session_t *s, const char *system_prompt);
void ec_session_append(ec_session_t *s, const char *role, const char *content);
void ec_session_append_tool_result(ec_session_t *s,
                                   const char *tool_call_id,
                                   const char *content);
void ec_session_reset(ec_session_t *s);

/* Get the message array for passing to ec_api */
const ec_api_message_t *ec_session_messages(const ec_session_t *s, size_t *count);
```

### 7. Agent Loop (`ec_agent.h` / `ec_agent.c`)

The core agentic loop. Single-threaded, blocking.

```
ec_agent_run_turn(session, user_input):
  1. session_append(user, user_input)
  2. loop:
       resp = ec_api_chat_completion(session.messages, tools)
       if resp.type == MESSAGE:
           session_append(assistant, resp.content)
           io_write(resp.content)
           break
       if resp.type == TOOL_CALLS:
           session_append(assistant, tool_calls_json)
           for each call in resp.tool_calls:
               result = ec_tool_dispatch(call)
               session_append_tool_result(call.id, result)
           // loop back: send updated history to LLM
```

A configurable iteration limit (`EC_CONFIG_MAX_AGENT_ITERATIONS`) prevents
infinite loops.

### 8. I/O Abstraction (`ec_io.h` / `ec_io.c`)

Decouples the agent from the physical input/output channel. New transports
(UART, Telnet, USB CDC, BLE UART) implement the same interface:

```c
typedef struct {
    /* Read a line of user input into buf (blocking, null-terminated) */
    int (*read_line)(char *buf, size_t size);
    /* Write a null-terminated string to the user */
    int (*write)(const char *str);
} ec_io_ops_t;

void ec_io_init(const ec_io_ops_t *ops);
int  ec_io_read_line(char *buf, size_t size);
int  ec_io_write(const char *str);
```

Initial implementations:
- **UART** (`ec_io_uart.c`): wraps FreeRTOS UART HAL (or POSIX stdin/stdout on host).
- **Telnet** (`ec_io_telnet.c`): wraps a raw TCP connection on a fixed port.

---

## Build System

CMake-based. Two build profiles:

```
cmake -DEC_PLATFORM=POSIX ..        # host build for development/testing
cmake -DEC_PLATFORM=FREERTOS \
      -DFREERTOS_PATH=... ..        # cross-compile for embedded target
```

Output: static library `libembedclaw.a` + optional demo executable.

---

## Buffer Sizing (configurable in `ec_config.h`)

| Constant                      | Default | Purpose                              |
|-------------------------------|---------|--------------------------------------|
| `EC_CONFIG_REQUEST_BUF`       | 4096    | HTTP request body (outgoing JSON)    |
| `EC_CONFIG_RESPONSE_BUF`      | 8192    | HTTP response body (incoming JSON)   |
| `EC_CONFIG_IO_LINE_BUF`       | 256     | One line of user input               |
| `EC_CONFIG_TOOL_ARG_BUF`      | 512     | Tool call arguments JSON             |
| `EC_CONFIG_MAX_TOOL_CALLS`    | 4       | Max tool_calls in one LLM response   |
| `EC_CONFIG_MAX_HISTORY`       | 32      | Max messages in conversation history |
| `EC_CONFIG_MAX_TOOLS`         | 16      | Max registered tools                 |
| `EC_CONFIG_MAX_AGENT_ITERS`   | 8       | Max agentic loop iterations per turn |

---

## What Is Not in Scope (for now)

- **Streaming / SSE**: Deferred. Tool calls cannot be dispatched until the full
  response is received, so streaming offers no benefit to the agentic loop. It
  can be added later as a text-output optimization.
- **TLS / HTTPS**: The `use_tls` field exists in `ec_api_config_t` but is not
  wired up. mbedTLS integration is a future task.
- **Multi-session / multi-user**: Single conversation, single I/O channel.
- **Tool output streaming back to LLM**: Tool results are always complete JSON
  strings, never partial.
