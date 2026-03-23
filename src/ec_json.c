#include "ec_json.h"

#include <string.h>
#include <stdio.h>

/* ========================================================================
 * JSON Writer
 * ======================================================================== */

static void jw_append(ec_json_writer_t *w, const char *s, size_t n)
{
    if (w->error) return;
    if (w->len + n >= w->cap) {
        w->error = 1;
        return;
    }
    memcpy(w->buf + w->len, s, n);
    w->len += n;
    w->buf[w->len] = '\0';
}

static void jw_putc(ec_json_writer_t *w, char c)
{
    jw_append(w, &c, 1);
}

static void jw_puts(ec_json_writer_t *w, const char *s)
{
    jw_append(w, s, strlen(s));
}

static void jw_comma(ec_json_writer_t *w)
{
    if (w->need_comma) {
        jw_putc(w, ',');
    }
    w->need_comma = 0;
}

/* Write a JSON-escaped string (with quotes) */
static void jw_quoted_string(ec_json_writer_t *w, const char *s)
{
    jw_putc(w, '"');
    while (*s) {
        switch (*s) {
        case '"':  jw_puts(w, "\\\""); break;
        case '\\': jw_puts(w, "\\\\"); break;
        case '\n': jw_puts(w, "\\n");  break;
        case '\r': jw_puts(w, "\\r");  break;
        case '\t': jw_puts(w, "\\t");  break;
        default:
            if ((unsigned char)*s < 0x20) {
                char esc[8];
                snprintf(esc, sizeof(esc), "\\u%04x", (unsigned char)*s);
                jw_puts(w, esc);
            } else {
                jw_putc(w, *s);
            }
            break;
        }
        s++;
    }
    jw_putc(w, '"');
}

void ec_json_writer_init(ec_json_writer_t *w, char *buf, size_t cap)
{
    w->buf = buf;
    w->cap = cap;
    w->len = 0;
    w->error = 0;
    w->need_comma = 0;
    if (cap > 0) buf[0] = '\0';
}

void ec_json_obj_start(ec_json_writer_t *w)
{
    jw_comma(w);
    jw_putc(w, '{');
    w->need_comma = 0;
}

void ec_json_obj_end(ec_json_writer_t *w)
{
    jw_putc(w, '}');
    w->need_comma = 1;
}

void ec_json_array_start(ec_json_writer_t *w, const char *key)
{
    jw_comma(w);
    jw_quoted_string(w, key);
    jw_putc(w, ':');
    jw_putc(w, '[');
    w->need_comma = 0;
}

void ec_json_array_end(ec_json_writer_t *w)
{
    jw_putc(w, ']');
    w->need_comma = 1;
}

void ec_json_key(ec_json_writer_t *w, const char *key)
{
    jw_comma(w);
    jw_quoted_string(w, key);
    jw_putc(w, ':');
    w->need_comma = 0;
}

void ec_json_add_string(ec_json_writer_t *w, const char *key, const char *val)
{
    jw_comma(w);
    jw_quoted_string(w, key);
    jw_putc(w, ':');
    jw_quoted_string(w, val);
    w->need_comma = 1;
}

void ec_json_add_int(ec_json_writer_t *w, const char *key, int val)
{
    jw_comma(w);
    jw_quoted_string(w, key);
    jw_putc(w, ':');
    char num[16];
    snprintf(num, sizeof(num), "%d", val);
    jw_puts(w, num);
    w->need_comma = 1;
}

void ec_json_array_obj_start(ec_json_writer_t *w)
{
    jw_comma(w);
    jw_putc(w, '{');
    w->need_comma = 0;
}

int ec_json_writer_finish(ec_json_writer_t *w)
{
    if (w->error) return -1;
    return (int)w->len;
}

/* ========================================================================
 * JSON Parser — lightweight path-based string lookup
 * ======================================================================== */

/* Skip whitespace */
static const char *skip_ws(const char *p, const char *end)
{
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
        p++;
    return p;
}

