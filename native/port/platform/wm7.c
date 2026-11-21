// The ARM7 side of the DS wireless, over UDP on the local network.
//
// The game's wireless library (libs/NitroSDK/wm) runs as on the DS: it sends commands through the PXI FIFO (tag 0xa,
// a pointer to a command buffer) and receives messages from the ARM7 in its FIFO callback. This file plays the ARM7:
// it keeps the status area the library reads, answers the commands and sends the messages the library expects
// (command results, a child connecting or leaving, port data arriving, a port send done).
//
// Instead of radio frames, stations exchange UDP datagrams:
//  - a parent broadcasts a beacon (its game info) ten times a second; scanning children list the parents they hear;
//  - a child joins a parent with its 24 bytes of user data; the parent gives it an AID;
//  - port data goes over one ordered, reliable stream per link (sequence numbers, acknowledgements, resending), which
//    is what the MP protocol guarantees on the DS; a send is done when every destination acknowledged it;
//  - acknowledgements double as keep-alives; a station not heard for a few seconds is disconnected.
// Messages are delivered from the interrupt points of the OS (port_wm7_poll), like the ARM7's FIFO interrupts.
//
// Online play uses the same datagrams: the host side carries them through a relay server instead of the local
// network (hostside/online.h), and a broadcast reaches the stations of the room. Nothing here knows the difference,
// except that the timers follow the measured round trip time, which is no longer a millisecond.
#include "port.h"
#include "hostapi.h"

#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "arm_fifo_messaging.h"
#include "wireless_link_control.h"
#include "wireless_key_data_sharing.h"
#include "wireless_system_commands.h"

// ------------------------------------------------------------------------------------------------ wire format
#define NET_MAGIC 0x57444b4du // "MKDW"
#define NET_VERSION 2 // 2: data sharing over slow links (every station must do it the same way)

enum {
    PKT_BEACON = 1, // parent -> everyone: BeaconBody
    PKT_JOIN,       // child -> parent: JoinBody
    PKT_JOIN_OK,    // parent -> child: aid in the header
    PKT_JOIN_NO,    // parent -> child: result code in the header's aid
    PKT_LEAVE,      // either way
    PKT_DATA,       // reliable stream: DataBody
    PKT_ACK,        // AckBody; also the keep-alive
};

typedef struct NetHeader {
    u32 magic;
    u8 version, type;
    u16 aid;     // the sender's AID on the link (0 = parent), or a value of the packet type
    u32 session; // the parent's session (a new one each time a parent starts)
    u8 mac[6];   // the sender
    u16 length;  // bytes after the header
} NetHeader;

typedef struct BeaconBody {
    u16 channel, beacon_period;
    u16 game_info_length, reserved;
    u8 game_info[0x80]; // WMGameInfo
} BeaconBody;

typedef struct JoinBody {
    u8 user[24];
} JoinBody;

enum { DATA_PORT, DATA_MP_READY, DATA_MP_STOP };

typedef struct DataBody {
    u32 seq;
    u8 kind, port;
    u16 mp_seq; // the MP sequence number the receiver sees (WMPortRecvCallback.seqNo)
    u16 size;
    u16 phase; // where the sender was in its frame when it sent this, in 4 µs units (not on resends)
    u8 data[0x200];
} DataBody;

#define DATA_RESENT 0x80 // in DataBody.kind

typedef struct AckBody {
    u32 next; // every packet before this one arrived
    // Not in the first versions, which send and read only `next`: the data packet this answers, for the round trip
    // time.
    u32 echo_seq;
    u8 echo; // ACK_ECHO_*
    u8 reserved[3];
} AckBody;

#define ACK_SIZE_V1 4
enum { ACK_ECHO_NONE, ACK_ECHO_FIRST, ACK_ECHO_RESENT }; // a keep-alive; answers a first copy; answers a resent copy

#define DATA_HEADER_SIZE (sizeof(DataBody) - 0x200)

// ------------------------------------------------------------------------------------------------ state
#define MAX_LINKS 16      // by AID: 0 = the parent (on a child), 1..15 = children (on a parent)
#define OUT_SLOTS 64      // unacknowledged packets per link
#define MAX_PARENTS 8     // parents heard while scanning
#define MAX_SENDS 64      // port sends waiting for their acknowledgements
#define MAX_HELD 64       // received port data waiting for its port callback
#define RESEND_NS 20000000ull     // on the local network; longer with the round trip time (resend_after)
#define RESEND_MAX_NS 400000000ull
#define KEEPALIVE_NS 100000000ull
#define LINK_TIMEOUT_NS 5000000000ull
#define BEACON_NS 100000000ull
#define PARENT_TIMEOUT_NS 2500000000ull
#define JOIN_RETRY_NS 200000000ull
#define JOIN_TIMEOUT_NS 3000000000ull
#define HELD_TIMEOUT_NS 3000000000ull

#define DS_FIFO 32
#define DS_RECORD_MAX 0x80
typedef struct DsRecord {
    u16 port, mp_seq, size;
    u8 data[DS_RECORD_MAX];
} DsRecord;

typedef struct OutPacket {
    u64 first_ns; // first sent
    bool resent;
    u16 size;
    u8 bytes[sizeof(NetHeader) + sizeof(DataBody)];
} OutPacket;

typedef struct Link {
    bool used;
    HostNetAddr addr;
    u8 mac[6];
    u16 aid;
    u64 heard_ns, sent_ns, resend_ns;
    u32 send_next, send_acked; // packets [send_acked, send_next) are in flight
    OutPacket *out;            // OUT_SLOTS
    u32 recv_next;
    bool mp_ready;  // a child that started MP
    int64_t srtt_ns; // smoothed round trip time, -1 = none yet
    // A parent's data sharing with this child (see "data sharing over slow links").
    struct DsRecord *ds_fifo; // DS_FIFO records the library has no place for yet
    int ds_head, ds_count;
    u64 ds_heard_ns, ds_sent_ns; // the child's last record; the last data set sent to it
    int ds_surplus;              // frames in a row with records waiting
    int ds_out;                  // data sets sent to the child that it has not answered with a record yet
    u8 ds_last[DS_RECORD_MAX];   // the last record given to the library
    u16 ds_last_size;
} Link;

typedef struct Parent { // a parent heard while scanning
    bool used;
    HostNetAddr addr;
    u8 mac[6];
    u32 session;
    u64 heard_ns;
    BeaconBody beacon;
} Parent;

typedef struct Send { // a port send waiting for acknowledgements
    bool used;
    u16 remaining; // destinations that have not acknowledged it
    u32 link_seq[MAX_LINKS];
    u16 dest, sent, port, mp_seq, size;
    u32 data, callback, arg;
} Send;

typedef struct Held { // port data that arrived before its port callback was set
    u64 time_ns;
    u16 port, aid, mp_seq, size;
    u8 mac[6];
    u8 data[0x200];
} Held;

static bool s_net;
static u8 s_mac[6];
static WMStatus *s_status;
static bool s_parent, s_child;
static u32 s_session;
static ParentParameter s_pparam;
static u8 s_user_game_info[0x80];
static u8 s_attr;
static u16 s_mp_seq;
static u64 s_beacon_ns;
static Link s_links[MAX_LINKS];
static Parent s_parents[MAX_PARENTS];
static int s_next_parent;
static Send s_sends[MAX_SENDS];
static Held *s_held; // MAX_HELD, a ring
static int s_held_head, s_held_count;
static u64 s_last_poll_ns;

// Scan in progress.
static bool s_scanning;
static u64 s_scan_due_ns;
static u32 s_scan_buf;

// Join in progress.
static bool s_joining;
static int s_join_parent;
static u64 s_join_start_ns, s_join_sent_ns;
static JoinBody s_join;
static u16 s_join_power_save;

