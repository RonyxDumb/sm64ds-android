// Native replacement for the Nintendo DS hardware: interface used by the decomp's code.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Writes to registers with side effects (geometry commands, VRAM bank control, interrupt flags).
void port_io_write(volatile void *reg, unsigned size, unsigned value);
#define PORT_IO_WRITE(reg, value) port_io_write(&(reg), sizeof(reg), (unsigned) (value))

// Divider / square root units: compute the result registers and return their address.
unsigned char *port_cp_div(void);
unsigned char *port_cp_sqrt(void);

#ifdef __cplusplus
}
#endif
