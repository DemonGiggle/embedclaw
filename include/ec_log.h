#ifndef EC_LOG_H
#define EC_LOG_H

#include "ec_config.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Debug logging for EmbedClaw.
 *
 * On POSIX:    enabled at runtime via EC_DEBUG=1 environment variable.
 * On FreeRTOS: enabled at compile time via EC_CONFIG_DEBUG_LOG=1.
 *
 * All output goes to stderr so it doesn't mix with the agent's
 * stdout/UART output.
 */

#if defined(EC_PLATFORM_POSIX)

/* Runtime check — call ec_log_init() once at startup */
void ec_log_init(void);
int  ec_log_enabled(void);

#define EC_LOG_DEBUG(fmt, ...) \
    do { \
        if (ec_log_enabled()) \
            fprintf(stderr, "[EC_DEBUG] " fmt "\n", ##__VA_ARGS__); \
    } while (0)

#elif defined(EC_CONFIG_DEBUG_LOG) && EC_CONFIG_DEBUG_LOG

#define EC_LOG_DEBUG(fmt, ...) \
    fprintf(stderr, "[EC_DEBUG] " fmt "\n", ##__VA_ARGS__)

static inline void ec_log_init(void) {}
static inline int  ec_log_enabled(void) { return 1; }

#else

#define EC_LOG_DEBUG(fmt, ...) ((void)0)
static inline void ec_log_init(void) {}
static inline int  ec_log_enabled(void) { return 0; }

#endif

/**
 * Log a potentially large buffer (e.g. JSON body).
 * Prints it as-is to stderr, bracketed with a label.
 */
#define EC_LOG_BODY(label, buf, len) \
    do { \
        if (ec_log_enabled()) { \
            fprintf(stderr, "[EC_DEBUG] --- %s (%zu bytes) ---\n", \
                    (label), (size_t)(len)); \
            fwrite((buf), 1, (len), stderr); \
            fprintf(stderr, "\n[EC_DEBUG] --- end %s ---\n", (label)); \
        } \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* EC_LOG_H */