// Messages to the ARM9: a ring of buffers and a queue of the ones to deliver.
#define MSG_SLOTS 64
static u8 s_msg[MSG_SLOTS][0x100] __attribute__((aligned(32)));
static int s_msg_next;
static WirelessMessage *s_queue[MSG_SLOTS];
static int s_queue_head, s_queue_count;
static u8 s_payload[0x200] __attribute__((aligned(32)));

#define U16(m, off) (*(u16 *) ((u8 *) (m) + (off)))
#define U32(m, off) (*(u32 *) ((u8 *) (m) + (off)))
#define PTR(a) ((void *) (uintptr_t) (a))
#define ADDR(p) ((u32) (uintptr_t) (p))

static u64 now_ns(void) { return port_now_ns(); }
// Time from `then` to `now`; 0 when `then` is later (set during this poll).
static u64 since(u64 now, u64 then) { return now > then ? now - then : 0; }

static WirelessMessage *new_message(u16 command, u16 result) {
    u8 *m = s_msg[s_msg_next];
    s_msg_next = (s_msg_next + 1) % MSG_SLOTS;
    memset(m, 0, 0x100);
    U16(m, 0) = command;
    U16(m, 2) = result;
    return (WirelessMessage *) m;
}

#define WM7_TRACE 0
static void queue(WirelessMessage *m) {
    if (s_queue_count == MSG_SLOTS) {
        port_log("wm7: message queue full");
        return;
    }
    s_queue[(s_queue_head + s_queue_count++) % MSG_SLOTS] = m;
    if (WM7_TRACE && U16(m, 0) < 0x80) port_log("wm7: queued %02x (%d) at %d ms", U16(m, 0), s_queue_count, (int) (port_now_ns() / 1000000 % 100000));
}

static void reply(u16 command, u16 result) { queue(new_message(command, result)); }

static void deliver(WirelessMessage *m) {
    if (WM7_TRACE && U16(m, 0) != WIRELESS_REQUEST_PORT_DATA && U16(m, 0) != WIRELESS_REQUEST_PORT_SENT) {
        port_log("wm7: -> cmd %02x result %d state4 %d state8 %d at %d ms", U16(m, 0), U16(m, 2), U16(m, 4), U16(m, 8), (int) (port_now_ns() / 1000000 % 100000));
    }
    void (*fifo)(u32, u32, s32) = (void (*)(u32, u32, s32)) FifoRecvCallbackTable[0xa];
    if (fifo) fifo(0xa, ADDR(m), 0);
}

static void set_state(u16 state) {
    if (s_status) s_status->mState = state;
}

// ------------------------------------------------------------------------------------------------ sending
static void send_packet(const HostNetAddr *to, u8 type, u16 aid, const void *body, u16 size) {
    u8 buf[sizeof(NetHeader) + sizeof(DataBody)];
    NetHeader *h = (NetHeader *) buf;
    h->magic = NET_MAGIC;
    h->version = NET_VERSION;
    h->type = type;
    h->aid = aid;
    h->session = s_session;
    memcpy(h->mac, s_mac, 6);
    h->length = size;
    if (size) memcpy(buf + sizeof(NetHeader), body, size);
    hapi_net_send(to, buf, (u32) (sizeof(NetHeader) + size));
}

static u16 own_aid(void) { return s_status ? s_status->mOwnMemberIndex : 0; }

int64_t port_os_vblank_phase_ns(void);
int64_t port_os_vblank_period_ns(void);
void port_os_shift_vblank(int64_t ns);

// How long a packet may stay unacknowledged before it is sent again: 20 ms on the local network, where the round
// trip takes a millisecond; one and a half round trips when they take longer (online), so that packets that are
// only on their way are not all sent twice.
static u64 resend_after(const Link *l) {
    u64 t = RESEND_NS;
    if (l->srtt_ns > 0) {
        u64 rtt = (u64) l->srtt_ns * 3 / 2 + 5000000ull;
        if (rtt > t) t = rtt;
    }
    return t > RESEND_MAX_NS ? RESEND_MAX_NS : t;
}

static void link_transmit(Link *l, u32 seq, u64 now) {
    OutPacket *p = &l->out[seq % OUT_SLOTS];
    hapi_net_send(&l->addr, p->bytes, p->size);
    l->sent_ns = now;
}

// Queues a reliable packet on the link; false when too many are in flight.
static bool link_push(Link *l, u8 kind, u16 port, u16 mp_seq, const void *data, u16 size, u32 *seq_out) {
    if (l->send_next - l->send_acked >= OUT_SLOTS) return false;
    u32 seq = l->send_next++;
    OutPacket *p = &l->out[seq % OUT_SLOTS];
    NetHeader *h = (NetHeader *) p->bytes;
    DataBody *d = (DataBody *) (p->bytes + sizeof(NetHeader));
    h->magic = NET_MAGIC;
    h->version = NET_VERSION;
    h->type = PKT_DATA;
    h->aid = own_aid();
    h->session = s_session;
    memcpy(h->mac, s_mac, 6);
    h->length = (u16) (DATA_HEADER_SIZE + size);
    d->seq = seq;
    d->kind = kind;
    d->port = (u8) port;
    d->mp_seq = mp_seq;
    d->size = size;
    int64_t phase = port_os_vblank_phase_ns() / 4000;
    d->phase = (u16) (phase < 0 ? 0 : phase > 0xffff ? 0xffff : phase);
    if (size) memcpy(d->data, data, size);
    p->size = (u16) (sizeof(NetHeader) + h->length);
    u64 now = now_ns();
    p->first_ns = now;
    p->resent = false;
    if (l->send_next - l->send_acked == 1) l->resend_ns = now + resend_after(l);
    // Twice: the races exchange inputs every frame in lockstep, and a lost datagram would stall them until it is
    // resent. The receiver keeps the first copy.
    link_transmit(l, seq, now);
    link_transmit(l, seq, now);
    if (seq_out) *seq_out = seq;
    return true;
}

// data: the data packet this acknowledges (NULL for a keep-alive).
static void send_ack(Link *l, const DataBody *data) {
    AckBody a;
    memset(&a, 0, sizeof(a));
    a.next = l->recv_next;
    if (data) {
        a.echo_seq = data->seq;
        a.echo = (data->kind & DATA_RESENT) ? ACK_ECHO_RESENT : ACK_ECHO_FIRST;
    }
    send_packet(&l->addr, PKT_ACK, own_aid(), &a, sizeof(a));
    l->sent_ns = now_ns();
}

// ------------------------------------------------------------------------------------------------ port sends
static void send_done(Send *s, u16 result) {
    WirelessMessage *m = new_message(WIRELESS_REQUEST_PORT_SENT, result);
    U16(m, 4) = 0;
    U16(m, 8) = 20; // WM_STATECODE_PORT_SEND
    U16(m, 0x0a) = s->port;
    U16(m, 0x0c) = s->dest;
    U16(m, 0x0e) = s->remaining;
    U16(m, 0x10) = s->sent;
    U32(m, 0x14) = s->data;
    U16(m, 0x18) = s->size;
    U16(m, 0x1a) = s->mp_seq;
    U32(m, 0x1c) = s->callback; // WmReceiveFifo calls it
    U32(m, 0x20) = s->arg;
    if (s_status) {
        U16(m, 0x24) = s_status->mMpMaxSendSize;
        U16(m, 0x26) = s_status->mMpMaxRecvSize;
    }
    s->used = false;
    queue(m);
}

static void check_sends(void) {
    for (int i = 0; i < MAX_SENDS; i++) {
        Send *s = &s_sends[i];
        if (!s->used) continue;
        for (int a = 0; a < MAX_LINKS; a++) {
            if (!(s->remaining & (1 << a))) continue;
            Link *l = &s_links[a];
            if (!l->used) {
                s->remaining &= (u16) ~(1 << a);
            } else if (l->send_acked > s->link_seq[a]) {
                s->remaining &= (u16) ~(1 << a);
                s->sent |= (u16) (1 << a);
            }
        }
        if (!s->remaining) send_done(s, WIRELESS_RESULT_SUCCEEDED);
    }
}

