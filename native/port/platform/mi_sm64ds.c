// NitroSDK MI on the host for Super Mario 64 DS: CPU copies and fills, DMA, geometry FIFO transfers, decompression,
// and the SDK's assembly matrix primitives. From mkds-android's port/platform/mi.c and asm_replacements.c.
#include "port.h"

#include <string.h>

#include "types.h"

typedef void (*MIDmaCallback)(void *);

void port_os_poll(void);

#define GXFIFO_ADDR 0x04000400u
#define G3_PORTS_START 0x04000440u
#define G3_PORTS_END 0x04000600u

static inline bool is_gxfifo(const volatile void *p) { return (uintptr_t) p >= GXFIFO_ADDR && (uintptr_t) p < G3_PORTS_START; }

static void send_words(const u32 *src, uintptr_t dst, u32 bytes) {
    if (dst >= GXFIFO_ADDR && dst < G3_PORTS_START) { // 0x04000400..0x0400043f all reach the FIFO
        for (u32 i = 0; i < bytes / 4; i++) {
            gx3d_fifo_write(src[i]);
        }
    } else if (dst >= G3_PORTS_START && dst < G3_PORTS_END) {
        for (u32 i = 0; i < bytes / 4; i++) {
            gx3d_port_write((u32) dst, src[i]);
        }
    } else {
        volatile u32 *d = (volatile u32 *) dst;
        for (u32 i = 0; i < bytes / 4; i++) {
            *d = src[i];
        }
    }
}

// ------------------------------------------------------------------------------------------------ CPU copies
void MIi_CpuClear32(u32 data, void *destp, u32 size) {
    u32 *d = destp;
    for (u32 i = 0; i < size / 4; i++) {
        d[i] = data;
    }
}

void MIi_CpuCopy32(const void *srcp, void *destp, u32 size) {
    if ((uintptr_t) destp >= GXFIFO_ADDR && (uintptr_t) destp < G3_PORTS_END) {
        send_words(srcp, GXFIFO_ADDR, size);
        return;
    }
    memmove(destp, srcp, size & ~3u);
}

void MIi_CpuSend32(const void *srcp, vu32 *destp, u32 size) { send_words(srcp, (uintptr_t) destp, size); }

void MIi_CpuClearFast(u32 data, void *destp, u32 size) { MIi_CpuClear32(data, destp, size); }
void MIi_CpuCopyFast(const void *srcp, void *destp, u32 size) { MIi_CpuCopy32(srcp, destp, size); }
// Block copies: to the geometry engine when the destination is its FIFO or a command port.
static bool is_geometry(const void *p) { return (uintptr_t) p >= GXFIFO_ADDR && (uintptr_t) p < G3_PORTS_END; }
static void block_copy(const void *src, void *dst, u32 n) {
    if (is_geometry(dst)) {
        if ((uintptr_t) dst < G3_PORTS_START) {
            send_words(src, GXFIFO_ADDR, n);
        } else {
            for (u32 i = 0; i < n / 4; i++) gx3d_port_write((u32) (uintptr_t) dst + 4 * i, ((const u32 *) src)[i]);
        }
    } else {
        memcpy(dst, src, n);
    }
}
void MI_Copy32B(const void *pSrc, void *pDest) { block_copy(pSrc, pDest, 32); }
void MI_Copy36B(const void *pSrc, void *pDest) { block_copy(pSrc, pDest, 36); }
void MI_Copy48B(const void *pSrc, void *pDest) { block_copy(pSrc, pDest, 48); }
void MI_Copy64B(const void *pSrc, void *pDest) { memcpy(pDest, pSrc, 64); }
void MI_Zero36B(void *pDest) { memset(pDest, 0, 36); }
void MI_CpuFill8(void *dstp, u8 data, u32 size) { memset(dstp, data, size); }
void MI_CpuCopy8(const void *srcp, void *dstp, u32 size) { memmove(dstp, srcp, size); }

