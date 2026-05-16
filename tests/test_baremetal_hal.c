#include "test_runner.h"
#include "ec_io.h"
#include "ec_socket.h"

#include <stdint.h>
#include <string.h>

typedef struct {
    const char *input;
    size_t input_pos;
    char output[128];
    size_t output_len;
} fake_uart_t;

static int fake_uart_read(void *buf, size_t len, fake_uart_t *uart)
{
    (void)len;
    if (!uart->input[uart->input_pos]) return 0;
    *(char *)buf = uart->input[uart->input_pos++];
    return 1;
}

static int fake_uart_write(const void *buf, size_t len, fake_uart_t *uart)
{
    if (uart->output_len + len >= sizeof(uart->output)) return -1;
    memcpy(uart->output + uart->output_len, buf, len);
    uart->output_len += len;
    uart->output[uart->output_len] = '\0';
    return (int)len;
}

static fake_uart_t *s_uart_ctx;

static int uart_read_wrapper(void *buf, size_t len, uint32_t timeout_ms)
{
    (void)timeout_ms;
    return fake_uart_read(buf, len, s_uart_ctx);
}

static int uart_write_wrapper(const void *buf, size_t len, uint32_t timeout_ms)
{
    (void)timeout_ms;
    return fake_uart_write(buf, len, s_uart_ctx);
}

typedef struct {
    int connect_calls;
    int send_calls;
    int recv_calls;
    int close_calls;
    const char *host;
    uint16_t port;
    int use_tls;
    char sent[64];
    const char *recv_data;
} fake_socket_t;

static int fake_connect(void *ctx, const char *host, uint16_t port, int use_tls)
{
    fake_socket_t *sock = (fake_socket_t *)ctx;
    sock->connect_calls++;
    sock->host = host;
    sock->port = port;
    sock->use_tls = use_tls;
    return 0;
}

static int fake_send(void *ctx, const void *data, size_t len)
{
    fake_socket_t *sock = (fake_socket_t *)ctx;
    sock->send_calls++;
    if (len >= sizeof(sock->sent)) return -1;
    memcpy(sock->sent, data, len);
    sock->sent[len] = '\0';
    return (int)len;
}

static int fake_recv(void *ctx, void *buf, size_t len, uint32_t timeout_ms)
{
    fake_socket_t *sock = (fake_socket_t *)ctx;
    (void)timeout_ms;
    sock->recv_calls++;
    size_t n = strlen(sock->recv_data);
    if (n > len) n = len;
    memcpy(buf, sock->recv_data, n);
    return (int)n;
}

static void fake_close(void *ctx)
{
    fake_socket_t *sock = (fake_socket_t *)ctx;
    sock->close_calls++;
}

static int test_baremetal_uart_reads_lines_and_writes(void)
{
    fake_uart_t uart;
    memset(&uart, 0, sizeof(uart));
    uart.input = "hello\r\n";
    s_uart_ctx = &uart;

    ec_io_uart_hal_t hal = {
        .read = uart_read_wrapper,
        .write = uart_write_wrapper,
    };
    ec_io_uart_set_hal(&hal);
    ec_io_init(&ec_io_uart_ops);

    char line[16];
    ASSERT_EQ(ec_io_read_line(line, sizeof(line)), 5,
              "bare-metal UART should read a line");
    ASSERT_EQ(strcmp(line, "hello"), 0, "bare-metal UART should strip CRLF");
    ASSERT_EQ(ec_io_write("ok"), 0, "bare-metal UART should write");
    ASSERT_EQ(strcmp(uart.output, "ok"), 0, "write should reach HAL");
    return 1;
}

static int test_baremetal_uart_truncates_long_lines(void)
{
    fake_uart_t uart;
    memset(&uart, 0, sizeof(uart));
    uart.input = "abcdef\n";
    s_uart_ctx = &uart;

    ec_io_uart_hal_t hal = {
        .read = uart_read_wrapper,
        .write = uart_write_wrapper,
    };
    ec_io_uart_set_hal(&hal);
    ec_io_init(&ec_io_uart_ops);

    char line[4];
    ASSERT_EQ(ec_io_read_line(line, sizeof(line)), 0,
              "truncated line should return an empty command");
    ASSERT_EQ(line[0], '\0', "truncated line should clear output buffer");
    ASSERT_STR(uart.output, "input truncated",
               "truncation warning should be written through HAL");
    return 1;
}

static int test_baremetal_socket_delegates_to_hal(void)
{
    fake_socket_t fake;
    memset(&fake, 0, sizeof(fake));
    fake.recv_data = "HTTP/1.1 200 OK\r\n\r\n{}";

    ec_socket_baremetal_hal_t hal = {
        .ctx = &fake,
        .connect = fake_connect,
        .send = fake_send,
        .recv = fake_recv,
        .close = fake_close,
    };
    ec_socket_baremetal_set_hal(&hal);

    ec_socket_t *sock = ec_socket_connect("api.example.test", 443, 1);
    ASSERT(sock != 0, "connect should return a socket handle");
    ASSERT_EQ(fake.connect_calls, 1, "connect callback should run");
    ASSERT_EQ(strcmp(fake.host, "api.example.test"), 0, "host should be passed through");
    ASSERT_EQ(fake.port, 443, "port should be passed through");
    ASSERT_EQ(fake.use_tls, 1, "TLS flag should be passed through");

    ASSERT(ec_socket_connect("second.example.test", 80, 0) == 0,
           "single static bare-metal socket should reject concurrent connect");

    ASSERT_EQ(ec_socket_send(sock, "ping", 4), 4, "send should delegate to HAL");
    ASSERT_EQ(strcmp(fake.sent, "ping"), 0, "send payload should reach HAL");

    char buf[32];
    int n = ec_socket_recv(sock, buf, sizeof(buf), 100);
    ASSERT(n > 0, "recv should delegate to HAL");
    buf[n] = '\0';
    ASSERT_STR(buf, "HTTP/1.1 200 OK", "recv payload should come from HAL");

    ec_socket_close(sock);
    ASSERT_EQ(fake.close_calls, 1, "close callback should run");
    ASSERT(ec_socket_connect("after-close.example.test", 80, 0) != 0,
           "connect should work again after close");
    ec_socket_close(sock);
    return 1;
}

int main(void)
{
    printf("=== EmbedClaw Bare-metal HAL Tests ===\n\n");

    RUN_TEST(test_baremetal_uart_reads_lines_and_writes);
    RUN_TEST(test_baremetal_uart_truncates_long_lines);
    RUN_TEST(test_baremetal_socket_delegates_to_hal);

    PRINT_RESULTS();
    return (_tests_pass == _tests_run) ? 0 : 1;
}
