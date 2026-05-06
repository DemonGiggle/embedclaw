#ifndef EC_HW_ACCESS_H
#define EC_HW_ACCESS_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    EC_HW_ACCESS_READ = 0,
    EC_HW_ACCESS_WRITE = 1,
} ec_hw_access_kind_t;

/*
 * Returns 1 when the requested register access is permitted by the compiled-in
 * datasheet policy, otherwise 0 and writes a human-readable denial reason.
 */
int ec_hw_access_allowed(uint32_t address,
                         ec_hw_access_kind_t kind,
                         char *reason,
                         size_t reason_size);

#endif /* EC_HW_ACCESS_H */
