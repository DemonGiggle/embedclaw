#include "ec_socket.h"

#include <stdlib.h>
#include <string.h>

#if defined(EC_PLATFORM_POSIX)

#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <sys/time.h>

struct ec_socket {
    int fd;
};

ec_socket_t *ec_socket_connect(const char *host, uint16_t port)
{
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || res == NULL) {
        return NULL;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        return NULL;
    }

    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        close(fd);
        freeaddrinfo(res);
        return NULL;
    }

    freeaddrinfo(res);

    ec_socket_t *sock = (ec_socket_t *)malloc(sizeof(ec_socket_t));
    if (!sock) {
        close(fd);
        return NULL;
    }
    sock->fd = fd;
    return sock;
}

int ec_socket_send(ec_socket_t *sock, const void *data, size_t len)
{
    if (!sock) return -1;

    size_t total = 0;
    const char *p = (const char *)data;

    while (total < len) {
        ssize_t n = send(sock->fd, p + total, len - total, 0);
        if (n <= 0) {
            return -1;
        }
        total += (size_t)n;
    }
    return (int)total;
}

int ec_socket_recv(ec_socket_t *sock, void *buf, size_t len, uint32_t timeout_ms)
{
    if (!sock) return -1;

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ssize_t n = recv(sock->fd, buf, len, 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0; /* timeout */
        }
        return -1;
    }
    return (int)n;
}

void ec_socket_close(ec_socket_t *sock)
{
    if (!sock) return;
    close(sock->fd);
    free(sock);
}

#elif defined(EC_PLATFORM_FREERTOS)

/*
 * FreeRTOS+TCP stub — to be filled in when porting to a real target.
 * Requires FreeRTOS_IP.h, FreeRTOS_Sockets.h.
 */

struct ec_socket {
    int placeholder;
};

ec_socket_t *ec_socket_connect(const char *host, uint16_t port)
{
    (void)host; (void)port;
    /* TODO: implement with FreeRTOS_socket(), FreeRTOS_connect() */
    return NULL;
}

int ec_socket_send(ec_socket_t *sock, const void *data, size_t len)
{
    (void)sock; (void)data; (void)len;
    return -1;
}

int ec_socket_recv(ec_socket_t *sock, void *buf, size_t len, uint32_t timeout_ms)
{
    (void)sock; (void)buf; (void)len; (void)timeout_ms;
    return -1;
}

void ec_socket_close(ec_socket_t *sock)
{
    (void)sock;
}

#else
#error "Define EC_PLATFORM_POSIX or EC_PLATFORM_FREERTOS"
#endif
