#include "ec_http.h"
#include "ec_socket.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Maximum size for the request header buffer */
#define EC_HTTP_HDR_BUF_SIZE 2048

/* Receive timeout in milliseconds */
#define EC_HTTP_RECV_TIMEOUT 30000

static int parse_status_line(const char *line, size_t len)
{
    /* "HTTP/1.1 200 OK\r\n" */
    const char *p = line;
    const char *end = line + len;

    /* skip "HTTP/x.x " */
    while (p < end && *p != ' ') p++;
    if (p >= end) return -1;
    p++; /* skip space */

    int code = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        code = code * 10 + (*p - '0');
        p++;
    }
    return code > 0 ? code : -1;
}

/*
 * Find \r\n\r\n in the buffer, return pointer to the start of the body.
 * Returns NULL if not found.
 */
static const char *find_header_end(const char *buf, size_t len)
{
    for (size_t i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i+1] == '\n' &&
            buf[i+2] == '\r' && buf[i+3] == '\n') {
            return buf + i + 4;
        }
    }
    return NULL;
}

/*
 * Case-insensitive search for a header value.
 * Returns the value as an integer, or -1 if not found.
 */
static int find_header_int(const char *headers, size_t headers_len,
                           const char *name)
{
    size_t name_len = strlen(name);
    const char *p = headers;
    const char *end = headers + headers_len;

    while (p < end) {
        /* Find end of this line */
        const char *eol = p;
        while (eol < end && *eol != '\r' && *eol != '\n') eol++;

        size_t line_len = (size_t)(eol - p);
        if (line_len > name_len + 1 && p[name_len] == ':') {
            /* Case-insensitive compare */
            int match = 1;
            for (size_t i = 0; i < name_len; i++) {
                char a = p[i];
                char b = name[i];
                if (a >= 'A' && a <= 'Z') a += 32;
                if (b >= 'A' && b <= 'Z') b += 32;
                if (a != b) { match = 0; break; }
            }
            if (match) {
                const char *v = p + name_len + 1;
                while (v < eol && *v == ' ') v++;
                int val = 0;
                while (v < eol && *v >= '0' && *v <= '9') {
                    val = val * 10 + (*v - '0');
                    v++;
                }
                return val;
            }
        }

        /* Advance past \r\n */
        p = eol;
        if (p < end && *p == '\r') p++;
        if (p < end && *p == '\n') p++;
    }
    return -1;
}

/* Check if Transfer-Encoding: chunked is present */
static int is_chunked(const char *headers, size_t headers_len)
{
    const char *p = headers;
    const char *end = headers + headers_len;

    while (p < end) {
        const char *eol = p;
        while (eol < end && *eol != '\r' && *eol != '\n') eol++;

        /* Check for "transfer-encoding:" */
        const char *te = "transfer-encoding:";
        size_t te_len = strlen(te);
        size_t line_len = (size_t)(eol - p);

        if (line_len > te_len) {
            int match = 1;
            for (size_t i = 0; i < te_len; i++) {
                char a = p[i];
                if (a >= 'A' && a <= 'Z') a += 32;
                if (a != te[i]) { match = 0; break; }
            }
            if (match) {
                /* Check if value contains "chunked" */
                const char *v = p + te_len;
                while (v < eol) {
                    if (eol - v >= 7 && memcmp(v, "chunked", 7) == 0)
                        return 1;
                    v++;
                }
            }
        }

        p = eol;
        if (p < end && *p == '\r') p++;
        if (p < end && *p == '\n') p++;
    }
    return 0;
}