/* Skip a JSON value (string, number, object, array, true, false, null) */
static const char *skip_value(const char *p, const char *end)
{
    p = skip_ws(p, end);
    if (p >= end) return NULL;

    if (*p == '"') {
        /* string */
        p++;
        while (p < end) {
            if (*p == '\\') { p += 2; continue; }
            if (*p == '"') return p + 1;
            p++;
        }
        return NULL;
    }

    if (*p == '{') {
        /* object */
        int depth = 1;
        p++;
        while (p < end && depth > 0) {
            if (*p == '"') {
                p++;
                while (p < end && *p != '"') {
                    if (*p == '\\') p++;
                    p++;
                }
                if (p < end) p++; /* skip closing quote */
            } else {
                if (*p == '{') depth++;
                else if (*p == '}') depth--;
                p++;
            }
        }
        return p;
    }

    if (*p == '[') {
        /* array */
        int depth = 1;
        p++;
        while (p < end && depth > 0) {
            if (*p == '"') {
                p++;
                while (p < end && *p != '"') {
                    if (*p == '\\') p++;
                    p++;
                }
                if (p < end) p++;
            } else {
                if (*p == '[') depth++;
                else if (*p == ']') depth--;
                p++;
            }
        }
        return p;
    }

    /* number, true, false, null */
    while (p < end && *p != ',' && *p != '}' && *p != ']' &&
           *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
        p++;
    return p;
}

/*
 * Find a key in the current object. p should point just past the opening '{'.
 * Returns pointer to the value after the colon, or NULL.
 */
static const char *find_key(const char *p, const char *end,
                            const char *key, size_t key_len)
{
    p = skip_ws(p, end);
    while (p < end && *p != '}') {
        /* expect a string key */
        p = skip_ws(p, end);
        if (p >= end || *p != '"') return NULL;
        p++; /* skip opening quote */

        const char *ks = p;
        while (p < end && *p != '"') {
            if (*p == '\\') p++;
            p++;
        }
        if (p >= end) return NULL;
        size_t kl = (size_t)(p - ks);
        p++; /* skip closing quote */

        /* expect colon */
        p = skip_ws(p, end);
        if (p >= end || *p != ':') return NULL;
        p++;
        p = skip_ws(p, end);

        if (kl == key_len && memcmp(ks, key, key_len) == 0) {
            return p; /* found it */
        }

        /* skip value */
        p = skip_value(p, end);
        if (!p) return NULL;

        p = skip_ws(p, end);
        if (p < end && *p == ',') p++;
    }
    return NULL;
}

int ec_json_find_string(const char *json, size_t json_len,
                        const char *path,
                        char *out, size_t out_size)
{
    const char *p = json;
    const char *end = json + json_len;

    p = skip_ws(p, end);
    if (p >= end) return -1;

    /* Navigate the path */
    const char *seg = path;
    while (*seg) {
        /* Parse next segment: either "key" or "key[N]" */
        const char *dot = seg;
        while (*dot && *dot != '.' && *dot != '[') dot++;

        size_t key_len = (size_t)(dot - seg);

        if (key_len > 0) {
            /* Navigate into an object by key */
            if (*p != '{') return -1;
            p++; /* skip '{' */
            p = find_key(p, end, seg, key_len);
            if (!p) return -1;
        }

        seg = dot;

        /* Handle array index */
        if (*seg == '[') {
            seg++; /* skip '[' */
            int idx = 0;
            while (*seg >= '0' && *seg <= '9') {
                idx = idx * 10 + (*seg - '0');
                seg++;
            }
            if (*seg == ']') seg++;

            p = skip_ws(p, end);
            if (p >= end || *p != '[') return -1;
            p++; /* skip '[' */

            for (int i = 0; i < idx; i++) {
                p = skip_ws(p, end);
                p = skip_value(p, end);
                if (!p) return -1;
                p = skip_ws(p, end);
                if (p < end && *p == ',') p++;
            }
            p = skip_ws(p, end);
        }

        if (*seg == '.') seg++;
    }

    /* p should now point to the target value — extract a string */
    p = skip_ws(p, end);
    if (p >= end || *p != '"') return -1;
    p++; /* skip opening quote */

    size_t out_len = 0;
    while (p < end && *p != '"') {
        char c;
        if (*p == '\\') {
            p++;
            if (p >= end) return -1;
            switch (*p) {
            case '"':  c = '"';  break;
            case '\\': c = '\\'; break;
            case 'n':  c = '\n'; break;
            case 'r':  c = '\r'; break;
            case 't':  c = '\t'; break;
            case '/':  c = '/';  break;
            default:   c = *p;   break;
            }
        } else {
            c = *p;
        }
        if (out_len + 1 < out_size) {
            out[out_len] = c;
        }
        out_len++;
        p++;
    }
    if (out_len < out_size) {
        out[out_len] = '\0';
    } else if (out_size > 0) {
        out[out_size - 1] = '\0';
    }
    return (int)out_len;
}
