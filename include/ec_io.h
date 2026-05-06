#ifndef EC_IO_H
#define EC_IO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Transport-agnostic I/O interface.
 *
 * Implement these two callbacks to add a new input/output channel
 * (UART, Telnet, USB CDC, BLE UART, …). Then call ec_io_init() with
 * a pointer to your ops struct.
 */
typedef struct {
    /**
     * Read one line of user input (blocking).
     * Must null-terminate buf and strip the trailing newline.
     * Returns number of bytes written to buf (>= 0), or -1 on error /
     * connection closed.
     */
    int (*read_line)(char *buf, size_t size);

    /**
     * Write a null-terminated string to the user.
     * Returns 0 on success, -1 on error.
     */
    int (*write)(const char *str);
} ec_io_ops_t;

/** Set the active I/O backend. Must be called before any other ec_io_* call. */
void ec_io_init(const ec_io_ops_t *ops);

/** Read one line of user input. Calls ops->read_line. */
int ec_io_read_line(char *buf, size_t size);

/** Write a string to the user. Calls ops->write. */
int ec_io_write(const char *str);

/* -------------------------------------------------------------------------
 * Available backends — include ec_io_uart.h / ec_io_telnet.h for ops structs.
 * ------------------------------------------------------------------------- */

/** POSIX: stdin/stdout. FreeRTOS: UART HAL (stub). */
extern const ec_io_ops_t ec_io_uart_ops;

/*
 * FreeRTOS UART HAL bridge.
 *
 * Provide blocking or timeout-based byte transport hooks from your board/HAL,
 * then call ec_io_uart_set_hal() before using ec_io_uart_ops on FreeRTOS.
 *
 * Return conventions for read/write hooks:
 *   > 0  number of bytes transferred
 *   = 0  timeout with no data/progress
 *   < 0  error
 */
typedef struct {
    int (*read)(void *buf, size_t len, uint32_t timeout_ms);
    int (*write)(const void *buf, size_t len, uint32_t timeout_ms);
} ec_io_uart_hal_t;

void ec_io_uart_set_hal(const ec_io_uart_hal_t *hal);

/** POSIX + FreeRTOS: TCP server on EC_CONFIG_TELNET_PORT. */
extern const ec_io_ops_t ec_io_telnet_ops;

#ifdef __cplusplus
}
#endif

#endif /* EC_IO_H */