u32 MI_SwapWord(u32 setData, volatile u32 *destp) {
    u32 old = *destp;
    *destp = setData;
    return old;
}

// ------------------------------------------------------------------------------------------------ DMA
// The transfer is over when the call returns, so the completion interrupt's callback runs at once: the game waits for
// it in busy loops (NNS_G3dGeWaitSendDL), which nothing else would interrupt here.
static void dma_done(MIDmaCallback callback, void *arg) {
    if (callback) {
        callback(arg);
    }
}

static void dma_copy(u32 dmaNo, const void *src, void *dest, u32 size, bool fill32, u32 fill_value, u32 unit) {
    (void) dmaNo;
    if (fill32) {
        MIi_CpuClear32(fill_value, dest, size);
    } else if (is_gxfifo(dest) || ((uintptr_t) dest >= G3_PORTS_START && (uintptr_t) dest < G3_PORTS_END)) {
        send_words(src, (uintptr_t) dest, size);
    } else if (unit == 2) {
        const u16 *s = src;
        u16 *d = dest;
        for (u32 i = 0; i < size / 2; i++) {
            d[i] = s[i];
        }
    } else {
        memmove(dest, src, size);
    }
}

void MI_DmaCopy32(u32 dmaNo, const void *src, void *dest, u32 size) { dma_copy(dmaNo, src, dest, size, false, 0, 4); }
void MI_DmaCopy16(u32 dmaNo, const void *src, void *dest, u32 size) { dma_copy(dmaNo, src, dest, size, false, 0, 2); }
void MI_DmaFill32(u32 dmaNo, void *dest, u32 data, u32 size) { dma_copy(dmaNo, NULL, dest, size, true, data, 4); }

void MI_DmaCopy32Async(u32 dmaNo, const void *src, void *dest, u32 size, MIDmaCallback callback, void *arg) {
    dma_copy(dmaNo, src, dest, size, false, 0, 4);
    dma_done(callback, arg);
}

void MI_DmaFill32Async(u32 dmaNo, void *dest, u32 data, u32 size, MIDmaCallback callback, void *arg) {
    dma_copy(dmaNo, NULL, dest, size, true, data, 4);
    dma_done(callback, arg);
}

void MI_SendGXCommandAsync(u32 dmaNo, const void *src, u32 commandLength, MIDmaCallback callback, void *arg) {
    (void) dmaNo;
    send_words(src, GXFIFO_ADDR, commandLength);
    dma_done(callback, arg);
}

void MI_SendGXCommandAsyncFast(u32 dmaNo, const void *src, u32 commandLength, MIDmaCallback callback, void *arg) {
    MI_SendGXCommandAsync(dmaNo, src, commandLength, callback, arg);
}

void MI_SendGXCommand(u32 dmaNo, const void *src, u32 commandLength) {
    (void) dmaNo;
    send_words(src, GXFIFO_ADDR, commandLength);
}

void MI_WaitDma(u32 dmaNo) { (void) dmaNo; }
void MI_StopDma(u32 dmaNo) { (void) dmaNo; }
int MI_IsDmaBusy(u32 dmaNo) { (void) dmaNo; return 0; }