static void drop_sends(void) {
    for (int i = 0; i < MAX_SENDS; i++) s_sends[i].used = false;
}

// ------------------------------------------------------------------------------------------------ links
static Link *link_open(u16 aid, const HostNetAddr *addr, const u8 *mac) {
    Link *l = &s_links[aid];
    OutPacket *out = l->out;
    DsRecord *fifo = l->ds_fifo;
    memset(l, 0, sizeof(*l));
    l->out = out ? out : malloc(sizeof(OutPacket) * OUT_SLOTS);
    l->ds_fifo = fifo ? fifo : malloc(sizeof(DsRecord) * DS_FIFO);
    l->used = true;
    l->aid = aid;
    l->addr = *addr;
    memcpy(l->mac, mac, 6);
    l->heard_ns = l->sent_ns = now_ns();
    l->srtt_ns = -1;
    return l;
}

static void update_children(void) {
    if (!s_status || !s_parent) return;
    u16 connected = 0, ready = 0;
    for (int a = 1; a < MAX_LINKS; a++) {
        if (!s_links[a].used) continue;
        connected |= (u16) (1 << a);
        if (s_links[a].mp_ready) ready |= (u16) (1 << a);
    }
    s_status->mConnectedChildren = connected;
    s_status->mMpReadyBitmap = ready;
    s_status->mLinkLevel = connected ? 3 : 0;
}

static void child_reset_status(void) {
    if (!s_status) return;
    s_status->mOwnMemberIndex = 0;
    s_status->mMpFlag = 0;
    s_status->mLinkLevel = 0;
    s_status->mConnectedChildren = 0;
    s_status->mMpReadyBitmap = 0;
}

// The link to `aid` ended (left, timed out, or this station disconnected it): tell the ARM9 like the DS does.
static void link_lost(u16 aid, bool notify, u16 state) {
    Link *l = &s_links[aid];
    if (!l->used) return;
    l->used = false;
    check_sends();
    if (s_parent) {
        update_children();
        if (notify) {
            WirelessMessage *m = new_message(WIRELESS_REQUEST_OPEN_HOST, 0);
            U16(m, 8) = state;
            memcpy((u8 *) m + 0x0a, l->mac, 6);
            U16(m, 0x10) = aid;
            queue(m);
        }
        port_log("wm7: child %d left", aid);
    } else if (s_child) {
        s_child = false;
        drop_sends();
        child_reset_status();
        set_state(WIRELESS_UNIT_STATE_POWERED_ON);
        if (notify) {
            WirelessMessage *m = new_message(WIRELESS_REQUEST_ENTER_HOST, 0);
            U16(m, 8) = state;
            memcpy((u8 *) m + 0x0a, l->mac, 6);
            queue(m);
        }
        port_log("wm7: left the parent");
    }
}

static void leave_all(bool notify) {
    for (int a = 0; a < MAX_LINKS; a++) {
        if (!s_links[a].used) continue;
        for (int i = 0; i < 3; i++) send_packet(&s_links[a].addr, PKT_LEAVE, own_aid(), NULL, 0);
        link_lost((u16) a, notify, WIRELESS_STATE_UNLINKED);
    }
}

// ------------------------------------------------------------------------------------------------ the beacon
static u16 game_info(u8 *out) { // WMGameInfo
    WMGameInfo *g = (WMGameInfo *) out;
    memset(out, 0, 0x80);
    g->mMagicNumber = 1;
    g->mVer = 1;
    g->mPlatform = 0;
    g->mGgid = s_pparam.mGgid;
    g->mTgid = s_pparam.mTgid;
    u16 len = s_pparam.mUserGameInfoLength > 0x70 ? 0x70 : s_pparam.mUserGameInfoLength;
    g->mUserGameInfoLength = (u8) len;
    g->mAttribute = s_attr;
    g->mParentMaxSize = s_pparam.mParentMaxSize;
    g->mChildMaxSize = s_pparam.mChildMaxSize;
    memcpy(&g->mUserGameInfo, s_user_game_info, len);
    return (u16) (0x10 + len);
}

static void send_beacon(void) {
    BeaconBody b;
    memset(&b, 0, sizeof(b));
    b.channel = s_pparam.mChannel;
    b.beacon_period = s_pparam.mBeaconPeriod;
    b.game_info_length = game_info(b.game_info);
    send_packet(NULL, PKT_BEACON, 0, &b, sizeof(b));
}

// The WMBssDesc a scan reports for a parent.
static void bss_desc(const Parent *p, WMBssDesc *d) {
    memset(d, 0, 0xc0);
    u16 gl = p->beacon.game_info_length > 0x80 ? 0x80 : p->beacon.game_info_length;
    d->mLength = (u16) ((0x40 + gl + 1) / 2);
    d->mRssi = 40;
    memcpy(d->mBssid, p->mac, 6);
    d->mSsidLength = 0;
    d->mCapaInfo = 0x21;
    d->mRateSet.mBasic = 3;
    d->mRateSet.mSupport = 3;
    d->mBeaconPeriod = p->beacon.beacon_period;
    d->mDtimPeriod = 1;
    d->mChannel = p->beacon.channel;
    d->mGameInfoLength = gl;
    d->mOtherElementCount = 0;
    memcpy(&d->mGameInfo, p->beacon.game_info, gl);
}

// ------------------------------------------------------------------------------------------------ commands
static void net_start(void) {
    // Each time the wireless starts: the host side reads the network settings again (local or online play).
    s_net = hapi_net_open();
    if (!s_held) s_held = malloc(sizeof(Held) * MAX_HELD);
}

static void stop_everything(void) {
    leave_all(false);
    drop_sends();
    s_parent = s_child = false;
    s_scanning = s_joining = false;
    s_held_count = 0;
    if (s_status) {
        child_reset_status();
        s_status->mPwrMgtMode = 0;
    }
}

static void start_scan(u32 scan_buf, u16 time_ms) {
    s_scanning = true;
    s_scan_buf = scan_buf;
    if (time_ms < 10) time_ms = 10;
    if (time_ms > 200) time_ms = 200;
    s_scan_due_ns = now_ns() + (u64) time_ms * 1000000ull;
    set_state(WIRELESS_UNIT_STATE_SCANNING);
}

static void scan_result(void) {
    s_scanning = false;
    u64 now = now_ns();
    for (int i = 0; i < MAX_PARENTS; i++) {
        Parent *p = &s_parents[(s_next_parent + i) % MAX_PARENTS];
        if (!p->used || since(now, p->heard_ns) > PARENT_TIMEOUT_NS) continue;
        s_next_parent = (s_next_parent + i + 1) % MAX_PARENTS;
        WMBssDesc *d = PTR(s_scan_buf);
        if (d) bss_desc(p, d);
        WirelessMessage *m = new_message(WIRELESS_REQUEST_BEGIN_SEARCH, 0);
        U16(m, 8) = WIRELESS_STATE_HOST_SIGHTED;
        memcpy((u8 *) m + 0x0a, p->mac, 6);
        U16(m, 0x10) = p->beacon.channel;
        U16(m, 0x12) = 3; // link level
        U16(m, 0x14) = 0; // SSID length
        u16 gl = p->beacon.game_info_length > 0x80 ? 0x80 : p->beacon.game_info_length;
        U16(m, 0x36) = gl;
        memcpy((u8 *) m + 0x38, p->beacon.game_info, gl > 0xc8 ? 0xc8 : gl);
        queue(m);
        return;
    }
    WirelessMessage *m = new_message(WIRELESS_REQUEST_BEGIN_SEARCH, 0);
    U16(m, 8) = WIRELESS_STATE_SEARCH_EMPTY;
    queue(m);
}

static void join_failed(u16 result) {
    s_joining = false;
    set_state(WIRELESS_UNIT_STATE_POWERED_ON);
    WirelessMessage *m = new_message(WIRELESS_REQUEST_ENTER_HOST, result);
    queue(m);
    port_log("wm7: joining failed (%d)", result);
}

