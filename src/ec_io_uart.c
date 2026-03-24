#include "ec_io.h"

#include <string.h>

#if defined(EC_PLATFORM_POSIX)

#include <stdio.h>

static int uart_read_line(char *buf, size_t size)
{
    if (!fgets(buf, (int)size, stdin)) return -1;
    /* Strip trailing newline */
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') buf[--len] = '\0';
    if (len > 0 && buf[len - 1] == '\r') buf[--len] = '\0';
    return (int)len;
}

static int uart_write(const char *str)
{
    return fputs(str, stdout) < 0 ? -1 : 0;
}

#elif defined(EC_PLATFORM_FREERTOS)

/*
 * FreeRTOS UART stub — replace with your HAL calls.
 * Typical pattern: use xStreamBufferReceive / HAL_UART_Receive for read,
 * HAL_UART_Transmit for write.
 */

static int uart_read_line(char *buf, size_t size)
{
    (void)buf; (void)size;
    /* TODO: implement with FreeRTOS UART HAL */
    return -1;
}

static int uart_write(const char *str)
{
    (void)str;
    /* TODO: implement with FreeRTOS UART HAL */
    return -1;
}

#else
#error "Define EC_PLATFORM_POSIX or EC_PLATFORM_FREERTOS"
#endif

const ec_io_ops_t ec_io_uart_ops = {
    .read_line = uart_read_line,
    .write     = uart_write,
};
