#include "ec_mmio.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#if defined(EC_PLATFORM_POSIX) && defined(EC_CONFIG_HOST_SIM) && EC_CONFIG_HOST_SIM

#define EC_MMIO_SIM_MAX_ENTRIES  64

typedef struct {
    uint32_t address;
    uint32_t value;
    int      in_use;
} ec_mmio_sim_entry_t;

static ec_mmio_sim_entry_t s_sim_entries[EC_MMIO_SIM_MAX_ENTRIES];

static ec_mmio_sim_entry_t *find_sim_entry(uint32_t address)
{
    for (size_t i = 0; i < EC_MMIO_SIM_MAX_ENTRIES; i++) {
        if (s_sim_entries[i].in_use && s_sim_entries[i].address == address) {
            return &s_sim_entries[i];
        }
    }
    return NULL;
}

static ec_mmio_sim_entry_t *alloc_sim_entry(uint32_t address)
{
    ec_mmio_sim_entry_t *entry = find_sim_entry(address);
    if (entry) return entry;

    for (size_t i = 0; i < EC_MMIO_SIM_MAX_ENTRIES; i++) {
        if (!s_sim_entries[i].in_use) {
            s_sim_entries[i].in_use = 1;
            s_sim_entries[i].address = address;
            s_sim_entries[i].value = 0;
            return &s_sim_entries[i];
        }
    }
    return NULL;
}

int ec_mmio_read32(uint32_t address, uint32_t *value)
{
    if (!value) return -1;
    if ((address & 0x3U) != 0) return -1;

    ec_mmio_sim_entry_t *entry = find_sim_entry(address);
    *value = entry ? entry->value : 0;
    return 0;
}

int ec_mmio_write32(uint32_t address, uint32_t value)
{
    if ((address & 0x3U) != 0) return -1;

    ec_mmio_sim_entry_t *entry = alloc_sim_entry(address);
    if (!entry) return -1;

    entry->value = value;
    return 0;
}

void ec_mmio_reset(void)
{
    memset(s_sim_entries, 0, sizeof(s_sim_entries));
}

size_t ec_mmio_entry_count(void)
{
    size_t count = 0;

    for (size_t i = 0; i < EC_MMIO_SIM_MAX_ENTRIES; i++) {
        if (s_sim_entries[i].in_use) count++;
    }

    return count;
}

int ec_mmio_entry_at(size_t index, uint32_t *address, uint32_t *value)
{
    size_t seen = 0;

    for (size_t i = 0; i < EC_MMIO_SIM_MAX_ENTRIES; i++) {
        if (!s_sim_entries[i].in_use) continue;

        if (seen == index) {
            if (address) *address = s_sim_entries[i].address;
            if (value) *value = s_sim_entries[i].value;
            return 0;
        }
        seen++;
    }

    return -1;
}

#elif defined(EC_PLATFORM_POSIX)

#define EC_MMIO_MOCK_BASE   0x40000000UL
#define EC_MMIO_MOCK_COUNT  16

static uint32_t s_mock_regs[EC_MMIO_MOCK_COUNT];

int ec_mmio_read32(uint32_t address, uint32_t *value)
{
    if (!value) return -1;
    if (address < EC_MMIO_MOCK_BASE) return -1;
    if ((address & 0x3U) != 0) return -1;

    size_t idx = (address - EC_MMIO_MOCK_BASE) / 4;
    if (idx >= EC_MMIO_MOCK_COUNT) return -1;

    *value = s_mock_regs[idx];
    return 0;
}

int ec_mmio_write32(uint32_t address, uint32_t value)
{
    if (address < EC_MMIO_MOCK_BASE) return -1;
    if ((address & 0x3U) != 0) return -1;

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