// ------------------------------------------------------------------------------------------------ VBlank sync
// A child keeps its frames in step with the parent's, as MP children do on the DS: half a frame after the parent's.
// The races exchange their inputs every frame (data sharing); with the child half a frame behind, each side's data
// has half a frame to cross the network before the other side needs it, the most room for the Wi-Fi's jitter both
// ways. Each packet from the parent says where the parent was in its frame when it sent it; with half the round
// trip time for the way, the child knows where the parent's VBlank is and nudges its own a little.
static int64_t wrap_phase(int64_t d, int64_t period) {
    while (d >= period / 2) d -= period;
    while (d < -period / 2) d += period;
    return d;
}

static bool link_slow(const Link *l);
static WMDataSharingInfo *ds_parent(u16 port);

static void sync_to_parent(const Link *l, const DataBody *d) {
    if (l->srtt_ns < 0 || link_slow(l)) return; // a slow link: ds_child_pace sets the pace
    int64_t period = port_os_vblank_period_ns();
    int64_t parent_phase = (int64_t) d->phase * 4000 + l->srtt_ns / 2; // the parent's phase now
    int64_t err = wrap_phase(parent_phase - port_os_vblank_phase_ns() - period / 2, period);
    // err > 0: the parent is further into its frame than half a frame ahead: this VBlank comes too late.
    int64_t shift = -err / 8;
    if (shift > 150000) shift = 150000;
    if (shift < -150000) shift = -150000;
    port_os_shift_vblank(shift);
}

static void port_send(const u32 *p) {
    u32 data = p[0], size = p[1] & 0xffff, dest = p[2] & 0xffff, port = p[3] & 0xffff;
    u32 callback = p[5], arg = p[6];
    Send *s = NULL;
    for (int i = 0; i < MAX_SENDS; i++) {
        if (!s_sends[i].used) {
            s = &s_sends[i];
            break;
        }
    }
    s_mp_seq = (u16) (s_mp_seq + 2);
    Send tmp;
    if (!s) s = &tmp;
    memset(s, 0, sizeof(*s));
    s->used = true;
    s->dest = (u16) dest;
    s->port = (u16) port;
    s->mp_seq = s_mp_seq;
    s->size = (u16) size;
    s->data = data;
    s->callback = callback;
    s->arg = arg;
    if (s == &tmp || size > 0x200) {
        send_done(s, s == &tmp ? WIRELESS_RESULT_SEND_BACKLOG : WIRELESS_RESULT_BAD_INPUT);
        return;
    }
    u16 targets = s_parent ? (u16) (dest & 0xfffe) : (u16) (dest & 1), counted = 0;
    for (int a = 0; a < MAX_LINKS; a++) {
        if (!(targets & (1 << a)) || !s_links[a].used) continue;
        if (!link_push(&s_links[a], DATA_PORT, (u16) port, s_mp_seq, PTR(data), (u16) size, &s->link_seq[a])) {
            // Too much in flight on this link: the sender retries (data sharing does on SEND_BACKLOG).
            for (int b = 0; b < a; b++) { // the set is not on its way to the links before this one either
                if ((counted & (1 << b)) && s_links[b].ds_out > 0) s_links[b].ds_out--;
            }
            s->remaining = 0;
            send_done(s, WIRELESS_RESULT_SEND_BACKLOG);
            return;
        }
        s->sent |= (u16) (1 << a);
        if (s_parent && ds_parent((u16) port)) { // for the data sharing over slow links: a set on its way to this child
            Link *l = &s_links[a];
            u64 now = now_ns();
            if (since(now, l->ds_sent_ns) > 1000000000ull) { // a new start
                l->ds_heard_ns = now;
                l->ds_out = 0;
                l->ds_count = 0;
            }
            l->ds_sent_ns = now;
            l->ds_out++;
            counted |= (u16) (1 << a);
        }
    }
    // Done at once, as an MP send is within its MP cycle on the DS: delivery is certain (resent until acknowledged),
    // and the data sharing reads its own set only after the send is done, which waiting for the acknowledgement
    // would delay by a round trip.
    send_done(s, WIRELESS_RESULT_SUCCEEDED);
}