// A DMA channel programmed through its registers (src/ITCM/dma_transfer.c): runs the transfer at once when the start
// timing is immediate or GXFIFO, as the game uses them.
void port_dma_start(uint32_t channel, uint32_t src, uint32_t dst, uint32_t control) {
    volatile u32 *regs = (volatile u32 *) (uintptr_t) (0x040000b0 + channel * 12);
    regs[0] = src;
    regs[1] = dst;
    regs[2] = control;
    if (!(control & 0x80000000u)) {
        return;
    }
    u32 count = control & 0x1fffff;
    if (count == 0) {
        count = 0x200000;
    }
    bool words = (control >> 26) & 1;
    u32 timing = (control >> 27) & 7;
    int dst_step = (int[]) {1, -1, 0, 1}[(control >> 21) & 3];
    int src_step = (int[]) {1, -1, 0, 0}[(control >> 23) & 3];
    u32 unit = words ? 4 : 2;
    if (timing != 0 && timing != 7) {
        port_log("dma: channel %u start timing %u not emulated (src %08x dst %08x)", channel, timing, src, dst);
    }
    for (u32 i = 0; i < count; i++) {
        uintptr_t s = src + (intptr_t) src_step * unit * i;
        uintptr_t d = dst + (intptr_t) dst_step * unit * i;
        if (timing == 7 || d == GXFIFO_ADDR) {
            gx3d_fifo_write(*(volatile u32 *) s);
        } else if (d >= G3_PORTS_START && d < G3_PORTS_END) {
            gx3d_port_write((u32) d, *(volatile u32 *) s);
        } else if (words) {
            *(volatile u32 *) d = *(volatile u32 *) s;
        } else {
            *(volatile u16 *) d = *(volatile u16 *) s;
        }
    }
    regs[2] = control & ~0x80000000u; // finished
}

void StartDmaTransfer(u32 channelIndex, const void *source, void *destination, u32 control) {
    port_dma_start(channelIndex, (u32) (uintptr_t) source, (u32) (uintptr_t) destination, control);
}
void StartDmaTransferWithoutIrqDisable(u32 channelIndex, const void *source, void *destination, u32 control) {
    port_dma_start(channelIndex, (u32) (uintptr_t) source, (u32) (uintptr_t) destination, control);
}
void StartDmaTransferAndChannel0Dummy(u32 channelIndex, const void *source, void *destination, u32 control) {
    port_dma_start(channelIndex, (u32) (uintptr_t) source, (u32) (uintptr_t) destination, control);
}
void StartDmaTransferAndChannel0DummyWithoutIrqDisable(u32 channelIndex, const void *source, void *destination,
                                                       u32 control) {
    port_dma_start(channelIndex, (u32) (uintptr_t) source, (u32) (uintptr_t) destination, control);
}

// ------------------------------------------------------------------------------------------------ decompression
// LZ77 (type 0x10): header word (size << 8), then flag bytes for 8 items each, top bit first: 0 = literal byte,
// 1 = copy (top nibble + 3) bytes from (low 12 bits + 1) back.
void MI_UncompressLZ8(const void *srcp, void *destp) {
    const u8 *src = srcp;
    u8 *dst = destp;
    u32 size = (src[1] | (src[2] << 8) | (src[3] << 16));
    src += 4;
    while (size > 0) {
        u8 flags = *src++;
        for (int i = 0; i < 8 && size > 0; i++, flags <<= 1) {
            if (!(flags & 0x80)) {
                *dst++ = *src++;
                size--;
            } else {
                u32 len = (src[0] >> 4) + 3;
                u32 disp = (((src[0] & 0xf) << 8) | src[1]) + 1;
                src += 2;
                while (len-- > 0 && size > 0) {
                    *dst = dst[-(s32) disp];
                    dst++;
                    size--;
                }
            }
        }
    }
    port_os_poll();
}

// Backward LZ (the ROM's compressed ARM9 binary and overlays): the footer at compressedEnd gives the sizes.
void MIi_UncompressBackward(void *compressedEnd) {
    u8 *end = compressedEnd;
    u32 footer = *(u32 *) (end - 8), extra = *(u32 *) (end - 4);
    if (!extra && !footer) {
        return;
    }
    u32 header_len = footer >> 24;
    u32 compressed_len = footer & 0xffffff;
    u8 *src = end - header_len;
    u8 *dst = end + extra;
    u8 *stop = end - compressed_len;
    while (src > stop) {
        u8 flags = *--src;
        for (int i = 0; i < 8 && src > stop; i++, flags <<= 1) {
            if (!(flags & 0x80)) {
                *--dst = *--src;
            } else {
                u32 hi = *--src, lo = *--src;
                u32 disp = (((hi & 0xf) << 8) | lo) + 3;
                u32 len = (hi >> 4) + 3;
                while (len--) {
                    dst--;
                    *dst = dst[disp];
                }
            }
        }
    }
}

