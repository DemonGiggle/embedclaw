#include "ec_io.h"

static const ec_io_ops_t *s_ops = NULL;

void ec_io_init(const ec_io_ops_t *ops)
{
    s_ops = ops;
}

int ec_io_read_line(char *buf, size_t size)
{
    if (!s_ops || !s_ops->read_line) return -1;
    return s_ops->read_line(buf, size);
}

int ec_io_write(const char *str)
{
    if (!s_ops || !s_ops->write) return -1;
    return s_ops->write(str);
}