void port_wm7_command(u32 address) {
    u8 *buf = PTR(address);
    u16 id = U16(buf, 0) & 0x7fff;
    const u32 *p = (const u32 *) (buf + 4);
    WirelessMessage *m;
    if (WM7_TRACE && id != WIRELESS_REQUEST_QUEUE_EXCHANGE_DATA) {
        port_log("wm7: <- cmd %02x %08x %08x %08x", id, p[0], p[1], p[2]);
    }
    switch (id) {
    case WIRELESS_REQUEST_POWER_UP_STACK: // WM_Initialize: enable and power on
    case WIRELESS_REQUEST_ACTIVATE:       // WM_Enable
        s_status = PTR(p[1]);
        memset(s_status, 0, sizeof(WMStatus));
        hapi_net_mac(s_mac);
        net_start();
        set_state(id == WIRELESS_REQUEST_ACTIVATE ? WIRELESS_UNIT_STATE_ENABLED : WIRELESS_UNIT_STATE_POWERED_ON);
        reply(id, 0);
        break;
    case WIRELESS_REQUEST_RESTART: // WM_Reset
        stop_everything();
        set_state(WIRELESS_UNIT_STATE_POWERED_ON);
        reply(id, 0);
        break;
    case WIRELESS_REQUEST_CLOSE_STACK: // WM_End: powered off and disabled
        stop_everything();
        set_state(WIRELESS_UNIT_STATE_DISABLED);
        reply(id, 0);
        break;
    case WIRELESS_REQUEST_DEACTIVATE:
        set_state(WIRELESS_UNIT_STATE_DISABLED);
        reply(id, 0);
        break;
    case WIRELESS_REQUEST_RADIO_ON:
        set_state(WIRELESS_UNIT_STATE_POWERED_ON);
        reply(id, 0);
        break;
    case WIRELESS_REQUEST_RADIO_OFF:
        set_state(WIRELESS_UNIT_STATE_ENABLED);
        reply(id, 0);
        break;
    case WIRELESS_REQUEST_CONFIGURE_HOST: { // WM_SetParentParameter
        const ParentParameter *pp = PTR(p[0]);
        s_pparam = *pp;
        memset(s_user_game_info, 0, sizeof(s_user_game_info));
        if (pp->mUserGameInfo && pp->mUserGameInfoLength) {
            memcpy(s_user_game_info, pp->mUserGameInfo, pp->mUserGameInfoLength > 0x70 ? 0x70 : pp->mUserGameInfoLength);
        }
        s_attr = (u8) ((pp->mEntryFlag ? WIRELESS_ATTRIBUTE_ENTRY : 0) | (pp->mMultiBootFlag ? WIRELESS_ATTRIBUTE_MULTIBOOT : 0) |
                       (pp->mKsFlag ? WIRELESS_ATTRIBUTE_KS_FLAG : 0) | (pp->mCsFlag ? WIRELESS_ATTRIBUTE_CS_FLAG : 0));
        if (s_status) s_status->mMaxEntry = pp->mMaxEntry;
        reply(id, 0);
        break;
    }
    case WIRELESS_REQUEST_OPEN_HOST: // WM_StartParent
        s_parent = true;
        s_child = false;
        s_session = (u32) (now_ns() ^ ((u32) s_mac[5] << 24) ^ ((u32) s_mac[4] << 16)) | 1;
        s_mp_seq = 0;
        if (s_status) {
            s_status->mOwnMemberIndex = 0;
            s_status->mPwrMgtMode = 0;
            s_status->mMpMaxSendSize = (u16) (s_pparam.mParentMaxSize + (s_pparam.mKsFlag ? 0x2a : 0) + 4);
            s_status->mMpMaxRecvSize = (u16) (s_pparam.mChildMaxSize + (s_pparam.mKsFlag ? 6 : 0) + 2);
            s_status->mMaxEntry = s_pparam.mMaxEntry;
        }
        update_children();
        set_state(WIRELESS_UNIT_STATE_PARENT_STARTED);
        s_beacon_ns = 0;
        m = new_message(id, 0);
        U16(m, 8) = WIRELESS_STATE_SESSION_START;
        queue(m);
        port_log("wm7: parent started (ggid %x tgid %x)", s_pparam.mGgid, s_pparam.mTgid);
        break;
    case WIRELESS_REQUEST_CLOSE_HOST: // WM_EndParent
        leave_all(false);
        drop_sends();
        s_parent = false;
        if (s_status) s_status->mMpFlag = 0;
        update_children();
        set_state(WIRELESS_UNIT_STATE_POWERED_ON);
        reply(id, 0);
        break;
    case WIRELESS_REQUEST_BEGIN_SEARCH: // WM_StartScan
        start_scan(U32(buf, 4), U16(buf, 8));
        break;
    case WIRELESS_REQUEST_BEGIN_SEARCH_CHANNELS: // WM_StartScanEx
        start_scan(U32(buf, 4), U16(buf, 0x0a));
        break;
    case WIRELESS_REQUEST_STOP_SEARCH: // WM_EndScan
        s_scanning = false;
        set_state(WIRELESS_UNIT_STATE_POWERED_ON);
        reply(id, 0);
        break;
    case WIRELESS_REQUEST_ENTER_HOST: { // WM_StartConnectEx
        const WMBssDesc *d = PTR(U32(buf, 4));
        int found = -1;
        for (int i = 0; d && i < MAX_PARENTS; i++) {
            if (s_parents[i].used && !memcmp(s_parents[i].mac, d->mBssid, 6)) found = i;
        }
        memcpy(s_join.user, buf + 8, 24);
        s_join_power_save = (u16) U32(buf, 0x20);
        if (found < 0) {
            join_failed(9); // timeout: no such parent
            break;
        }
        s_joining = true;
        s_join_parent = found;
        s_session = s_parents[found].session; // the join names the parent's session
        s_join_start_ns = now_ns();
        s_join_sent_ns = 0;
        port_log("wm7: joining %02x:%02x:%02x", d->mBssid[3], d->mBssid[4], d->mBssid[5]);
        break;
    }
    case WIRELESS_REQUEST_LEAVE: { // WM_Disconnect, WM_DisconnectChildren
        u16 bitmap = (u16) p[0], done = 0;
        if (s_parent) {
            for (int a = 1; a < MAX_LINKS; a++) {
                if (!(bitmap & (1 << a)) || !s_links[a].used) continue;
                for (int i = 0; i < 3; i++) send_packet(&s_links[a].addr, PKT_LEAVE, 0, NULL, 0);
                link_lost((u16) a, true, 26); // WM_STATECODE_DISCONNECTED_FROM_MYSELF
                done |= (u16) (1 << a);
            }
        } else if (s_child && s_links[0].used) {
            for (int i = 0; i < 3; i++) send_packet(&s_links[0].addr, PKT_LEAVE, own_aid(), NULL, 0);
            link_lost(0, false, 0);
            done = 1;
        }
        m = new_message(id, 0);
        U16(m, 8) = bitmap;
        U16(m, 0x0a) = done;
        queue(m);
        break;
    }
    case WIRELESS_REQUEST_BEGIN_EXCHANGE: // WM_StartMPEx
        if (s_status) {
            s_status->mMpFlag = 1;
            s_status->mMpRecvBufSize = (u16) (p[1] * 2);
            s_status->mMpSendBuf = PTR(p[2]);
        }
        set_state(s_parent ? WIRELESS_UNIT_STATE_PARENT_WITH_MP : WIRELESS_UNIT_STATE_CHILD_WITH_MP);
        if (s_child && s_links[0].used) link_push(&s_links[0], DATA_MP_READY, 0, 0, NULL, 0, NULL);
        m = new_message(id, 0);
        U16(m, 4) = WIRELESS_STATE_EXCHANGE_BEGUN;
        queue(m);
        break;
    case WIRELESS_REQUEST_QUEUE_EXCHANGE_DATA: // WM_SetMPDataToPortEx: the answer is the port-sent message
        port_send(p);
        break;
    case WIRELESS_REQUEST_STOP_EXCHANGE: // WM_EndMP
        if (s_status) s_status->mMpFlag = 0;
        if (s_child && s_links[0].used) link_push(&s_links[0], DATA_MP_STOP, 0, 0, NULL, 0, NULL);
        drop_sends();
        set_state(s_parent ? WIRELESS_UNIT_STATE_PARENT_STARTED
                           : s_child ? WIRELESS_UNIT_STATE_CHILD_CONNECTED : WIRELESS_UNIT_STATE_POWERED_ON);
        reply(id, 0);
        break;
    case WIRELESS_REQUEST_SET_HOST_ADVERT: // WM_SetGameInfo
        memset(s_user_game_info, 0, sizeof(s_user_game_info));
        memcpy(s_user_game_info, PTR(p[0]), (p[1] & 0xffff) > 0x70 ? 0x70 : (p[1] & 0xffff));
        s_pparam.mUserGameInfoLength = (u16) ((p[1] & 0xffff) > 0x70 ? 0x70 : p[1]);
        s_pparam.mGgid = p[2];
        s_pparam.mTgid = (u16) p[3];
        s_attr = (u8) p[4];
        s_beacon_ns = 0; // at once
        reply(id, 0);
        break;
    case WIRELESS_REQUEST_SET_JOIN_ALLOWED: // WM_SetEntry
        s_attr = (u8) ((s_attr & ~WIRELESS_ATTRIBUTE_ENTRY) | (p[0] ? WIRELESS_ATTRIBUTE_ENTRY : 0));
        s_beacon_ns = 0;
        reply(id, 0);
        break;
    case WIRELESS_REQUEST_MEASURE_CHANNEL: // WM_MeasureChannel: every channel is free
        m = new_message(id, 0);
        U16(m, 8) = U16(buf, 6); // channel
        U16(m, 0x0a) = 0;        // busy ratio
        queue(m);
        break;
    default: // life times, beacon indication, MP frequency, keys, DCF: accepted
        if (id != WIRELESS_REQUEST_SET_TIMEOUT && id != WIRELESS_REQUEST_SET_EXCHANGE_RATE &&
            id != WIRELESS_REQUEST_WATCH_BEACONS) {
            port_log("wm7: command %d accepted without effect", id);
        }
        reply(id, 0);
        break;
    }
    U16(buf, 0) |= WIRELESS_FREE_FLAG;
}

// ------------------------------------------------------------------------------------------------ receiving
static bool port_has_callback(u16 port) {
    WMArm9Buf *w = WMi_GetSystemWork();
    return w && s_status && s_status->mMpFlag && w->mPortCallbacks[port & 15];
}

// Whether the port's callback can take data now. A child's data sharing keeps at most three unread data sets (a
// ring of four): one more would overwrite one. On the DS no more than two are ever on their way; over a slow link
// more are (see below), and they wait here.
static bool port_ready(u16 port) {
    if (!port_has_callback(port)) return false;
    WMArm9Buf *w = WMi_GetSystemWork();
    if (w->mPortCallbacks[port & 15] == (WirelessPortCallback) WmDataSharingReceiveCallback_Child) {
        const WMDataSharingInfo *ds = w->mPortCallbackArgs[port & 15];
        if (ds && ((ds->mWriteIndex - ds->mReadIndex) & 3) >= 3) return false;
    }
    return true;
}

static void deliver_port_data(u16 port, u16 aid, const u8 *mac, u16 mp_seq, const u8 *data, u16 size) {
    memcpy(s_payload, data, size);
    WirelessMessage *m = new_message(WIRELESS_REQUEST_PORT_DATA, 0);
    U16(m, 4) = WIRELESS_STATE_PORT_DATA_ARRIVED;
    U16(m, 6) = port;
    U32(m, 8) = ADDR(s_payload);
    U32(m, 0x0c) = ADDR(s_payload);
    U16(m, 0x10) = size;
    U16(m, 0x12) = aid;
    memcpy((u8 *) m + 0x14, mac, 6);
    U16(m, 0x1a) = mp_seq;
    U16(m, 0x20) = own_aid();
    U16(m, 0x22) = s_status ? (u16) (s_status->mConnectedChildren | 1) : 1;
    deliver(m);
}