void MIi_CpuClear16(u16 data, void *destp, u32 size) {
    u16 *d = destp;
    for (u32 i = 0; i < size / 2; i++) {
        d[i] = data;
    }
}
void MIi_CpuCopy16(const void *srcp, void *destp, u32 size) { memmove(destp, srcp, size & ~1u); }

// LZ77 (type 0x10) written 16 bits at a time (VRAM-safe in the original): the same output.
void DecompressLZ16(void *src, void *dst) {
    MI_UncompressLZ8(src, dst);
}

// ------------------------------------------------------------------------------------------------ geometry FIFO
// 48 bytes to a geometry port: stmia without write-back, three words at a time, to dst, dst+4, dst+8.
void GX_SendFifo48B(const void *pSrc, void *pDest) {
    const u32 *s = pSrc;
    for (int i = 0; i < 12; i++) {
        send_words(&s[i], (uintptr_t) pDest + 4 * (i % 3), 4);
    }
}
// 128 zero words: NOPs that flush a partly written packed command.
void GXi_NopClearFifo128_(void *dest) {
    static const u32 zeros[128];
    send_words(zeros, (uintptr_t) dest, sizeof(zeros));
}

// ------------------------------------------------------------------------------------------------ matrices (fx_mtx*.c)
#define FX_ONE 0x1000
static void set(s32 *m, int n, const s32 *v) { memcpy(m, v, sizeof(s32) * n); }

void MTX_Identity33_(void *dst) { set(dst, 9, (const s32[]) {FX_ONE, 0, 0, 0, FX_ONE, 0, 0, 0, FX_ONE}); }
void MTX_Identity43_(void *dst) { set(dst, 12, (const s32[]) {FX_ONE, 0, 0, 0, FX_ONE, 0, 0, 0, FX_ONE, 0, 0, 0}); }
void MTX_Identity44_(void *dst) {
    set(dst, 16, (const s32[]) {FX_ONE, 0, 0, 0, 0, FX_ONE, 0, 0, 0, 0, FX_ONE, 0, 0, 0, 0, FX_ONE});
}
void MTX_Copy33To44_(const void *srcp, void *dstp) {
    const s32 *s = srcp;
    s32 *d = dstp;
    s32 t[16] = {s[0], s[1], s[2], 0, s[3], s[4], s[5], 0, s[6], s[7], s[8], 0, 0, 0, 0, FX_ONE};
    memcpy(d, t, sizeof(t));
}
void MTX_Scale43_(void *dst, s32 x, s32 y, s32 z) { set(dst, 12, (const s32[]) {x, 0, 0, 0, y, 0, 0, 0, z, 0, 0, 0}); }
void MTX_RotX33_(void *dst, s32 s, s32 c) { set(dst, 9, (const s32[]) {FX_ONE, 0, 0, 0, c, s, 0, -s, c}); }
void MTX_RotY33_(void *dst, s32 s, s32 c) { set(dst, 9, (const s32[]) {c, 0, -s, 0, FX_ONE, 0, s, 0, c}); }
void MTX_RotZ33_(void *dst, s32 s, s32 c) { set(dst, 9, (const s32[]) {c, s, 0, -s, c, 0, 0, 0, FX_ONE}); }
void MTX_RotX43_(void *dst, s32 s, s32 c) { set(dst, 12, (const s32[]) {FX_ONE, 0, 0, 0, c, s, 0, -s, c, 0, 0, 0}); }
void MTX_RotY43_(void *dst, s32 s, s32 c) { set(dst, 12, (const s32[]) {c, 0, -s, 0, FX_ONE, 0, s, 0, c, 0, 0, 0}); }
void MTX_RotZ43_(void *dst, s32 s, s32 c) { set(dst, 12, (const s32[]) {c, s, 0, -s, c, 0, 0, 0, FX_ONE, 0, 0, 0}); }
