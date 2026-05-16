#include "ec_socket.h"
#include "ec_config.h"

#include <stdlib.h>
#include <string.h>

#if EC_CONFIG_USE_TLS
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/error.h>
#include "ec_cacerts.h"
#endif

#if !defined(EC_PLATFORM_FREERTOS)
#error "ec_socket_freertos.c must only be built for EC_PLATFORM=FREERTOS"
#endif


#include "FreeRTOS.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"

#include <stdio.h>

#define EC_FREERTOS_CONNECT_TIMEOUT_MS  5000U

static TickType_t ms_to_ticks(uint32_t timeout_ms)
{
    TickType_t ticks = pdMS_TO_TICKS(timeout_ms);
    if (timeout_ms > 0U && ticks == 0) {
        ticks = 1;
    }
    return ticks;
}

struct ec_socket {
    Socket_t fd;
#if EC_CONFIG_USE_TLS
    int tls_active;
    mbedtls_ssl_context      ssl;
    mbedtls_ssl_config       ssl_conf;
    mbedtls_entropy_context  entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_x509_crt         ca_cert;
#endif
};

#if EC_CONFIG_USE_TLS

static int tls_bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    Socket_t fd = *(Socket_t *)ctx;
    BaseType_t n = FreeRTOS_send(fd, buf, len, 0);
    if (n == -pdFREERTOS_ERRNO_EWOULDBLOCK) {
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    }
    if (n < 0) {
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return (int)n;
}

