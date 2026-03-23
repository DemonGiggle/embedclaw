#ifndef EC_JSON_H
#define EC_JSON_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * JSON Builder — writes JSON into a fixed-size buffer
 * ======================================================================== */

typedef struct {
    char   *buf;
    size_t  cap;
    size_t  len;
    int     error;    /* set to 1 if buffer overflowed */
    int     need_comma;
} ec_json_writer_t;

void ec_json_writer_init(ec_json_writer_t *w, char *buf, size_t cap);

void ec_json_obj_start(ec_json_writer_t *w);
void ec_json_obj_end(ec_json_writer_t *w);
void ec_json_array_start(ec_json_writer_t *w, const char *key);
void ec_json_array_end(ec_json_writer_t *w);
void ec_json_key(ec_json_writer_t *w, const char *key);
void ec_json_add_string(ec_json_writer_t *w, const char *key, const char *val);
void ec_json_add_int(ec_json_writer_t *w, const char *key, int val);

/* Start a new object element inside an array */
void ec_json_array_obj_start(ec_json_writer_t *w);

/** Returns the number of bytes written (excluding null terminator), or -1 on error. */
int ec_json_writer_finish(ec_json_writer_t *w);

/* ========================================================================
 * JSON Parser — lightweight key-path lookup (no DOM)
 * ======================================================================== */

/**
 * Find a string value by key path in a JSON string.
 *
 * Supports dotted paths and array indices, e.g.:
 *   "choices[0].message.content"
 *
 * @param json      The JSON string to search.
 * @param json_len  Length of the JSON string.
 * @param path      Key path to find.
 * @param out       Output buffer for the extracted string value.
 * @param out_size  Size of the output buffer.
 * @return Length of the extracted value, or negative on error.
 */
int ec_json_find_string(const char *json, size_t json_len,
                        const char *path,
                        char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* EC_JSON_H */
