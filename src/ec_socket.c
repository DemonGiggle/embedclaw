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
#if EC_CONFIG_USE_TLS
    int tls_active;
    mbedtls_ssl_context      ssl;
    mbedtls_ssl_config       ssl_conf;
    mbedtls_entropy_context  entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_x509_crt         ca_cert;
#endif
};

/* ---- TLS BIO callbacks (send/recv on raw fd) --------------------------- */
#if EC_CONFIG_USE_TLS

static int tls_bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    int fd = *(int *)ctx;
    ssize_t n = send(fd, buf, len, 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return MBEDTLS_ERR_SSL_WANT_WRITE;
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return (int)n;
}

static int tls_bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    int fd = *(int *)ctx;
    ssize_t n = recv(fd, buf, len, 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return MBEDTLS_ERR_SSL_WANT_READ;
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    if (n == 0)
        return MBEDTLS_ERR_SSL_WANT_READ;
    return (int)n;
}

static int tls_handshake(ec_socket_t *sock, const char *host)
{
    mbedtls_ssl_init(&sock->ssl);
    mbedtls_ssl_config_init(&sock->ssl_conf);
    mbedtls_entropy_init(&sock->entropy);
    mbedtls_ctr_drbg_init(&sock->ctr_drbg);
    mbedtls_x509_crt_init(&sock->ca_cert);

    /* Seed the DRBG */
    int ret = mbedtls_ctr_drbg_seed(&sock->ctr_drbg, mbedtls_entropy_func,
                                     &sock->entropy, NULL, 0);
    if (ret != 0) return ret;

    /* Load embedded CA bundle */
    ret = mbedtls_x509_crt_parse(&sock->ca_cert,
                                  EC_CA_BUNDLE, EC_CA_BUNDLE_LEN);
    if (ret != 0) return ret;

    /* Configure TLS client defaults */
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

    /* Set SNI hostname for virtual hosting */
    ret = mbedtls_ssl_set_hostname(&sock->ssl, host);
    if (ret != 0) return ret;

    /* Attach BIO callbacks using raw fd */
    mbedtls_ssl_set_bio(&sock->ssl, &sock->fd,
                         tls_bio_send, tls_bio_recv, NULL);

    /* Perform handshake */
    while ((ret = mbedtls_ssl_handshake(&sock->ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
            ret != MBEDTLS_ERR_SSL_WANT_WRITE)
            return ret;
    }

    sock->tls_active = 1;
    return 0;
}

static void tls_cleanup(ec_socket_t *sock)
{
    if (sock->tls_active)
        mbedtls_ssl_close_notify(&sock->ssl);
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
    if (use_tls) return NULL;   /* TLS not compiled in */
#endif

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

    ec_socket_t *sock = (ec_socket_t *)calloc(1, sizeof(ec_socket_t));
    if (!sock) {
        close(fd);
        return NULL;
    }
    sock->fd = fd;

#if EC_CONFIG_USE_TLS
    if (use_tls) {
        if (tls_handshake(sock, host) != 0) {
            tls_cleanup(sock);
            close(fd);
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
    const char *p = (const char *)data;
    while (total < len) {
        ssize_t n = send(sock->fd, p + total, len - total, 0);
        if (n <= 0) return -1;
        total += (size_t)n;
    }
    return (int)total;
}

int ec_socket_recv(ec_socket_t *sock, void *buf, size_t len, uint32_t timeout_ms)
{
    if (!sock) return -1;

    /* Set recv timeout — applies to both plain and TLS (via BIO callback) */
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

#if EC_CONFIG_USE_TLS
    if (sock->tls_active) {
        int n = mbedtls_ssl_read(&sock->ssl, (unsigned char *)buf, len);
        if (n == MBEDTLS_ERR_SSL_WANT_READ)
            return 0; /* timeout */
        if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || n == 0)
            return 0; /* connection closed */
        if (n < 0) return -1;
        return n;
    }
#endif

    ssize_t n = recv(sock->fd, buf, len, 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0; /* timeout */
        return -1;
    }
    return (int)n;
}

void ec_socket_close(ec_socket_t *sock)
{
    if (!sock) return;

#if EC_CONFIG_USE_TLS
    if (sock->tls_active)
        tls_cleanup(sock);
#endif

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
#if EC_CONFIG_USE_TLS
    int tls_active;
    mbedtls_ssl_context      ssl;
    mbedtls_ssl_config       ssl_conf;
    mbedtls_entropy_context  entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_x509_crt         ca_cert;
#endif
};

ec_socket_t *ec_socket_connect(const char *host, uint16_t port, int use_tls)
{
    (void)host; (void)port; (void)use_tls;
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
