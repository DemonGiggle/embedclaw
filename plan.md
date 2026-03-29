EmbedClaw — Implementation Plan
===

## Current State

All core components are implemented and functional on the POSIX platform.
The FreeRTOS backend remains stubbed, pending target hardware bring-up.

### Completed Components

| Component           | File(s)                        | Status                              |
|---------------------|--------------------------------|-------------------------------------|
| Socket layer        | `ec_socket.c/h`                | POSIX done (TCP + TLS); FreeRTOS stubbed |
| TLS (mbedTLS)       | `ec_socket.c`, `ec_cacerts.h`  | Done — embedded CA bundle, no filesystem |
| HTTP client         | `ec_http.c/h`                  | Done (chunked encoding, TLS pass-through) |
| JSON builder/parser | `ec_json.c/h`                  | Done (writer + path-based parser)   |
| Chat API            | `ec_api.c/h`                   | Done (text + tool_calls responses)  |
| Tool framework      | `ec_tool.c/h`                  | Done (registry + dispatcher)        |
| Skill system        | `ec_skill.c/h`, `ec_skill_table.c` | Done (compile-time skill bundles) |
| HW register skill   | `ec_skill_table.c`             | Done (read/write, POSIX mock array) |
| Web browsing skill  | `ec_skill_table.c`             | Done (web_search via Brave, web_fetch) |
| Session layer       | `ec_session.c/h`               | Done (ring buffer, tool_call support) |
| Agent loop          | `ec_agent.c/h`                 | Done (multi-iteration tool dispatch) |
| I/O abstraction     | `ec_io.c/h`                    | Done                                |
| UART I/O backend    | `ec_io_uart.c`                 | Done (stdin/stdout on POSIX)        |
| Telnet I/O backend  | `ec_io_telnet.c`               | Done (TCP server on configurable port) |
| Debug logging       | `ec_log.c/h`                   | Done (EC_DEBUG=1 on POSIX, compile-time on FreeRTOS) |
| E2E test suite      | `tests/test_e2e.c`             | 10 tests passing (mock HTTP layer)  |
| Demo application    | `main.c`                       | POSIX CLI demo with env config      |

### Remaining Work

| Component              | Files                        | Notes                               |
|------------------------|------------------------------|-------------------------------------|
| FreeRTOS sockets       | `ec_socket.c` (FREERTOS)     | Replace stubs with FreeRTOS+TCP     |
| FreeRTOS UART I/O      | `ec_io_uart.c` (FREERTOS)    | Wire to HAL UART driver             |
| FreeRTOS Telnet I/O    | `ec_io_telnet.c` (FREERTOS)  | Wire to FreeRTOS+TCP server socket  |
| FreeRTOS TLS           | `ec_socket.c` (FREERTOS)     | Socket layer is TLS-aware; needs BIO callbacks |
| Flash/NVS persistence  | `ec_session.c`               | Serialize/deserialize history       |
| HW register allowlist  | `ec_tool.c`                  | Address range validation            |
| Minimal mbedTLS config | `ec_mbedtls_config.h`        | Reduce binary size for embedded     |

---

## Implementation Phases

### Phase 1 — Extend Chat API for tool_calls ✅

**Completed.** `ec_api_chat_completion` returns either a text message or a
parsed list of tool calls. Request JSON includes the `tools` array with
function schemas.

---

### Phase 2 — Tool Framework ✅

**Completed.** Registry + dispatcher in `ec_tool.c`. Built-in `hw_register_read`
and `hw_register_write` tools. POSIX mock register array at `0x40000000`.

---

### Phase 3 — Session Layer ✅

**Completed.** Fixed-size message ring in `ec_session.c`. Supports all OpenAI
roles (`system`, `user`, `assistant`, `tool`). Tool result messages include
`tool_call_id`. Oldest non-system messages evicted when full.

---

### Phase 4 — Agent Loop ✅

**Completed.** Full agentic loop in `ec_agent.c`. Multi-iteration tool dispatch
with configurable `EC_CONFIG_MAX_AGENT_ITERS` limit. Error handling for API
failures, session overflow, and iteration limits.

---

### Phase 5 — I/O Abstraction + Backends ✅

