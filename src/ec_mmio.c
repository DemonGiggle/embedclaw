#include "ec_mmio.h"

#include <stdint.h>
#include <stddef.h>

#if defined(EC_PLATFORM_POSIX)

#define EC_MMIO_MOCK_BASE   0x40000000UL
#define EC_MMIO_MOCK_COUNT  16

static uint32_t s_mock_regs[EC_MMIO_MOCK_COUNT];

int ec_mmio_read32(uint32_t address, uint32_t *value)
{
    if (!value) return -1;
    if (address < EC_MMIO_MOCK_BASE) return -1;

    size_t idx = (address - EC_MMIO_MOCK_BASE) / 4;
    if (idx >= EC_MMIO_MOCK_COUNT) return -1;

    *value = s_mock_regs[idx];
    return 0;
}

int ec_mmio_write32(uint32_t address, uint32_t value)
{
    if (address < EC_MMIO_MOCK_BASE) return -1;

    size_t idx = (address - EC_MMIO_MOCK_BASE) / 4;
    if (idx >= EC_MMIO_MOCK_COUNT) return -1;

    s_mock_regs[idx] = value;
    return 0;
}

#elif defined(EC_PLATFORM_FREERTOS)

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

#else
#error "Define EC_PLATFORM_POSIX or EC_PLATFORM_FREERTOS"
#endif
