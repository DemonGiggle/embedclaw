#ifndef EC_SOCKET_H
#define EC_SOCKET_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque socket handle */
typedef struct ec_socket ec_socket_t;

/**
 * Connect to a remote host over TCP, optionally with TLS.
 *
 * @param host      Hostname or IP address.
 * @param port      TCP port number.
 * @param use_tls   1 = perform TLS handshake after TCP connect, 0 = plain TCP.
 * @return Socket handle on success, NULL on failure.
 *
 * When EC_CONFIG_USE_TLS is 0 (mbedTLS not linked), passing use_tls=1
 * returns NULL.
 */
ec_socket_t *ec_socket_connect(const char *host, uint16_t port, int use_tls);

/**
 * Send data over the socket.
 * Returns number of bytes sent, or negative on error.
 */
int ec_socket_send(ec_socket_t *sock, const void *data, size_t len);

/**
 * Receive data from the socket.
 * Returns number of bytes received, 0 on connection closed, or negative on error.
 */
int ec_socket_recv(ec_socket_t *sock, void *buf, size_t len, uint32_t timeout_ms);

/**
 * Close the socket and free resources.
 */
void ec_socket_close(ec_socket_t *sock);

#ifdef __cplusplus
}
#endif

#endif /* EC_SOCKET_H */
