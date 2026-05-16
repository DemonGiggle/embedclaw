#include "ec_hw_access.h"
#include "ec_hw_datasheet.h"

#include <stdio.h>
#include <string.h>

static int access_allows_read(const char *access)
{
    return access == NULL || strcmp(access, "WO") != 0;
}

static int access_allows_write(const char *access)
{
    return access != NULL && strcmp(access, "RO") != 0;
}

static const ec_hw_module_t *find_register_by_address(uint32_t address,
                                                      const ec_hw_register_t **out_reg)
{
    for (size_t i = 0; i < EC_HW_MODULE_COUNT; i++) {
        const ec_hw_module_t *mod = &EC_HW_MODULES[i];
        for (size_t j = 0; j < mod->num_registers; j++) {
            const ec_hw_register_t *reg = &mod->registers[j];
            if (mod->base_addr + reg->offset == address) {
                *out_reg = reg;
                return mod;
            }
        }
    }

    *out_reg = NULL;
    return NULL;
}

static int register_allows_read(const ec_hw_register_t *reg)
{
    if (!reg->fields || reg->num_fields == 0) {
        return 0;
    }

    for (size_t i = 0; i < reg->num_fields; i++) {
        if (access_allows_read(reg->fields[i].access)) {
            return 1;
        }
    }

    return 0;
}

static int register_allows_write(const ec_hw_register_t *reg)
{
    if (!reg->fields || reg->num_fields == 0) {
        return 0;
    }

    for (size_t i = 0; i < reg->num_fields; i++) {
        if (access_allows_write(reg->fields[i].access)) {
            return 1;
        }
    }

    return 0;
}

int ec_hw_access_allowed(uint32_t address,
                         ec_hw_access_kind_t kind,
                         char *reason,
                         size_t reason_size)
{
    const ec_hw_register_t *reg = NULL;
    const ec_hw_module_t *mod = NULL;

    if (!reason || reason_size == 0) {
        return 0;
    }

    reason[0] = '\0';

    if (!EC_HW_MODULES || EC_HW_MODULE_COUNT == 0) {
        snprintf(reason, reason_size,
                 "no hardware datasheet is configured");
        return 0;
    }

    mod = find_register_by_address(address, &reg);
    if (!mod || !reg) {
        snprintf(reason, reason_size,
                 "address 0x%08x is not present in the hardware datasheet",
                 (unsigned)address);
        return 0;
    }

    if (kind == EC_HW_ACCESS_READ && !register_allows_read(reg)) {
        snprintf(reason, reason_size,
                 "%s.%s has no readable fields in the hardware datasheet",
                 mod->name, reg->name);
        return 0;
    }

    if (kind == EC_HW_ACCESS_WRITE && !register_allows_write(reg)) {
        snprintf(reason, reason_size,
                 "%s.%s has no writable fields in the hardware datasheet",
                 mod->name, reg->name);
        return 0;
    }

    return 1;
}
