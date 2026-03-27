#include "ec_log.h"

#if defined(EC_PLATFORM_POSIX)

#include <stdlib.h>

static int s_debug_enabled = -1; /* -1 = not yet checked */

void ec_log_init(void)
{
    const char *val = getenv("EC_DEBUG");
    s_debug_enabled = (val && val[0] == '1') ? 1 : 0;
}

int ec_log_enabled(void)
{
    if (s_debug_enabled < 0)
        ec_log_init();  /* lazy init if caller forgot */
    return s_debug_enabled;
}

#endif