// ------------------------------------------------------------------------------------------------ data sharing over slow links
// The game's races and menus run on the library's data sharing (wireless_key_data_sharing.c): every frame each child
// sends its record (keys, touch) to the parent, the parent puts the records into a data set and sends the set to
// everyone, and all stations play the frame from that set. A child sends a record only when it has read a set, and
// the parent sends a set only when every record for it is there: two sets are on their way at most, so a frame takes
// half a round trip. On the DS and on a local network the round trip is a millisecond. Online it is many frames, and
// the game would run in slow motion.
//
// So over a slow link more sets are put on their way, without changing what the library and the game do with them:
//  - Parent: when it has no set left to play and a child's record for the next one has not come by the end of the
//    frame, the set goes out without that record. The library and the game know such sets (a station that missed a
//    frame): every station skips that frame, the same way, and nothing is lost: the record comes later and goes into
//    a later set. Each such set is one more on its way; after the first few the child's records arrive one per
//    frame, each a constant number of frames late, and the game runs at full speed with the child's input delayed
//    by the round trip. A late packet costs one more skipped frame and one more frame of delay.
//  - Parent: records that arrive when the library has no set waiting for them are kept (the library would drop
//    them) and handed over as sets open. When records keep waiting, the child's delay is longer than the link needs:
//    one record that repeats the one before it is taken out, which shortens the delay by a frame.
//  - Parent: a child that has stopped answering (loading, in the background) is waited for, as on the DS, once
//    nothing has come from it for a while; and so is a child that reads the sets slower than they are sent (a slower
//    device), once more sets are on their way to it than the link's delay accounts for. Otherwise the sets would
//    pile up at the child, which would fall further and further behind.
//  - Child: sets that arrive faster than it reads them wait (port_ready); when they pile up it runs slightly faster
//    to catch up, and slightly slower when it runs dry.
// Only where the game is known to skip a frame for a missing record: its own data sharing, on port 12. A skipped
// frame's records are not played, and one byte of the game's record is a message that is sent once (the menus'
// commands): a set that holds one is never sent short, and a record that holds one is never taken out.
#define DS_SLOW_NS 8000000          // round trip time from which a link is slow
#define DS_GAME_PORT 12
#define DS_GAME_RECORD 8            // bytes in the game's record
#define DS_GAME_MESSAGE 5           // offset of the byte that is sent once (SharingSendRecord.mSendByte)
#define DS_DEADLINE_PERCENT 85      // of the frame: when the parent stops waiting for records
#define DS_OUT_MAX 30               // sets on their way to a child at most (they wait in its queue of MAX_HELD)
#define DS_SURPLUS_FRAMES 120       // records waiting for this many frames: one frame of delay is taken out
#define DS_SURPLUS_FRAMES_DEEP 15   // the same with three or more waiting

static bool link_slow(const Link *l) { return l->srtt_ns >= DS_SLOW_NS; }

// The data sharing a parent runs on the port, or NULL.
static WMDataSharingInfo *ds_parent(u16 port) {
    WMArm9Buf *w = WMi_GetSystemWork();
    if (!w || !s_parent || !s_status || !s_status->mMpFlag) return NULL;
    if (w->mPortCallbacks[port & 15] != (WirelessPortCallback) WmDataSharingReceiveCallback_Parent) return NULL;
    WMDataSharingInfo *ds = w->mPortCallbackArgs[port & 15];
    return ds && ds->mMemberMask ? ds : NULL;
}

// Whether a set is waiting for the record of station aid (WmDataSharingReceiveData takes it; else it drops it).
static bool ds_awaits(const WMDataSharingInfo *ds, u16 aid) {
    u16 mask = (u16) (1 << aid), w = ds->mWriteIndex & 3;
    if (ds->mDs[w].mAidBitmap & mask) return true;
    return ds->mDoubleMode == 1 && (ds->mDs[(w + 1) & 3].mAidBitmap & mask);
}

// Hands the child's waiting records to the library, as far as it has sets for them.
static void ds_pump(Link *l) {
    while (l->ds_count) {
        DsRecord *r = &l->ds_fifo[l->ds_head];
        WMDataSharingInfo *ds = ds_parent(r->port);
        if (ds && !ds_awaits(ds, l->aid)) return;
        l->ds_head = (l->ds_head + 1) % DS_FIFO;
        l->ds_count--;
        if (!ds && !port_has_callback(r->port)) continue; // the data sharing has ended
        memcpy(l->ds_last, r->data, r->size);
        l->ds_last_size = r->size;
        deliver_port_data(r->port, l->aid, l->mac, r->mp_seq, r->data, r->size);
    }
}

// Port data for the ARM9: a child's record for a parent's data sharing goes through the link's queue.
static void route_port_data(u16 port, u16 aid, const u8 *mac, u16 mp_seq, const u8 *data, u16 size) {
    Link *l = aid < MAX_LINKS ? &s_links[aid] : NULL;
    if (!l || !l->used || !l->ds_fifo || size > DS_RECORD_MAX || !ds_parent(port)) {
        deliver_port_data(port, aid, mac, mp_seq, data, size);
        return;
    }
    l->ds_heard_ns = now_ns();
    if (l->ds_out > 0) l->ds_out--; // the child sends one record for each set it reads
    if (l->ds_count == DS_FIFO) { // cannot happen while the parent stops for a silent child; keep the newest
        l->ds_head = (l->ds_head + 1) % DS_FIFO;
        l->ds_count--;
    }
    DsRecord *r = &l->ds_fifo[(l->ds_head + l->ds_count++) % DS_FIFO];
    r->port = port;
    r->mp_seq = mp_seq;
    r->size = size;
    memcpy(r->data, data, size);
    ds_pump(l);
}

// Takes one frame out of the child's delay when its records have kept waiting: a record equal to the one before it
// (the same keys held) is dropped, which the game cannot tell from the child having sent one record less.
static void ds_trim(Link *l) {
    if (!link_slow(l) || !l->ds_count) {
        l->ds_surplus = 0;
        return;
    }
    if (++l->ds_surplus < (l->ds_count >= 3 ? DS_SURPLUS_FRAMES_DEEP : DS_SURPLUS_FRAMES)) return;
    const u8 *before = l->ds_last;
    u16 before_size = l->ds_last_size;
    for (int i = 0; i < l->ds_count; i++) {
        DsRecord *r = &l->ds_fifo[(l->ds_head + i) % DS_FIFO];
        bool message = r->size == DS_GAME_RECORD && r->data[DS_GAME_MESSAGE];
        if (!message && r->size == before_size && !memcmp(r->data, before, r->size)) {
            for (int j = i; j + 1 < l->ds_count; j++) {
                l->ds_fifo[(l->ds_head + j) % DS_FIFO] = l->ds_fifo[(l->ds_head + j + 1) % DS_FIFO];
            }
            l->ds_count--;
            l->ds_surplus = 0;
            return;
        }
        before = r->data;
        before_size = r->size;
    }
}

