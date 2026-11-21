// Online play through a relay server: see online.h.
#include "online.h"
#include "port.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PEERS 32
#define MAX_DATAGRAM 1400
#define HELLO_NS 2000000000ull
#define HELLO_FIRST_NS 250000000ull     // until the server has answered
#define SERVER_SILENT_NS 10000000000ull // no answer for this long: the server is gone
#define PROBE_NS 500000000ull           // how often a station is asked directly whether it hears this one
#define DIRECT_ALIVE_NS 1600000000ull   // the direct way works while those questions are answered

typedef struct Peer {
    uint32_t ip, port;
    uint64_t seen_ns, probe_ns, direct_ns; // last used; last probe sent; last probe answer (0 = never)
    bool direct;
} Peer;

static OnlineLibc s_libc;
static char s_config[256];
static bool s_enabled;
static uint32_t s_room;
static Peer s_peers[MAX_PEERS];
static uint64_t s_hello_ns, s_answer_ns;
static bool s_answered;

// The server's address, written by the resolver thread: generation, then ip and port.
static _Atomic uint32_t s_generation, s_server_generation, s_server_ip, s_server_port;

typedef struct Resolve {
    uint32_t generation;
    char host[200], port[16];
} Resolve;

uint32_t online_room_hash(const char *room) { // FNV-1a
    uint32_t h = 2166136261u;
    for (const unsigned char *p = (const unsigned char *) room; *p; p++) h = (h ^ *p) * 16777619u;
    return h;
}

static void *resolve_thread(void *arg) {
    Resolve *r = arg;
    struct addrinfo hints, *list = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    // A phone that has just come back to the network may need a moment: a few tries.
    for (int i = 0; i < 5 && atomic_load(&s_generation) == r->generation; i++) {
        int err = s_libc.getaddrinfo(r->host, r->port, &hints, &list);
        if (err == 0 && list) break;
        list = NULL;
        port_log("online: cannot find %s (%d)", r->host, err);
        hapi_sleep_ns(2000000000ull);
    }
    if (list) {
        const struct sockaddr_in *a = (const struct sockaddr_in *) list->ai_addr;
        if (atomic_load(&s_generation) == r->generation) {
            atomic_store(&s_server_ip, a->sin_addr.s_addr);
            atomic_store(&s_server_port, ntohs(a->sin_port));
            atomic_store(&s_server_generation, r->generation);
            const uint8_t *b = (const uint8_t *) &a->sin_addr.s_addr;
            port_log("online: server %s is %u.%u.%u.%u port %u", r->host, b[0], b[1], b[2], b[3], ntohs(a->sin_port));
        }
        if (s_libc.freeaddrinfo) s_libc.freeaddrinfo(list);
    }
    free(r);
    return NULL;
}

void online_configure(const OnlineLibc *libc, const char *config) {
    if (!config) config = "";
    if (!strncmp(config, s_config, sizeof(s_config) - 1) && strlen(config) < sizeof(s_config)) return; // unchanged
    snprintf(s_config, sizeof(s_config), "%s", config);
    s_libc = *libc;
    uint32_t generation = atomic_fetch_add(&s_generation, 1) + 1;
    memset(s_peers, 0, sizeof(s_peers));
    s_answered = false;
    s_hello_ns = s_answer_ns = 0;

    char host[200] = "", room[64] = "";
    unsigned port = ONLINE_DEFAULT_PORT;
    int fields = sscanf(s_config, "%199s %u %63s", host, &port, room);
    s_enabled = fields >= 1 && host[0] && s_libc.sendto && s_libc.getaddrinfo;
    if (!s_enabled) {
        port_log("online: off (local wireless play)");
        return;
    }
    if (fields < 2 || port == 0 || port > 65535) port = ONLINE_DEFAULT_PORT;
    if (fields < 3 || !room[0]) snprintf(room, sizeof(room), "public");
    s_room = online_room_hash(room);
    port_log("online: on, server %s port %u, room %s", host, port, room);

    Resolve *r = calloc(1, sizeof(*r));
    r->generation = generation;
    snprintf(r->host, sizeof(r->host), "%s", host);
    snprintf(r->port, sizeof(r->port), "%u", port);
    pthread_t t;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&t, &attr, resolve_thread, r) != 0) free(r);
    pthread_attr_destroy(&attr);
}

bool online_enabled(void) { return s_enabled; }

static bool server(uint32_t *ip, uint32_t *port) {
    if (!s_enabled || atomic_load(&s_server_generation) != atomic_load(&s_generation)) return false;
    *ip = atomic_load(&s_server_ip);
    *port = atomic_load(&s_server_port);
    return true;
}

static void raw_send(int sock, uint32_t ip, uint32_t port, const void *data, size_t size) {
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = ip;
    a.sin_port = htons((uint16_t) port);
    s_libc.sendto(sock, data, size, 0, (struct sockaddr *) &a, sizeof(a));
}

static void put_header(uint8_t *h, uint8_t type, uint32_t ip, uint32_t port) {
    h[0] = (uint8_t) ONLINE_MAGIC, h[1] = (uint8_t) (ONLINE_MAGIC >> 8);
    h[2] = (uint8_t) (ONLINE_MAGIC >> 16), h[3] = (uint8_t) (ONLINE_MAGIC >> 24);
    h[4] = ONLINE_VERSION;
    h[5] = type;
    h[6] = (uint8_t) port, h[7] = (uint8_t) (port >> 8);
    memcpy(h + 8, &ip, 4); // network order, as in HostNetAddr
    h[12] = (uint8_t) s_room, h[13] = (uint8_t) (s_room >> 8);
    h[14] = (uint8_t) (s_room >> 16), h[15] = (uint8_t) (s_room >> 24);
}

