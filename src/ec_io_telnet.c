#include "ec_io.h"
#include "ec_config.h"

#include <string.h>

/*
 * Skip a Telnet IAC command sequence starting at p.
 * Returns pointer past the sequence, or NULL if the sequence is incomplete.
 */
static const char *skip_iac(const char *p, const char *end)
{
    /* p points to 0xFF (IAC) */
    const char *start = p;
    p++; /* skip IAC */
    if (p >= end) return NULL;

    unsigned char cmd = (unsigned char)*p++;
    if (cmd == 0xFA) {
        /* SB ... IAC SE sub-negotiation */
        while (p + 1 < end) {
            if ((unsigned char)*p == 0xFF && (unsigned char)*(p + 1) == 0xF0) {
                p += 2;
                return p;
            }
            p++;
        }
        return NULL;
    } else if (cmd == 0xFB || cmd == 0xFC || cmd == 0xFD || cmd == 0xFE) {
        /* WILL/WONT/DO/DONT: one more option byte */
        if (p >= end) return NULL;
        p++;
    }

    return p > start ? p : NULL;
}

#if defined(EC_PLATFORM_POSIX)

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

static int s_listen_fd = -1;
static int s_client_fd = -1;
static char s_pending[EC_CONFIG_IO_LINE_BUF * 2];
static size_t s_pending_len = 0;

static int telnet_write(const char *str);

/*
 * Open the listening socket once. Idempotent.
 */
static int telnet_ensure_listen(void)
{
    if (s_listen_fd >= 0) return 0;

    s_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_listen_fd < 0) return -1;

    int opt = 1;
    setsockopt(s_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(EC_CONFIG_TELNET_PORT);

    if (bind(s_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s_listen_fd);
        s_listen_fd = -1;
        return -1;
    }
    if (listen(s_listen_fd, 1) < 0) {
        close(s_listen_fd);
        s_listen_fd = -1;
        return -1;
    }
    fprintf(stderr, "[embedclaw] Telnet listening on port %d\n",
            EC_CONFIG_TELNET_PORT);
    return 0;
}

/*
 * Accept a new client, blocking until one connects.
 */
static int telnet_accept(void)
{
    if (s_client_fd >= 0) {
        close(s_client_fd);
        s_client_fd = -1;
    }
    if (telnet_ensure_listen() < 0) return -1;

    s_client_fd = accept(s_listen_fd, NULL, NULL);
    if (s_client_fd < 0) return -1;
    s_pending_len = 0;

    fprintf(stderr, "[embedclaw] Telnet client connected\n");
    return 0;
}

static int telnet_read_line(char *buf, size_t size)
{
    if (!buf || size == 0) return -1;

    for (;;) {
        /* Accept a connection if we don't have one */
        if (s_client_fd < 0) {
            if (telnet_accept() < 0) return -1;
        }

        size_t len = 0;
        int truncated = 0;

        for (;;) {
            size_t consumed = 0;
            while (consumed < s_pending_len) {
                const char *p = s_pending + consumed;
                const char *end = s_pending + s_pending_len;
                unsigned char c = (unsigned char)*p;

                if (c == 0xFF) {
                    if (p + 1 >= end) break;
                    if ((unsigned char)*(p + 1) == 0xFF) {
                        consumed += 2;
                        if (len < size - 1) {
                            buf[len++] = (char)0xFF;
                        } else {
                            truncated = 1;
                        }
                        continue;
                    }

                    const char *next = skip_iac(p, end);
                    if (!next) break;
                    consumed += (size_t)(next - p);
                    continue;
                }

                consumed++;
                if (c == '\r') continue;

                if (c == '\n') {
                    memmove(s_pending, s_pending + consumed,
                            s_pending_len - consumed);
                    s_pending_len -= consumed;

                    if (truncated) {
                        static const char warning[] =
                            "\n[input truncated: line too long]\n";
                        (void)telnet_write(warning);
                        buf[0] = '\0';
                        return 0;
                    }

                    buf[len] = '\0';
                    return (int)len;
                }

                if (len < size - 1) {
                    buf[len++] = (char)c;
                } else {
                    truncated = 1;
                }
            }

            if (consumed > 0) {
                memmove(s_pending, s_pending + consumed, s_pending_len - consumed);
                s_pending_len -= consumed;
            }

            if (s_pending_len == sizeof(s_pending)) {
                fprintf(stderr, "[embedclaw] Telnet client disconnected (buffer overflow)\n");
                close(s_client_fd);
                s_client_fd = -1;
                s_pending_len = 0;
                break;
            }

            ssize_t n = recv(s_client_fd, s_pending + s_pending_len,
                             sizeof(s_pending) - s_pending_len, 0);
            if (n <= 0) {
                fprintf(stderr, "[embedclaw] Telnet client disconnected\n");
                close(s_client_fd);
                s_client_fd = -1;
                s_pending_len = 0;
                break;
            }
            s_pending_len += (size_t)n;
        }
    }
}

