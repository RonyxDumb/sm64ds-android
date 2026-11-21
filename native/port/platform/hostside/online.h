// Online play: the wireless datagrams (wm7.c) between stations that are not on the same local network.
//
// The stations of a room meet at a relay server (tools/relay_server.py). Each keeps one UDP flow to the server; the
// server knows every station by the public address that flow comes from, and that address is the station's address
// for wm7.c, exactly as a local address is on the Wi-Fi network:
//  - a broadcast (the parent's beacon) goes to the server, which hands it to the other stations of the room;
//  - a datagram for a station goes to the server with the station's address, and arrives with the sender's.
// Two stations that have exchanged datagrams through the server also try each other's public address directly (the
// flow to the server has opened it in most home routers): each asks the other, twice a second, whether it hears it
// (ONLINE_PROBE, answered with ONLINE_PROBE_ANSWER, both sent directly). While the answers come the datagrams for
// that station go directly, which leaves the server's detour out; when they stop, through the server again.
//
// Wire format, client <-> server, little endian, before the datagram:
//   u32 magic "MKDR", u8 version, u8 type, u16 port, u8 ip[4], u32 room
// To the server ip/port are the destination (ONLINE_DATA); from the server they are the sender. ONLINE_HELLO keeps
// the flow open; the server answers with the station's own public address and, as a u16, the other stations in
// the room. ONLINE_QUERY is answered like a hello without joining the room (the app's connection test); ONLINE_BYE leaves the room at once (the app's count of players).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "hostapi.h"

#define ONLINE_MAGIC 0x52444b4du // "MKDR"
#define ONLINE_VERSION 1
#define ONLINE_HEADER 16
#define ONLINE_DEFAULT_PORT 23536
enum { ONLINE_HELLO, ONLINE_DATA, ONLINE_BROADCAST, ONLINE_PROBE, ONLINE_PROBE_ANSWER, ONLINE_QUERY, ONLINE_BYE };

struct addrinfo;
// The C library's functions (the 32-bit builds link the game's own socket functions under the same names).
typedef struct OnlineLibc {
    ssize_t (*sendto)(int, const void *, size_t, int, const struct sockaddr *, socklen_t);
    int (*getaddrinfo)(const char *, const char *, const struct addrinfo *, struct addrinfo **);
    void (*freeaddrinfo)(struct addrinfo *);
} OnlineLibc;

uint32_t online_room_hash(const char *room);
// "host port room" (port and room optional), or NULL / "" for local play. Resolves the host on a thread of its own.
void online_configure(const OnlineLibc *libc, const char *config);
bool online_enabled(void);
// to == NULL: to every station of the room.
void online_send(int sock, const HostNetAddr *to, const void *data, uint32_t size);
// A datagram of n bytes that came from *from: the wireless datagram in it is moved to the start of data and *from
// becomes its sender; returns its size, or -1 when there is nothing for the wireless in it.
int32_t online_receive(int sock, uint8_t *data, int32_t n, HostNetAddr *from);
// Keeps the flow to the server open; call often.
void online_tick(int sock);