// Parent, once a frame, shortly before its end.
static void ds_parent_deadline(u64 now) {
    for (int a = 1; a < MAX_LINKS; a++) {
        if (!s_links[a].used) continue;
        ds_pump(&s_links[a]);
        ds_trim(&s_links[a]);
    }
    WMDataSharingInfo *ds = ds_parent(DS_GAME_PORT);
    if (!ds || ds->mStatus != DATA_SHARING_STATUS_RUNNING) return;
    if (ds->mReadIndex != ds->mSendIndex) return; // a set is ready for the next frame
    u16 w = ds->mWriteIndex & 3, waiting = ds->mDs[w].mAidBitmap;
    if ((waiting & 1) || !waiting) return; // the parent's own record is not in yet; or nothing is missing
    for (int a = 1; a < MAX_LINKS; a++) {
        if (!(waiting & (1 << a))) continue;
        const Link *l = &s_links[a];
        if (!l->used || !link_slow(l)) return; // a local child: wait for it, as on the DS
        // A child that has gone silent is waited for: it would not read the sets sent meanwhile.
        u64 window = (u64) l->srtt_ns * 2 + 200000000ull;
        if (window > 700000000ull) window = 700000000ull;
        if (since(now, l->ds_heard_ns) > window) return;
        // Nor is a child that reads the sets slower than they are sent (a slower device): no more sets on their way
        // than the link's delay accounts for, with some room. The game then runs at that child's speed, as on the DS.
        int64_t room = 4 + l->srtt_ns * 3 / 2 / port_os_vblank_period_ns();
        if (l->ds_out >= (room > DS_OUT_MAX ? DS_OUT_MAX : room)) return;
    }
    if (ds->mMemberDataSize != DS_GAME_RECORD) return;
    u16 arrived = ds->mDs[w].mArrivedMembers;
    for (int a = 0; a < MAX_LINKS; a++) { // a message in the set: it must be played, wait for everyone
        if (!(arrived & (1 << a))) continue;
        if (WmGetSharedDataAddress(ds, ds->mMemberMask, ds->mDs[w].mData, (u32) a)[DS_GAME_MESSAGE]) return;
    }
    // The set goes without the missing records: not waited for any more, and not marked as arrived.
    ds->mDs[w].mAidBitmap = 0;
    WmDataSharingSendDataSet(ds, 0);
}

// Child, once a frame: keeps the sets waiting to be read near one.
static void ds_child_pace(void) {
    const Link *l = &s_links[0];
    if (!l->used || !link_slow(l)) return;
    WMArm9Buf *w = WMi_GetSystemWork();
    if (!w || !s_status || !s_status->mMpFlag) return;
    for (int port = 0; port < 16; port++) {
        if (w->mPortCallbacks[port] != (WirelessPortCallback) WmDataSharingReceiveCallback_Child) continue;
        const WMDataSharingInfo *ds = w->mPortCallbackArgs[port];
        if (!ds) continue;
        int unread = ((ds->mWriteIndex - ds->mReadIndex) & 3) + s_held_count;
        if (unread >= 3) port_os_shift_vblank(-250000); // 1.5 % faster
        else if (unread == 0) port_os_shift_vblank(80000);
        return;
    }
}

// Once per frame of this console, late in the frame; or, when the game was busy then, as soon as it lets this run
// (which is before it steps the data sharing for the next frame).
static void ds_frame(u64 now) {
    static u64 s_due_ns;
    if (now < s_due_ns) return;
    int64_t phase = port_os_vblank_phase_ns(), period = port_os_vblank_period_ns();
    if (phase < 0) phase = 0;
    u64 due = now - (u64) phase + (u64) (period * DS_DEADLINE_PERCENT / 100); // this frame's
    s_due_ns = now < due ? due : due + (u64) period;
    if (s_due_ns <= now) s_due_ns = now + (u64) period; // no VBlanks (paused)
    if (s_parent) ds_parent_deadline(now);
    else if (s_child) ds_child_pace();
}

// Port data that came early waits for its port callback (the receiver sets it once its MP has started).
static void deliver_held(void) {
    u64 now = now_ns();
    while (s_held_count) {
        Held *h = &s_held[s_held_head];
        if (!port_ready(h->port)) {
            // No callback yet: wait a while for it. A callback that is full: wait for it.
            if (port_has_callback(h->port) || since(now, h->time_ns) < HELD_TIMEOUT_NS) return;
        } else {
            route_port_data(h->port, h->aid, h->mac, h->mp_seq, h->data, h->size);
        }
        s_held_head = (s_held_head + 1) % MAX_HELD;
        s_held_count--;
    }
}

static void receive_data(Link *l, const DataBody *d) {
    switch (d->kind & ~DATA_RESENT) {
    case DATA_MP_READY:
    case DATA_MP_STOP:
        l->mp_ready = d->kind == DATA_MP_READY;
        update_children();
        if (l->mp_ready && s_parent) {
            // Like the DS, a child that starts MP is "connected" again for the ports (data sharing sends it the sets).
        }
        break;
    case DATA_PORT: {
        u16 size = d->size > 0x200 ? 0x200 : d->size;
        if (!s_held_count && port_ready(d->port)) {
            route_port_data(d->port, l->aid, l->mac, d->mp_seq, d->data, size);
        } else if (s_held_count < MAX_HELD) {
            Held *h = &s_held[(s_held_head + s_held_count++) % MAX_HELD];
            h->time_ns = now_ns();
            h->port = d->port;
            h->aid = l->aid;
            h->mp_seq = d->mp_seq;
            h->size = size;
            memcpy(h->mac, l->mac, 6);
            memcpy(h->data, d->data, size);
        }
        break;
    }
    }
}

static Link *link_of(const NetHeader *h, const HostNetAddr *from) {
    if (s_parent) {
        if (h->aid < 1 || h->aid >= MAX_LINKS || h->session != s_session) return NULL;
    } else if (s_child) {
        if (h->aid != 0 || h->session != s_session) return NULL;
    } else {
        return NULL;
    }
    Link *l = &s_links[h->aid];
    if (!l->used || memcmp(l->mac, h->mac, 6)) return NULL;
    l->addr = *from; // follows an address change
    return l;
}

static void on_join(const NetHeader *h, const JoinBody *j, const HostNetAddr *from) {
    if (!s_parent || h->session != s_session) {
        send_packet(from, PKT_JOIN_NO, WIRELESS_RESULT_REFUSED, NULL, 0);
        return;
    }
    for (int a = 1; a < MAX_LINKS; a++) { // a repeated request: answer again
        if (s_links[a].used && !memcmp(s_links[a].mac, h->mac, 6)) {
            s_links[a].addr = *from;
            send_packet(from, PKT_JOIN_OK, (u16) a, NULL, 0);
            return;
        }
    }
    if (!(s_attr & WIRELESS_ATTRIBUTE_ENTRY)) {
        send_packet(from, PKT_JOIN_NO, WIRELESS_RESULT_NOT_LISTED, NULL, 0);
        return;
    }
    int aid = 0;
    u16 max = s_pparam.mMaxEntry ? s_pparam.mMaxEntry : 15;
    for (int a = 1; a <= max && a < MAX_LINKS; a++) {
        if (!s_links[a].used) {
            aid = a;
            break;
        }
    }
    if (!aid) {
        send_packet(from, PKT_JOIN_NO, WIRELESS_RESULT_LIST_FULL, NULL, 0);
        return;
    }
    link_open((u16) aid, from, h->mac);
    update_children();
    send_packet(from, PKT_JOIN_OK, (u16) aid, NULL, 0);
    WirelessMessage *m = new_message(WIRELESS_REQUEST_OPEN_HOST, 0);
    U16(m, 8) = WIRELESS_STATE_LINKED;
    memcpy((u8 *) m + 0x0a, h->mac, 6);
    U16(m, 0x10) = (u16) aid;
    memcpy((u8 *) m + 0x14, j->user, 24);
    queue(m);
    port_log("wm7: child %d joined", aid);
}