static void relay_send(int sock, uint8_t type, uint32_t ip, uint32_t port, const void *data, uint32_t size) {
    uint32_t sip, sport;
    if (size > MAX_DATAGRAM || !server(&sip, &sport)) return;
    uint8_t buf[ONLINE_HEADER + MAX_DATAGRAM];
    put_header(buf, type, ip, port);
    if (size) memcpy(buf + ONLINE_HEADER, data, size);
    raw_send(sock, sip, sport, buf, ONLINE_HEADER + size);
}

static Peer *peer(uint32_t ip, uint32_t port, bool create, uint64_t now) {
    Peer *oldest = &s_peers[0];
    for (int i = 0; i < MAX_PEERS; i++) {
        Peer *p = &s_peers[i];
        if (p->seen_ns && p->ip == ip && p->port == port) {
            if (create) p->seen_ns = now;
            return p;
        }
        if (p->seen_ns < oldest->seen_ns) oldest = p;
    }
    if (!create) return NULL;
    memset(oldest, 0, sizeof(*oldest));
    oldest->ip = ip;
    oldest->port = port;
    oldest->seen_ns = now ? now : 1;
    return oldest;
}

void online_send(int sock, const HostNetAddr *to, const void *data, uint32_t size) {
    if (!s_enabled) return;
    if (!to) {
        relay_send(sock, ONLINE_BROADCAST, 0, 0, data, size);
        return;
    }
    uint64_t now = hapi_now_ns();
    Peer *p = peer(to->ip, to->port, true, now);
    // Ask the station directly whether it hears this one: the question also opens this side's router for the
    // station's own. The direct way is used while the answers come, so only when it works in both directions.
    if (now - p->probe_ns >= PROBE_NS) {
        p->probe_ns = now;
        uint8_t probe[ONLINE_HEADER];
        put_header(probe, ONLINE_PROBE, 0, 0);
        raw_send(sock, p->ip, p->port, probe, sizeof(probe));
    }
    bool direct = p->direct_ns && now - p->direct_ns < DIRECT_ALIVE_NS;
    if (direct != p->direct) {
        p->direct = direct;
        const uint8_t *b = (const uint8_t *) &p->ip;
        port_log("online: %u.%u.%u.%u port %u is reached %s", b[0], b[1], b[2], b[3], p->port,
                 direct ? "directly" : "through the server");
    }
    if (direct) raw_send(sock, to->ip, to->port, data, size);
    else relay_send(sock, ONLINE_DATA, to->ip, to->port, data, size);
}

static uint32_t get32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t) p[3] << 24; }

int32_t online_receive(int sock, uint8_t *data, int32_t n, HostNetAddr *from) {
    if (!s_enabled || n <= 0) return -1;
    uint64_t now = hapi_now_ns();
    uint32_t sip, sport;
    if (server(&sip, &sport) && from->ip == sip && from->port == sport) {
        if (n < ONLINE_HEADER) return -1;
        if (get32(data) != ONLINE_MAGIC || data[4] != ONLINE_VERSION || get32(data + 12) != s_room) return -1;
        s_answer_ns = now;
        if (data[5] == ONLINE_HELLO) {
            if (!s_answered) {
                s_answered = true;
                port_log("online: connected to the server as %u.%u.%u.%u port %u, %u other(s) in the room", data[8],
                         data[9], data[10], data[11], data[6] | data[7] << 8,
                         n >= ONLINE_HEADER + 2 ? data[16] | data[17] << 8 : 0);
            }
            return -1;
        }
        if (data[5] != ONLINE_DATA && data[5] != ONLINE_BROADCAST) return -1;
        memcpy(&from->ip, data + 8, 4);
        from->port = data[6] | data[7] << 8;
        peer(from->ip, from->port, true, now);
        memmove(data, data + ONLINE_HEADER, (size_t) (n - ONLINE_HEADER));
        return n - ONLINE_HEADER;
    }
    // Directly from a station the server has introduced; anything else is not for us.
    Peer *p = peer(from->ip, from->port, false, now);
    if (!p) return -1;
    if (n >= ONLINE_HEADER && get32(data) == ONLINE_MAGIC) {
        if (data[4] != ONLINE_VERSION || get32(data + 12) != s_room) return -1;
        if (data[5] == ONLINE_PROBE) {
            uint8_t answer[ONLINE_HEADER];
            put_header(answer, ONLINE_PROBE_ANSWER, 0, 0);
            raw_send(sock, from->ip, from->port, answer, sizeof(answer));
        } else if (data[5] == ONLINE_PROBE_ANSWER) {
            p->direct_ns = now;
        }
        return -1;
    }
    return n; // a wireless datagram
}

void online_tick(int sock) {
    uint32_t sip, sport;
    if (!server(&sip, &sport)) return;
    uint64_t now = hapi_now_ns();
    if (s_answered && now - s_answer_ns > SERVER_SILENT_NS) {
        s_answered = false;
        port_log("online: the server does not answer");
    }
    if (s_hello_ns && now - s_hello_ns < (s_answered ? HELLO_NS : HELLO_FIRST_NS)) return;
    s_hello_ns = now;
    relay_send(sock, ONLINE_HELLO, 0, 0, NULL, 0);
}
