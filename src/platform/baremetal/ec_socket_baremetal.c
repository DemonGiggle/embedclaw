#include "ec_socket.h"

#if !defined(EC_PLATFORM_BAREMETAL)
#error "ec_socket_baremetal.c must only be built for EC_PLATFORM=BAREMETAL"
#endif

/*
 * Bare-metal socket port.
 *
 * This port deliberately delegates TCP/TLS ownership to board support code.
 * That keeps the shared HTTP/model/agent stack reusable on MCUs that use a
 * vendor Ethernet driver, Wi-Fi module, cellular modem, or TLS offload engine.
 */

struct ec_socket {
    int in_use;
};

static const ec_socket_baremetal_hal_t *s_hal = 0;
static ec_socket_t s_socket;

void ec_socket_baremetal_set_hal(const ec_socket_baremetal_hal_t *hal)
{
    s_hal = hal;
}

ec_socket_t *ec_socket_connect(const char *host, uint16_t port, int use_tls)
{
    if (!s_hal || !s_hal->connect || s_socket.in_use) {
        return 0;
    }

    if (s_hal->connect(s_hal->ctx, host, port, use_tls) != 0) {
        return 0;
    }

    s_socket.in_use = 1;
    return &s_socket;
}

int ec_socket_send(ec_socket_t *sock, const void *data, size_t len)
{
    if (!sock || !sock->in_use || !s_hal || !s_hal->send) {
        return -1;
    }
    return s_hal->send(s_hal->ctx, data, len);
}

int ec_socket_recv(ec_socket_t *sock, void *buf, size_t len, uint32_t timeout_ms)
{
    if (!sock || !sock->in_use || !s_hal || !s_hal->recv) {
        return -1;
    }
    return s_hal->recv(s_hal->ctx, buf, len, timeout_ms);
}

void ec_socket_close(ec_socket_t *sock)
{
    if (!sock || !sock->in_use) {
        return;
    }

    if (s_hal && s_hal->close) {
        s_hal->close(s_hal->ctx);
    }
    sock->in_use = 0;
}