static int tls_bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    Socket_t fd = *(Socket_t *)ctx;
    BaseType_t n = FreeRTOS_recv(fd, buf, len, 0);
    if (n == -pdFREERTOS_ERRNO_EWOULDBLOCK) {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    if (n < 0) {
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    if (n == 0) {
        return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
    }
    return (int)n;
}

static int tls_handshake(ec_socket_t *sock, const char *host)
{
    mbedtls_ssl_init(&sock->ssl);
    mbedtls_ssl_config_init(&sock->ssl_conf);
    mbedtls_entropy_init(&sock->entropy);
    mbedtls_ctr_drbg_init(&sock->ctr_drbg);
    mbedtls_x509_crt_init(&sock->ca_cert);

    int ret = mbedtls_ctr_drbg_seed(&sock->ctr_drbg, mbedtls_entropy_func,
                                    &sock->entropy, NULL, 0);
    if (ret != 0) return ret;

    ret = mbedtls_x509_crt_parse(&sock->ca_cert,
                                 EC_CA_BUNDLE, EC_CA_BUNDLE_LEN);
    if (ret != 0) return ret;

    ret = mbedtls_ssl_config_defaults(&sock->ssl_conf,
                                      MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) return ret;

    mbedtls_ssl_conf_authmode(&sock->ssl_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&sock->ssl_conf, &sock->ca_cert, NULL);
    mbedtls_ssl_conf_rng(&sock->ssl_conf,
                         mbedtls_ctr_drbg_random, &sock->ctr_drbg);

    ret = mbedtls_ssl_setup(&sock->ssl, &sock->ssl_conf);
    if (ret != 0) return ret;

    ret = mbedtls_ssl_set_hostname(&sock->ssl, host);
    if (ret != 0) return ret;

    mbedtls_ssl_set_bio(&sock->ssl, &sock->fd,
                        tls_bio_send, tls_bio_recv, NULL);

    while ((ret = mbedtls_ssl_handshake(&sock->ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
            ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            return ret;
        }
    }

    sock->tls_active = 1;
    return 0;
}

static void tls_cleanup(ec_socket_t *sock)
{
    if (sock->tls_active) {
        mbedtls_ssl_close_notify(&sock->ssl);
    }
    mbedtls_ssl_free(&sock->ssl);
    mbedtls_ssl_config_free(&sock->ssl_conf);
    mbedtls_ctr_drbg_free(&sock->ctr_drbg);
    mbedtls_entropy_free(&sock->entropy);
    mbedtls_x509_crt_free(&sock->ca_cert);
}

#endif /* EC_CONFIG_USE_TLS */

ec_socket_t *ec_socket_connect(const char *host, uint16_t port, int use_tls)
{
#if !EC_CONFIG_USE_TLS
    if (use_tls) return NULL;
#endif

    if (!host || host[0] == '\0') {
        return NULL;
    }

    struct freertos_addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = FREERTOS_AF_INET;
    hints.ai_socktype = FREERTOS_SOCK_STREAM;

    struct freertos_addrinfo *res = NULL;
    if (FreeRTOS_getaddrinfo(host, NULL, &hints, &res) != 0 || res == NULL) {
        return NULL;
    }

    Socket_t fd = FreeRTOS_socket(FREERTOS_AF_INET,
                                  FREERTOS_SOCK_STREAM,
                                  FREERTOS_IPPROTO_TCP);
    if (fd == FREERTOS_INVALID_SOCKET) {
        FreeRTOS_freeaddrinfo(res);
        return NULL;
    }

    TickType_t timeout = ms_to_ticks(EC_FREERTOS_CONNECT_TIMEOUT_MS);
    FreeRTOS_setsockopt(fd, 0, FREERTOS_SO_RCVTIMEO,
                        &timeout, sizeof(timeout));
    FreeRTOS_setsockopt(fd, 0, FREERTOS_SO_SNDTIMEO,
                        &timeout, sizeof(timeout));

    struct freertos_sockaddr remote_addr;
    memset(&remote_addr, 0, sizeof(remote_addr));
    memcpy(&remote_addr, res->ai_addr,
           res->ai_addrlen < sizeof(remote_addr)
               ? res->ai_addrlen
               : sizeof(remote_addr));
    remote_addr.sin_port = FreeRTOS_htons(port);
    FreeRTOS_freeaddrinfo(res);

    if (FreeRTOS_connect(fd, &remote_addr, sizeof(remote_addr)) != 0) {
        FreeRTOS_closesocket(fd);
        return NULL;
    }

    ec_socket_t *sock = (ec_socket_t *)calloc(1, sizeof(ec_socket_t));
    if (!sock) {
        FreeRTOS_closesocket(fd);
        return NULL;
    }
    sock->fd = fd;

#if EC_CONFIG_USE_TLS
    if (use_tls) {
        if (tls_handshake(sock, host) != 0) {
            tls_cleanup(sock);
            FreeRTOS_closesocket(fd);
            free(sock);
            return NULL;
        }
    }
#endif

    return sock;
}

int ec_socket_send(ec_socket_t *sock, const void *data, size_t len)
{
    if (!sock) return -1;

#if EC_CONFIG_USE_TLS
    if (sock->tls_active) {
        size_t total = 0;
        const unsigned char *p = (const unsigned char *)data;
        while (total < len) {
            int n = mbedtls_ssl_write(&sock->ssl, p + total, len - total);
            if (n == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            if (n < 0) return -1;
            total += (size_t)n;
        }
        return (int)total;
    }
#endif

    size_t total = 0;
    const uint8_t *p = (const uint8_t *)data;
    while (total < len) {
        BaseType_t n = FreeRTOS_send(sock->fd, p + total, len - total, 0);
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

    TickType_t timeout = ms_to_ticks(timeout_ms);
    FreeRTOS_setsockopt(sock->fd, 0, FREERTOS_SO_RCVTIMEO,
                        &timeout, sizeof(timeout));

#if EC_CONFIG_USE_TLS
    if (sock->tls_active) {
        int n = mbedtls_ssl_read(&sock->ssl, (unsigned char *)buf, len);
        if (n == MBEDTLS_ERR_SSL_WANT_READ) {
            return 0;
        }
        if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || n == 0) {
            return 0;
        }
        if (n < 0) {
            return -1;
        }
        return n;
    }
#endif

    BaseType_t n = FreeRTOS_recv(sock->fd, buf, len, 0);
    if (n == -pdFREERTOS_ERRNO_EWOULDBLOCK) {
        return 0;
    }
    if (n < 0) {
        return -1;
    }
    return (int)n;
}

void ec_socket_close(ec_socket_t *sock)
{
    if (!sock) return;

#if EC_CONFIG_USE_TLS
    if (sock->tls_active) {
        tls_cleanup(sock);
    }
#endif

    FreeRTOS_closesocket(sock->fd);
    free(sock);
}


