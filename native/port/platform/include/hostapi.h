// What the platform layer needs from the operating system, behind one small interface.
//
// The game and the DS platform layer ("guest") use only these functions, never the C library's threads, clocks,
// files or memory mappings directly. In the 32-bit builds they are plain calls (host_direct.c). In the arm64 build
// the guest is a separate image compiled with 32-bit pointers (ILP32) and loaded below 4 GiB, and each of these is a
// call into the 64-bit host library (host/bridge).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HostTime {
    int32_t year, month, day, weekday, hour, minute, second;
} HostTime;

void hapi_log(const char *line);
__attribute__((noreturn)) void hapi_abort(void);

uint64_t hapi_now_ns(void); // monotonic
void hapi_sleep_ns(uint64_t ns);
void hapi_local_time(HostTime *out);

void *hapi_mutex_new(void);
void hapi_mutex_lock(void *mutex);
void hapi_mutex_unlock(void *mutex);
void *hapi_sem_new(void);
void hapi_sem_free(void *sem);
void hapi_sem_post(void *sem);
void hapi_sem_wait(void *sem);

// A new host thread running fn(arg) (with its stack below 4 GiB in the arm64 build).
void hapi_thread_start(void (*fn)(void *), void *arg);
__attribute__((noreturn)) void hapi_thread_exit(void);
// One pointer per thread (replaces __thread, which the guest image cannot use).
void *hapi_tls_get(void);
void hapi_tls_set(void *value);

// Memory map: the VRAM banks (one block, see memory.c) and their CPU mapping.
uint8_t *hapi_vram_banks(void);
void hapi_vram_remap(int bank);

// The ROM image (read-only, mapped below 4 GiB) and the save file.
const uint8_t *hapi_rom(uint32_t *size);
uint32_t hapi_save_read(void *data, uint32_t size);
bool hapi_save_write(const void *data, uint32_t size);

// Local network, for the DS wireless (wm7.c): UDP datagrams on the LAN. Addresses are IPv4 in network order.
typedef struct HostNetAddr {
    uint32_t ip, port;
} HostNetAddr;
bool hapi_net_open(void); // false when no socket can be opened
void hapi_net_close(void);
void hapi_net_mac(uint8_t *mac); // this device's wireless address (stable, random)
// to == NULL: to every station of the local network (broadcast).
void hapi_net_send(const HostNetAddr *to, const void *data, uint32_t size);
// The next datagram, or -1 when none is waiting.
int32_t hapi_net_recv(void *data, uint32_t capacity, HostNetAddr *from);

#ifdef __cplusplus
}
#endif
