#include "ec_hw_datasheet.h"

/*
 * Include the ASIC-specific register map header here.
 * Replace with your own chip's header (e.g. ec_hw_my_asic.h).
 * If no ASIC header is included, define EC_HW_NO_DATASHEET to compile
 * with an empty register map.
 */
#if !defined(EC_HW_NO_DATASHEET)
#include "ec_hw_example_asic.h"
#else
const ec_hw_module_t *EC_HW_MODULES      = NULL;
const size_t          EC_HW_MODULE_COUNT = 0;
#endif
