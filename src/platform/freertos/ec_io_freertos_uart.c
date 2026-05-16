#include "ec_io.h"
#include "ec_config.h"

#include <string.h>

static const ec_io_uart_hal_t *s_uart_hal = NULL;

void ec_io_uart_set_hal(const ec_io_uart_hal_t *hal)
{
    s_uart_hal = hal;
}

#if !defined(EC_PLATFORM_FREERTOS)
#error "ec_io_freertos_uart.c must only be built for EC_PLATFORM=FREERTOS"
#endif


/*
 * FreeRTOS UART backend.
 *
 * Board code supplies the underlying blocking/timeout-based UART transport via
 * ec_io_uart_set_hal(). This keeps the agent runtime single-threaded while
 * avoiding hard-coded dependencies on one vendor HAL.
 */

static int uart_read_line(char *buf, size_t size)
{
    if (!buf || size == 0) return -1;
    if (!s_uart_hal || !s_uart_hal->read) return -1;

    size_t len = 0;
    int truncated = 0;

    for (;;) {
        char ch = '\0';
        int rc = s_uart_hal->read(&ch, 1, EC_CONFIG_UART_RX_TIMEOUT_MS);
        if (rc < 0) return -1;
        if (rc == 0) continue; /* blocking line mode: keep waiting */

        if (ch == '\r') continue;
        if (ch == '\n') break;

        if (len < size - 1) {
            buf[len++] = ch;
        } else {
            truncated = 1;
        }
    }

    if (truncated) {
        static const char warning[] = "\n[input truncated: line too long]\n";
        if (s_uart_hal->write) {
            s_uart_hal->write(warning, sizeof(warning) - 1,
                              EC_CONFIG_UART_TX_TIMEOUT_MS);
        }
        buf[0] = '\0';
        return 0;
    }

    buf[len] = '\0';
    return (int)len;
}

static int uart_write(const char *str)
{
    if (!str) return -1;
    if (!s_uart_hal || !s_uart_hal->write) return -1;

    size_t remaining = strlen(str);
    const char *p = str;

    while (remaining > 0) {
        int rc = s_uart_hal->write(p, remaining, EC_CONFIG_UART_TX_TIMEOUT_MS);
        if (rc <= 0) return -1;
        p += (size_t)rc;
        remaining -= (size_t)rc;
    }

    return 0;
}



const ec_io_ops_t ec_io_uart_ops = {
    .read_line = uart_read_line,
    .write     = uart_write,
};
