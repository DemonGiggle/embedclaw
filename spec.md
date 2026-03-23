EmbedClaw
---

Requirement
===

Just like OpenClaw, but it runs on the embedded system.

That means, we need to start from scratch to implement curl-like programs.
It should support OpenAI-like API protocol.

For simplicity, we starts from running on FreeRTOS.

Detailed Design
===

## Overview

EmbedClaw is a lightweight, embedded HTTP client library written in C, designed
to run on FreeRTOS. It provides a minimal curl-like API for making HTTP/HTTPS
requests to OpenAI-compatible API endpoints (e.g., `/v1/chat/completions`).

## Architecture

```
+---------------------+
|   Application Code  |   embedclaw_chat_completion(), etc.
+---------------------+
|   Chat API Layer    |   JSON request/response construction & parsing
+---------------------+
|   HTTP Client Layer |   HTTP/1.1 request/response engine
+---------------------+
|   TLS Layer         |   mbedTLS (optional, for HTTPS)
+---------------------+
|   TCP Socket Layer  |   FreeRTOS+TCP / lwIP abstraction
+---------------------+
```

## Components

### 1. TCP Socket Abstraction (`ec_socket.h` / `ec_socket.c`)

Wraps FreeRTOS+TCP socket API into a simple interface:

- `ec_socket_connect(host, port)` — resolve DNS, open TCP connection
- `ec_socket_send(sock, data, len)` — send bytes
- `ec_socket_recv(sock, buf, len, timeout_ms)` — receive bytes with timeout
- `ec_socket_close(sock)` — close connection

For the initial implementation, uses a POSIX socket shim so we can build and
test on a host machine. The FreeRTOS+TCP backend is a compile-time switch
(`EC_PLATFORM_FREERTOS` vs `EC_PLATFORM_POSIX`).

### 2. HTTP Client (`ec_http.h` / `ec_http.c`)

Minimal HTTP/1.1 client:

- Constructs `POST` requests with headers (`Host`, `Content-Type`,
  `Authorization`, `Content-Length`)
- Sends request over a socket
- Parses response status line and headers (chunked transfer-encoding supported)
- Reads response body into a caller-provided buffer
- Supports streaming via Server-Sent Events (SSE) with a line callback

Key types:

```c
typedef struct {
    const char *method;       /* "GET", "POST" */
    const char *host;
    uint16_t    port;
    const char *path;
    const char *headers;      /* extra headers, \r\n separated */
    const char *body;
    size_t      body_len;
} ec_http_request_t;

typedef struct {
    int         status_code;
    char       *body;
    size_t      body_len;
} ec_http_response_t;
```

### 3. JSON Builder / Parser (`ec_json.h` / `ec_json.c`)

Ultra-minimal JSON handling for embedded use (no malloc):

- **Builder**: Writes JSON tokens into a fixed-size buffer. Provides helpers
  like `ec_json_obj_start()`, `ec_json_add_string()`, `ec_json_array_start()`,
  etc.
- **Parser**: A lightweight pull-parser that finds values by key path (e.g.,
  `"choices[0].message.content"`). Does not build a DOM — scans the JSON
  string directly.

### 4. Chat Completion API Layer (`ec_api.h` / `ec_api.c`)

High-level API for any OpenAI-compatible endpoint:

```c
typedef struct {
    const char *base_url;    /* e.g. "api.openai.com" or any compatible host */
    uint16_t    port;        /* 443 */
    const char *api_key;
    int         use_tls;     /* 1 = HTTPS */
} ec_api_config_t;

typedef struct {
    const char *role;        /* "system", "user", "assistant" */
    const char *content;
} ec_api_message_t;

/* Blocking chat completion */
int ec_api_chat_completion(
    const ec_api_config_t *config,
    const char            *model,
    const ec_api_message_t *messages,
    size_t                 num_messages,
    char                  *out_buf,
    size_t                 out_buf_size
);
```

Returns 0 on success, negative error code on failure. The assistant's reply
is written to `out_buf` as a null-terminated string.

### 5. Main / Demo (`main.c`)

A FreeRTOS task (or POSIX main) that:
1. Configures the API endpoint
2. Sends a chat completion request
3. Prints the response

## Build System

CMake-based. Two build profiles:

- **Host (POSIX)**: `cmake -DEC_PLATFORM=POSIX ..` — builds a native
  executable for development/testing on Linux/macOS.
- **FreeRTOS**: `cmake -DEC_PLATFORM=FREERTOS -DFREERTOS_PATH=... ..` —
  cross-compiles for an embedded target.

## Constraints & Design Decisions

- **No dynamic allocation in hot paths**: All buffers are caller-provided or
  stack-allocated with configurable max sizes.
- **No external JSON library**: The JSON builder/parser is ~300 lines of C,
  purpose-built for the OpenAI message format.
- **TLS is optional**: The build can be configured with or without mbedTLS.
  For local/mock servers, plain HTTP works.
- **Portable**: The only platform-specific code is in `ec_socket.c`. Everything
  else is pure C99.