static void on_join_ok(const NetHeader *h, const HostNetAddr *from) {
    if (!s_joining) return;
    Parent *p = &s_parents[s_join_parent];
    if (h->session != p->session || memcmp(h->mac, p->mac, 6)) return;
    s_joining = false;
    u16 aid = h->aid;
    s_child = true;
    s_parent = false;
    s_session = p->session;
    s_mp_seq = 0;
    link_open(0, from, p->mac);
    const WMGameInfo *g = (const WMGameInfo *) p->beacon.game_info;
    if (s_status) {
        s_status->mOwnMemberIndex = aid;
        s_status->mPwrMgtMode = s_join_power_save;
        bool ks = (g->mAttribute & WIRELESS_ATTRIBUTE_KS_FLAG) != 0;
        s_status->mMpMaxSendSize = (u16) (g->mChildMaxSize + (ks ? 6 : 0) + 2);
        s_status->mMpMaxRecvSize = (u16) (g->mParentMaxSize + (ks ? 0x2a : 0) + 4);
        s_status->mConnectedChildren = 0;
        s_status->mLinkLevel = 3;
        s_status->mMpFlag = 0;
    }
    set_state(WIRELESS_UNIT_STATE_CHILD_CONNECTED);
    WirelessMessage *m = new_message(WIRELESS_REQUEST_ENTER_HOST, 0);
    U16(m, 8) = WIRELESS_STATE_LINKED;
    U16(m, 0x0a) = aid; // read as the own AID by the game
    U16(m, 0x10) = aid; // read as the own AID by WmReceiveFifo
    queue(m);
    port_log("wm7: joined as child %d", aid);
}

static void receive(const u8 *buf, int32_t n, const HostNetAddr *from) {
    if (n < (int32_t) sizeof(NetHeader)) return;
    const NetHeader *h = (const NetHeader *) buf;
    if (h->magic != NET_MAGIC || h->version != NET_VERSION || h->length > n - (int32_t) sizeof(NetHeader)) return;
    if (!memcmp(h->mac, s_mac, 6)) return; // our own beacon
    const u8 *body = buf + sizeof(NetHeader);
    u64 now = now_ns();
    switch (h->type) {
    case PKT_BEACON: {
        if (h->length < sizeof(BeaconBody)) return;
        int slot = -1, oldest = 0;
        for (int i = 0; i < MAX_PARENTS; i++) {
            if (s_parents[i].used && !memcmp(s_parents[i].mac, h->mac, 6)) {
                slot = i;
                break;
            }
            if (!s_parents[i].used || s_parents[i].heard_ns < s_parents[oldest].heard_ns) oldest = i;
        }
        if (slot < 0) {
            if (s_joining && s_join_parent == oldest) return;
            slot = oldest;
        }
        Parent *p = &s_parents[slot];
        p->used = true;
        p->addr = *from;
        memcpy(p->mac, h->mac, 6);
        p->session = h->session;
        p->heard_ns = now;
        memcpy(&p->beacon, body, sizeof(BeaconBody));
        break;
    }
    case PKT_JOIN:
        if (h->length >= sizeof(JoinBody)) on_join(h, (const JoinBody *) body, from);
        break;
    case PKT_JOIN_OK:
        on_join_ok(h, from);
        break;
    case PKT_JOIN_NO:
        if (s_joining && h->session == s_parents[s_join_parent].session) join_failed(h->aid);
        break;
    case PKT_LEAVE: {
        Link *l = link_of(h, from);
        if (l) link_lost(l->aid, true, WIRELESS_STATE_UNLINKED);
        break;
    }
    case PKT_ACK: {
        Link *l = link_of(h, from);
        if (!l || h->length < ACK_SIZE_V1) return;
        l->heard_ns = now;
        AckBody ack;
        memset(&ack, 0, sizeof(ack));
        memcpy(&ack, body, h->length < sizeof(ack) ? h->length : sizeof(ack));
        u32 next = ack.next;
        // The round trip time. An acknowledgement that names the copy it answers gives it even when the packet has
        // been resent meanwhile (always, once the round trip is longer than the resend time); one from an older
        // version only counts for a packet that was sent once.
        int64_t rtt = -1;
        if (h->length >= sizeof(AckBody)) {
            if (ack.echo == ACK_ECHO_FIRST && ack.echo_seq < l->send_next && l->send_next - ack.echo_seq <= OUT_SLOTS) {
                rtt = (int64_t) since(now, l->out[ack.echo_seq % OUT_SLOTS].first_ns);
            }
        } else if (next > l->send_acked && next <= l->send_next && !l->out[(next - 1) % OUT_SLOTS].resent) {
            rtt = (int64_t) since(now, l->out[(next - 1) % OUT_SLOTS].first_ns);
        }
        if (rtt >= 0) l->srtt_ns = l->srtt_ns < 0 ? rtt : l->srtt_ns + (rtt - l->srtt_ns) / 8;
        if (next > l->send_acked && next <= l->send_next) {
            l->send_acked = next;
            l->resend_ns = now + resend_after(l);
            check_sends();
        }
        break;
    }
    case PKT_DATA: {
        Link *l = link_of(h, from);
        if (!l || h->length < DATA_HEADER_SIZE) return;
        l->heard_ns = now;
        const DataBody *d = (const DataBody *) body;
        if (d->seq == l->recv_next) {
            l->recv_next++;
            if (s_child && l->aid == 0 && !(d->kind & DATA_RESENT)) sync_to_parent(l, d);
            receive_data(l, d);
        }
        send_ack(l, d); // also for repeats, whose acknowledgement was lost
        break;
    }
    }
}

// ------------------------------------------------------------------------------------------------ the interrupt
bool port_wm7_active(void) { return s_net && s_status && s_status->mState != WIRELESS_UNIT_STATE_DISABLED; }

// Called at the OS's interrupt points: network input, timers, then the messages for the ARM9. True when a message
// was delivered.
bool port_wm7_poll(void) {
    if (!s_net) return false;
    u64 now = now_ns();
    bool ran = false;
    static u64 s_prev;
    if (WM7_TRACE && s_prev && now - s_prev > 100000000ull) port_log("wm7: no poll for %d ms", (int) ((now - s_prev) / 1000000));
    s_prev = now;
    if (since(now, s_last_poll_ns) >= 500000ull) {
        s_last_poll_ns = now;
        static u8 buf[sizeof(NetHeader) + sizeof(DataBody) + 64] __attribute__((aligned(8)));
        HostNetAddr from;
        int32_t n;
        for (int i = 0; i < 64 && (n = hapi_net_recv(buf, sizeof(buf), &from)) >= 0; i++) receive(buf, n, &from);

        if (s_parent && now >= s_beacon_ns) {
            send_beacon();
            s_beacon_ns = now + BEACON_NS;
        }
        if (s_scanning && now >= s_scan_due_ns) scan_result();
        if (s_joining) {
            if (since(now, s_join_start_ns) > JOIN_TIMEOUT_NS) {
                join_failed(9);
            } else if (since(now, s_join_sent_ns) > JOIN_RETRY_NS) {
                send_packet(&s_parents[s_join_parent].addr, PKT_JOIN, 0, &s_join, sizeof(s_join));
                s_join_sent_ns = now;
            }
        }
        for (int a = 0; a < MAX_LINKS; a++) {
            Link *l = &s_links[a];
            if (!l->used) continue;
            if (since(now, l->heard_ns) > LINK_TIMEOUT_NS) {
                port_log("wm7: station %d timed out", a);
                link_lost((u16) a, true, WIRELESS_STATE_UNLINKED);
                continue;
            }
            if (l->send_acked != l->send_next && now >= l->resend_ns) {
                // Resend what is in flight (the oldest first, a few at a time).
                for (u32 s = l->send_acked; s != l->send_next && s - l->send_acked < 8; s++) {
                    OutPacket *p = &l->out[s % OUT_SLOTS];
                    p->resent = true;
                    ((DataBody *) (p->bytes + sizeof(NetHeader)))->kind |= DATA_RESENT;
                    link_transmit(l, s, now);
                }
                l->resend_ns = now + resend_after(l);
            }
            if (since(now, l->sent_ns) > KEEPALIVE_NS) send_ack(l, NULL);
        }
    }
    deliver_held();
    ds_frame(now);
    for (int guard = 0; s_queue_count && guard < MSG_SLOTS; guard++) {
        WirelessMessage *m = s_queue[s_queue_head];
        s_queue_head = (s_queue_head + 1) % MSG_SLOTS;
        s_queue_count--;
        deliver(m);
        ran = true;
    }
    return ran;
}
