#include "ec_io.h"
#include "ec_config.h"

#include <string.h>

static const ec_io_uart_hal_t *s_uart_hal = NULL;

void ec_io_uart_set_hal(const ec_io_uart_hal_t *hal)
{
    s_uart_hal = hal;
}

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

#else
#error "ec_io_posix_uart.c must only be built for EC_PLATFORM=POSIX"
#endif

const ec_io_ops_t ec_io_uart_ops = {
    .read_line = uart_read_line,
    .write     = uart_write,
};
