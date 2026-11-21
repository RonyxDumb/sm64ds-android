/* PORT: the decomp's register map, plus what the native platform layer needs on top of it.
 * The I/O space (0x04000000) is ordinary memory reserved at its DS address (port/platform/hostside/memory.c).
 * Registers whose reads must be computed are redirected here; writes with side effects are rewritten by
 * tools/portify.py into PORT_IO_WRITE (port/platform/include/port_hw.h). */
#pragma once
#include_next "nitro/hw/registers.h"
#include "port_hw.h"

/* Divider and square root: computed from the parameter registers when the result is read. */
#undef REG_DIV_RESULT_ADDR
#undef REG_DIVREM_RESULT_ADDR
#undef REG_SQRT_RESULT
#define REG_DIV_RESULT_ADDR ((unsigned long) port_cp_div())
#define REG_DIVREM_RESULT_ADDR ((unsigned long) port_cp_div() + 8)
#define REG_SQRT_RESULT (*(volatile u32 *) port_cp_sqrt())
