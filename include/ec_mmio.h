#ifndef EC_MMIO_H
#define EC_MMIO_H

#include <stddef.h>
#include <stdint.h>

/*
 * Thin MMIO abstraction shared by host and embedded builds.
 *
 * Host builds may back this with simulated/bookkeeping memory.
 * Embedded builds may perform direct memory-mapped accesses.
 */
int ec_mmio_read32(uint32_t address, uint32_t *value);
int ec_mmio_write32(uint32_t address, uint32_t value);

#if defined(EC_CONFIG_HOST_SIM) && EC_CONFIG_HOST_SIM
void ec_mmio_reset(void);
size_t ec_mmio_entry_count(void);
int ec_mmio_entry_at(size_t index, uint32_t *address, uint32_t *value);
#endif

#endif /* EC_MMIO_H */
