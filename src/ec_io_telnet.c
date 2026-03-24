#include "ec_io.h"
#include "ec_config.h"

#include <string.h>

#if defined(EC_PLATFORM_POSIX)

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

static int s_listen_fd = -1;
static int s_client_fd = -1;

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

    fprintf(stderr, "[embedclaw] Telnet client connected\n");
    return 0;
}

/*
 * Skip a Telnet IAC command sequence starting at p.
 * Returns pointer past the sequence, or p+1 on unrecognised byte.
 */
static const char *skip_iac(const char *p, const char *end)
{
    /* p points to 0xFF (IAC) */
    p++; /* skip IAC */
    if (p >= end) return p;

    unsigned char cmd = (unsigned char)*p++;
    if (cmd == 0xFA) {
        /* SB ... IAC SE sub-negotiation */
        while (p + 1 < end) {
            if ((unsigned char)*p == 0xFF && (unsigned char)*(p+1) == 0xF0) {
                p += 2;
                break;
            }
            p++;
        }
    }
    /* WILL/WONT/DO/DONT: one more option byte */
    else if (cmd == 0xFB || cmd == 0xFC || cmd == 0xFD || cmd == 0xFE) {
        if (p < end) p++;
    }
    /* Other 2-byte commands: nothing extra to skip */
    return p;
}

static int telnet_read_line(char *buf, size_t size)
{
    for (;;) {
        /* Accept a connection if we don't have one */
        if (s_client_fd < 0) {
            if (telnet_accept() < 0) return -1;
        }

        /* Read characters one-by-one until newline */
        size_t len = 0;
        char raw[EC_CONFIG_IO_LINE_BUF * 2]; /* extra room for IAC sequences */
        size_t raw_len = 0;

        /* Read a chunk */
        ssize_t n = recv(s_client_fd, raw, sizeof(raw) - 1, 0);
        if (n <= 0) {
            /* Client disconnected — wait for next connection */
            fprintf(stderr, "[embedclaw] Telnet client disconnected\n");
            close(s_client_fd);
            s_client_fd = -1;
            continue;
        }
        raw_len = (size_t)n;

        /* Strip Telnet IAC sequences and CR, copy printable chars */
        const char *p   = raw;
        const char *end = raw + raw_len;
        while (p < end && len < size - 1) {
            unsigned char c = (unsigned char)*p;
            if (c == 0xFF) {
                p = skip_iac(p, end);
                continue;
            }
            if (c == '\r') { p++; continue; } /* skip CR in CR LF */
            if (c == '\n') { p++; break; }    /* end of line */
            buf[len++] = (char)c;
            p++;
        }
        buf[len] = '\0';
        return (int)len;
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

/*
 * FreeRTOS Telnet stub — replace with FreeRTOS+TCP server socket calls.
 */

static int telnet_read_line(char *buf, size_t size)
{
    (void)buf; (void)size;
    /* TODO: implement with FreeRTOS_accept() / FreeRTOS_recv() */
    return -1;
}

static int telnet_write(const char *str)
{
    (void)str;
    /* TODO: implement with FreeRTOS_send() */
    return -1;
}

#else
#error "Define EC_PLATFORM_POSIX or EC_PLATFORM_FREERTOS"
#endif

const ec_io_ops_t ec_io_telnet_ops = {
    .read_line = telnet_read_line,
    .write     = telnet_write,
};