/* Parse hex chunk size from a line */
static size_t parse_chunk_size(const char *p, const char *end)
{
    size_t size = 0;
    while (p < end) {
        char c = *p;
        if (c >= '0' && c <= '9') size = size * 16 + (size_t)(c - '0');
        else if (c >= 'a' && c <= 'f') size = size * 16 + (size_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') size = size * 16 + (size_t)(c - 'A' + 10);
        else break;
        p++;
    }
    return size;
}

int ec_http_request(const ec_http_request_t *req,
                    ec_http_response_t *resp,
                    char *resp_buf, size_t resp_buf_size)
{
    /* Build the request header */
    char hdr[EC_HTTP_HDR_BUF_SIZE];
    int hdr_len = snprintf(hdr, sizeof(hdr),
        "%s %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "%s"
        "\r\n",
        req->method,
        req->path,
        req->host,
        req->body_len,
        req->headers ? req->headers : "");

    if (hdr_len < 0 || (size_t)hdr_len >= sizeof(hdr)) {
        return EC_HTTP_ERR_OVERFLOW;
    }

    /* Connect */
    ec_socket_t *sock = ec_socket_connect(req->host, req->port, req->use_tls);
    if (!sock) {
        return EC_HTTP_ERR_CONNECT;
    }

    /* Send header */
    if (ec_socket_send(sock, hdr, (size_t)hdr_len) < 0) {
        ec_socket_close(sock);
        return EC_HTTP_ERR_SEND;
    }

    /* Send body */
    if (req->body && req->body_len > 0) {
        if (ec_socket_send(sock, req->body, req->body_len) < 0) {
            ec_socket_close(sock);
            return EC_HTTP_ERR_SEND;
        }
    }

    /* Receive response into a temporary buffer (headers + body mixed) */
    /* We use resp_buf as our receive buffer, parsing in-place. */
    size_t total_recv = 0;
    const char *body_start = NULL;

    /* First, receive until we have all headers */
    while (total_recv < resp_buf_size - 1) {
        int n = ec_socket_recv(sock, resp_buf + total_recv,
                               resp_buf_size - 1 - total_recv,
                               EC_HTTP_RECV_TIMEOUT);
        if (n < 0) {
            ec_socket_close(sock);
            return EC_HTTP_ERR_RECV;
        }
        if (n == 0) break; /* connection closed or timeout */
        total_recv += (size_t)n;
        resp_buf[total_recv] = '\0';

        body_start = find_header_end(resp_buf, total_recv);
        if (body_start) break;
    }

    if (!body_start) {
        ec_socket_close(sock);
        return EC_HTTP_ERR_PARSE;
    }

    /* Parse status line */
    int status = parse_status_line(resp_buf, total_recv);
    if (status < 0) {
        ec_socket_close(sock);
        return EC_HTTP_ERR_PARSE;
    }

    size_t header_len = (size_t)(body_start - resp_buf);
    int content_length = find_header_int(resp_buf, header_len, "content-length");
    int chunked = is_chunked(resp_buf, header_len);

    /* Read the rest of the body */
    if (!chunked && content_length >= 0) {
        /* Known content length */
        size_t body_have = total_recv - header_len;
        size_t body_need = (size_t)content_length;

        while (body_have < body_need && header_len + body_have < resp_buf_size - 1) {
            int n = ec_socket_recv(sock, resp_buf + total_recv,
                                   resp_buf_size - 1 - total_recv,
                                   EC_HTTP_RECV_TIMEOUT);
            if (n <= 0) break;
            total_recv += (size_t)n;
            body_have += (size_t)n;
        }
    } else {
        /* Unknown length or chunked — read until connection closes */
        while (total_recv < resp_buf_size - 1) {
            int n = ec_socket_recv(sock, resp_buf + total_recv,
                                   resp_buf_size - 1 - total_recv,
                                   EC_HTTP_RECV_TIMEOUT);
            if (n <= 0) break;
            total_recv += (size_t)n;
        }
    }
    resp_buf[total_recv] = '\0';
    ec_socket_close(sock);

    /* Recalculate body_start after all data is in */
    body_start = resp_buf + header_len;
    size_t body_len = total_recv - header_len;

    if (chunked) {
        /* Decode chunked transfer encoding in-place */
        const char *rp = body_start;
        const char *rend = resp_buf + total_recv;
        char *wp = resp_buf; /* reuse the beginning of the buffer for decoded body */
        size_t decoded = 0;

        while (rp < rend) {
            /* Find end of chunk-size line */
            const char *line_end = rp;
            while (line_end < rend && *line_end != '\r' && *line_end != '\n')
                line_end++;

            size_t chunk_size = parse_chunk_size(rp, line_end);
            if (chunk_size == 0) break; /* last chunk */

            /* Skip past \r\n */
            rp = line_end;
            if (rp < rend && *rp == '\r') rp++;
            if (rp < rend && *rp == '\n') rp++;

            /* Copy chunk data */
            size_t avail = (size_t)(rend - rp);
            size_t to_copy = chunk_size < avail ? chunk_size : avail;
            if (decoded + to_copy >= resp_buf_size) {
                to_copy = resp_buf_size - decoded - 1;
            }
            memmove(wp + decoded, rp, to_copy);
            decoded += to_copy;
            rp += chunk_size;

            /* Skip trailing \r\n */
            if (rp < rend && *rp == '\r') rp++;
            if (rp < rend && *rp == '\n') rp++;
        }

        resp_buf[decoded] = '\0';
        resp->status_code = status;
        resp->body = resp_buf;
        resp->body_len = decoded;
    } else {
        /* Move body to the front of the buffer to give caller a clean pointer */
        memmove(resp_buf, body_start, body_len);
        resp_buf[body_len] = '\0';
        resp->status_code = status;
        resp->body = resp_buf;
        resp->body_len = body_len;
    }

    return 0;
}
