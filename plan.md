EmbedClaw — Implementation Plan
===

## Current State

The following components exist and are functional on the POSIX platform:

| Component        | File(s)                    | Status                              |
|------------------|----------------------------|-------------------------------------|
| Socket layer     | `ec_socket.c/h`            | POSIX done; FreeRTOS stubbed        |
| HTTP client      | `ec_http.c/h`              | Done (chunked encoding supported)   |
| JSON builder     | `ec_json.c/h`              | Done                                |
| JSON parser      | `ec_json.c/h`              | Done for string paths; needs tool_calls extension |
| Chat API         | `ec_api.c/h`               | Single-turn, text response only     |
| Demo             | `main.c`                   | POSIX CLI demo                      |

The following components are **absent** and must be built:

| Component         | Files (planned)            | Notes                               |
|-------------------|----------------------------|-------------------------------------|
| Tool framework    | `ec_tool.c/h`              | Registry + dispatcher + HW tools    |
| Session layer     | `ec_session.c/h`           | Conversation history management     |
| Agent loop        | `ec_agent.c/h`             | Agentic loop with tool dispatch     |
| I/O abstraction   | `ec_io.c/h`                | Transport-agnostic input/output     |
| UART I/O backend  | `ec_io_uart.c`             | UART or stdin/stdout (POSIX)        |
| Telnet I/O backend| `ec_io_telnet.c`           | TCP server on fixed port            |
| FreeRTOS sockets  | `ec_socket.c` (FREERTOS)   | Replace stubs with FreeRTOS+TCP     |

---

## Implementation Phases

### Phase 1 — Extend Chat API for tool_calls

**Goal**: `ec_api_chat_completion` can return either a text message or a parsed
list of tool calls.

Tasks:
1. Define `ec_api_tool_call_t`, `ec_api_response_t`, `ec_api_tool_def_t` in
   `ec_api.h`.
2. Extend `ec_json` parser to extract `tool_calls[N].id`, `.function.name`,
   `.function.arguments` from the response JSON.
3. Extend `ec_api.c` to serialize the `tools` array into the request JSON
   (name, description, parameters schema).
4. Update `ec_api_chat_completion` signature to accept tool definitions and
   return `ec_api_response_t` instead of a plain string.
5. Test on POSIX with a mock server or live endpoint.

**Deliverable**: `ec_api` can round-trip a tool-calling exchange with the LLM.

---

### Phase 2 — Tool Framework

**Goal**: A registry of named tools with a JSON-in / JSON-out dispatch interface.
Built-in hardware register read/write tools.

Tasks:
1. Define `ec_tool_def_t`, `ec_tool_fn_t` in `ec_tool.h`.
2. Implement `ec_tool_register`, `ec_tool_dispatch`, `ec_tool_table` in
   `ec_tool.c`.
3. Implement `hw_register_read` tool:
   - Parse `address` from arguments JSON (hex string → uint32).
   - Perform `*(volatile uint32_t *)address` read.
   - Return value as hex string in result JSON.
   - On POSIX: simulate with a static mock register map for testing.
4. Implement `hw_register_write` tool similarly.
5. Write JSON Schema strings for both tools (sent to LLM so it knows how to
   call them).

**Deliverable**: Tools can be registered and dispatched; HW tools work on both
POSIX (mock) and FreeRTOS (real registers).

---

### Phase 3 — Session Layer

**Goal**: Persistent conversation history across turns.

Tasks:
1. Define `ec_session_t` (fixed-size message ring) in `ec_session.h`.
2. Implement `ec_session_init`, `ec_session_append`, `ec_session_append_tool_result`,
   `ec_session_reset`, `ec_session_messages`.
3. Handle ring eviction (drop oldest non-system message when full).
4. Ensure `tool` role messages include the `tool_call_id` field (required by
   OpenAI API).

**Deliverable**: Multi-turn conversation with correct history formatting.

---

### Phase 4 — Agent Loop

**Goal**: Full agentic loop — user input → LLM → tool dispatch (repeat) →
final answer.

Tasks:
1. Define `ec_agent_t`, `ec_agent_config_t` in `ec_agent.h`.
2. Implement `ec_agent_run_turn`:
   - Append user input to session.
   - Loop: call `ec_api_chat_completion`, check response type.
   - On `TOOL_CALLS`: dispatch each tool, append results, loop.
   - On `MESSAGE`: append to session, write to I/O, return.
   - Enforce `EC_CONFIG_MAX_AGENT_ITERS` limit.
3. Handle error cases: LLM error, tool dispatch error, iteration limit hit.

**Deliverable**: End-to-end agentic exchange works on POSIX.

---

### Phase 5 — I/O Abstraction + Backends

**Goal**: Decouple the agent from the physical channel.

Tasks:
1. Define `ec_io_ops_t`, `ec_io_init`, `ec_io_read_line`, `ec_io_write` in
   `ec_io.h`.
2. Implement POSIX backend (`ec_io_uart.c` using stdin/stdout) for host testing.
3. Implement Telnet backend (`ec_io_telnet.c`):
   - Open a TCP server socket on a configurable port.
   - Accept one connection (single-session).
   - `read_line` blocks until `\n` or connection closed.
   - `write` sends bytes over the connection.
   - On disconnect, keep session history (persistent), wait for next connect.
4. Update `main.c` to wire I/O backend → agent loop.

**Deliverable**: User can connect via Telnet and have an agentic conversation.

---

### Phase 6 — FreeRTOS Socket Backend

**Goal**: Replace POSIX socket stubs with real FreeRTOS+TCP calls.

Tasks:
1. Implement `ec_socket_connect` using `FreeRTOS_socket()`,
   `FreeRTOS_getaddrinfo()`, `FreeRTOS_connect()`.
2. Implement `ec_socket_send` / `ec_socket_recv` / `ec_socket_close`.
3. Set socket receive timeout via `FreeRTOS_setsockopt`.
4. Implement FreeRTOS UART I/O backend for `ec_io`.
5. Validate full end-to-end on target hardware.

**Deliverable**: EmbedClaw runs fully on FreeRTOS hardware.

---

### Phase 7 — TLS (Future)

- Integrate mbedTLS or wolfSSL as an optional layer between `ec_http` and
  `ec_socket`.
- Wire `use_tls` flag in `ec_api_config_t`.
- Required for connecting to public LLM APIs (OpenAI, Anthropic, etc.) in
  production.

---

## Key Design Decisions (Recorded)

| Decision | Choice | Reason |
|----------|--------|--------|
| Threading model | Single-threaded, blocking | FreeRTOS target has no threading requirement; simplifies all state management |
| Streaming / SSE | Deferred | tool_calls require full response anyway; adds complexity with no agentic benefit |
| Tool call format | OpenAI `tool_calls` JSON | Standard format; no custom protocol invented |
| Conversation scope | Persistent across sessions | User expects context to survive reconnect |
| Memory model | Fixed buffers, no malloc in hot paths | Embedded constraint; predictable memory usage |
| JSON handling | Built-in, no external library | Avoids porting overhead; format is narrow and known |

---

## Open Questions

- **TLS priority**: When does production deployment on a real LLM endpoint
  become a requirement? That determines when Phase 7 must happen.
- **History persistence across power cycles**: Currently in-RAM only. If
  flash/NVS persistence is needed, `ec_session` needs a serialize/deserialize
  step.
- **Tool security**: HW register read/write is a privileged operation. Consider
  whether an address allowlist or range check is needed before
  `*(volatile uint32_t *)addr`.
