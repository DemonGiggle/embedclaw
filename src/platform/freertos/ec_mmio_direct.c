#include "ec_mmio.h"

#if !defined(EC_PLATFORM_FREERTOS)
#error "ec_mmio_direct.c must only be built for EC_PLATFORM=FREERTOS"
#endif

#include <stdint.h>

int ec_mmio_read32(uint32_t address, uint32_t *value)
{
    if (!value) return -1;
    *value = *(volatile uint32_t *)(uintptr_t)address;
    return 0;
}

int ec_mmio_write32(uint32_t address, uint32_t value)
{
    *(volatile uint32_t *)(uintptr_t)address = value;
    return 0;
}
