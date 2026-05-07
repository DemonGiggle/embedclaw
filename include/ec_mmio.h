#ifndef EC_MMIO_H
#define EC_MMIO_H

#include <stdint.h>

/*
 * Thin MMIO abstraction shared by host and embedded builds.
 *
 * Host builds may back this with simulated/bookkeeping memory.
 * Embedded builds may perform direct memory-mapped accesses.
 */
int ec_mmio_read32(uint32_t address, uint32_t *value);
int ec_mmio_write32(uint32_t address, uint32_t value);

#endif /* EC_MMIO_H */
