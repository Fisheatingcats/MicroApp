/** @file helper.c @brief Cross-file call and data.rel.ro pointer fixture. */
#include "host_api.h"
/** @brief Counter increment implementation; interface contract is in host_api.h. */
uint32_t counter_increment(uint32_t value) { return value + 1u; }
/** @brief String/function aggregate whose pointer values are restored in RAM. */
const counter_indirect_t counter_indirect = { "MicroApp pointers", counter_increment };