**Completed.** UART backend (stdin/stdout on POSIX) and Telnet backend (TCP
server). I/O mode selected via `EC_IO` environment variable on POSIX.

---

### Phase 6 — Skill System ✅

**Completed.** Compile-time skill bundles in `ec_skill_table.c`. Each skill
registers tools and contributes system prompt context. Two built-in skills:
`hw_register_control` and `web_browsing`.

---

### Phase 7 — TLS / HTTPS ✅

**Completed.** mbedTLS v3.6.5 integrated as git submodule (`third_party/mbedtls`).
Transparent TLS in `ec_socket.c` via custom BIO callbacks on the raw fd.
Embedded CA bundle in `ec_cacerts.h` (no filesystem dependency). SNI hostname
verification. Certificate validation set to `MBEDTLS_SSL_VERIFY_REQUIRED`.

---

### Phase 8 — Web Browsing Tools ✅

**Completed.** `web_search` tool calls the Brave Search API
(`GET /res/v1/web/search?q=...&count=N`). `web_fetch` tool performs HTTP GET on
arbitrary URLs. Both registered via the `web_browsing` skill with appropriate
system context for the LLM.

---

### Phase 9 — Debug Logging ✅

**Completed.** `ec_log.h` provides `EC_LOG_DEBUG()` and `EC_LOG_BODY()` macros.
On POSIX, enabled at runtime via `EC_DEBUG=1`. On FreeRTOS, enabled at compile
time via `EC_CONFIG_DEBUG_LOG=1`. Traces full LLM request/response JSON, tool
dispatches, and agent loop iterations. All output goes to stderr.

---

### Phase 10 — FreeRTOS Socket Backend (TODO)

**Goal**: Replace POSIX socket stubs with real FreeRTOS+TCP calls.

Tasks:
1. Implement `ec_socket_connect` using `FreeRTOS_socket()`,
   `FreeRTOS_getaddrinfo()`, `FreeRTOS_connect()`.
2. Implement `ec_socket_send` / `ec_socket_recv` / `ec_socket_close`.
3. Set socket receive timeout via `FreeRTOS_setsockopt`.
4. Wire TLS BIO callbacks to FreeRTOS send/recv (socket layer already has
   mbedTLS context fields in the FreeRTOS struct).
5. Implement FreeRTOS UART I/O backend for `ec_io`.
6. Implement FreeRTOS Telnet I/O backend for `ec_io`.
7. Validate full end-to-end on target hardware.

**Deliverable**: EmbedClaw runs fully on FreeRTOS hardware with TLS.

---

### Phase 11 — Production Hardening (TODO)

Tasks:
1. **Hardware register address allowlist**: Restrict valid address ranges in
   the `hw_register_read`/`hw_register_write` tools. The LLM controls the
   address argument — unrestricted access is a security risk.
2. **Minimal mbedTLS config**: Replace default config with a client-only
   subset to reduce binary size. Required modules documented in
   `include/ec_mbedtls_config.h`.
3. **Flash/NVS persistence**: Serialize conversation history to non-volatile
   storage so sessions survive power cycles.

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
| TLS library | mbedTLS v3.6.5 (default config) | Widely used on embedded; custom minimal config deferred |
| CA certificates | Embedded bundle (no filesystem) | FreeRTOS/baremetal has no `/etc/ssl/certs` |
| Web search API | Brave Search | Free tier available; simple REST API |
| Debug logging | stderr, runtime toggle (POSIX) | Doesn't interfere with agent I/O on stdout |
| Test isolation | Mock HTTP at link time | Tests run offline with deterministic responses |

---

## Open Questions

- **History persistence across power cycles**: Currently in-RAM only. If
  flash/NVS persistence is needed, `ec_session` needs a serialize/deserialize
  step. What storage API to target on FreeRTOS?
- **Tool security**: HW register read/write is a privileged operation. What
  address ranges should be in the allowlist for the target platform?
- **Minimal mbedTLS config**: The default config works but is large. The
  required modules are documented in `ec_mbedtls_config.h`, but mbedTLS v3.6
  `check_config.h` has complex internal macro dependencies that make custom
  configs fragile. May need to wait for upstream improvements or carefully
  test each combination.