static int telnet_write(const char *str)
{
    if (s_client_fd < 0) return -1;
    size_t len = strlen(str);
    while (len > 0) {
        ssize_t n = send(s_client_fd, str, len, 0);
        if (n <= 0) {
            close(s_client_fd);
            s_client_fd = -1;
            return -1;
        }
        str += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

#elif defined(EC_PLATFORM_FREERTOS)

#include "FreeRTOS.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"

static Socket_t s_listen_fd = FREERTOS_INVALID_SOCKET;
static Socket_t s_client_fd = FREERTOS_INVALID_SOCKET;
static char s_pending[EC_CONFIG_IO_LINE_BUF * 2];
static size_t s_pending_len = 0;

static int telnet_write(const char *str);

static int telnet_ensure_listen(void)
{
    if (s_listen_fd != FREERTOS_INVALID_SOCKET) return 0;

    s_listen_fd = FreeRTOS_socket(FREERTOS_AF_INET,
                                  FREERTOS_SOCK_STREAM,
                                  FREERTOS_IPPROTO_TCP);
    if (s_listen_fd == FREERTOS_INVALID_SOCKET) return -1;

    struct freertos_sockaddr addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = FREERTOS_AF_INET;
    addr.sin_port = FreeRTOS_htons(EC_CONFIG_TELNET_PORT);
    addr.sin_addr = FreeRTOS_inet_addr_quick(0, 0, 0, 0);

    if (FreeRTOS_bind(s_listen_fd, &addr, sizeof(addr)) != 0) {
        FreeRTOS_closesocket(s_listen_fd);
        s_listen_fd = FREERTOS_INVALID_SOCKET;
        return -1;
    }
    if (FreeRTOS_listen(s_listen_fd, 1) != 0) {
        FreeRTOS_closesocket(s_listen_fd);
        s_listen_fd = FREERTOS_INVALID_SOCKET;
        return -1;
    }

    return 0;
}

static int telnet_accept(void)
{
    if (s_client_fd != FREERTOS_INVALID_SOCKET) {
        FreeRTOS_closesocket(s_client_fd);
        s_client_fd = FREERTOS_INVALID_SOCKET;
    }
    if (telnet_ensure_listen() < 0) return -1;

    struct freertos_sockaddr client_addr;
    socklen_t client_len = sizeof(client_addr);
    s_client_fd = FreeRTOS_accept(s_listen_fd, &client_addr, &client_len);
    s_pending_len = 0;
    return s_client_fd == FREERTOS_INVALID_SOCKET ? -1 : 0;
}

static int telnet_read_line(char *buf, size_t size)
{
    if (!buf || size == 0) return -1;

    for (;;) {
        if (s_client_fd == FREERTOS_INVALID_SOCKET) {
            if (telnet_accept() < 0) return -1;
        }

        size_t len = 0;
        int truncated = 0;

        for (;;) {
            size_t consumed = 0;
            while (consumed < s_pending_len) {
                const char *p = s_pending + consumed;
                const char *end = s_pending + s_pending_len;
                unsigned char c = (unsigned char)*p;

                if (c == 0xFF) {
                    if (p + 1 >= end) break;
                    if ((unsigned char)*(p + 1) == 0xFF) {
                        consumed += 2;
                        if (len < size - 1) {
                            buf[len++] = (char)0xFF;
                        } else {
                            truncated = 1;
                        }
                        continue;
                    }

                    const char *next = skip_iac(p, end);
                    if (!next) break;
                    consumed += (size_t)(next - p);
                    continue;
                }

                consumed++;
                if (c == '\r') continue;

                if (c == '\n') {
                    memmove(s_pending, s_pending + consumed,
                            s_pending_len - consumed);
                    s_pending_len -= consumed;

                    if (truncated) {
                        static const char warning[] =
                            "\n[input truncated: line too long]\n";
                        (void)telnet_write(warning);
                        buf[0] = '\0';
                        return 0;
                    }

                    buf[len] = '\0';
                    return (int)len;
                }

                if (len < size - 1) {
                    buf[len++] = (char)c;
                } else {
                    truncated = 1;
                }
            }

            if (consumed > 0) {
                memmove(s_pending, s_pending + consumed, s_pending_len - consumed);
                s_pending_len -= consumed;
            }

            if (s_pending_len == sizeof(s_pending)) {
                FreeRTOS_closesocket(s_client_fd);
                s_client_fd = FREERTOS_INVALID_SOCKET;
                s_pending_len = 0;
                break;
            }

            BaseType_t n = FreeRTOS_recv(s_client_fd, s_pending + s_pending_len,
                                         sizeof(s_pending) - s_pending_len, 0);
            if (n <= 0) {
                FreeRTOS_closesocket(s_client_fd);
                s_client_fd = FREERTOS_INVALID_SOCKET;
                s_pending_len = 0;
                break;
            }
            s_pending_len += (size_t)n;
        }
    }
}

static int telnet_write(const char *str)
{
    if (s_client_fd == FREERTOS_INVALID_SOCKET) return -1;
    size_t len = strlen(str);
    while (len > 0) {
        BaseType_t n = FreeRTOS_send(s_client_fd, str, len, 0);
        if (n <= 0) {
            FreeRTOS_closesocket(s_client_fd);
            s_client_fd = FREERTOS_INVALID_SOCKET;
            return -1;
        }
        str += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

#else
#error "Define EC_PLATFORM_POSIX or EC_PLATFORM_FREERTOS"
#endif

const ec_io_ops_t ec_io_telnet_ops = {
    .read_line = telnet_read_line,
    .write     = telnet_write,
};
