// Accesses through null pointers. On the DS the first 32 MB of the address space mirror the ITCM, so a read through
// a null pointer (plus a small offset) returns whatever is there and the game carries on; several of its routines rely
// on that (e.g. drawing with an unset texture). Here such an access faults: the handler below decodes the faulting
// load or store, gives loads the value 0, drops stores, and resumes after the instruction.
//
// 32-bit ARM: the game is compiled in ARM state (-marm), so only A32 encodings occur. AArch64: A64 encodings.
#include "port.h"

#include <signal.h>
#include <string.h>
#include <ucontext.h>

#define NULL_REGION_END 0x01000000u

static struct sigaction s_previous;
static unsigned s_count;

#if defined(__arm__)
// A32: returns true when the instruction at pc was emulated.
static bool emulate(ucontext_t *uc) {
    mcontext_t *m = &uc->uc_mcontext;
    unsigned long *r = &m->arm_r0; // r0..r15 are consecutive in sigcontext
    uint32_t insn = *(const uint32_t *) m->arm_pc;
    if (m->arm_cpsr & 0x20) {
        return false; // Thumb: not generated for the game
    }
    uint32_t rn = (insn >> 16) & 15, rd = (insn >> 12) & 15;
    bool load = (insn >> 20) & 1;
    if ((insn & 0x0c000000) == 0x04000000) { // LDR/STR/LDRB/STRB
        bool p = (insn >> 24) & 1, w = (insn >> 21) & 1, u = (insn >> 23) & 1;
        uint32_t off;
        if (insn & 0x02000000) {
            uint32_t rm = r[insn & 15], shift = (insn >> 7) & 31;
            switch ((insn >> 5) & 3) {
            case 0: off = rm << shift; break;
            case 1: off = shift ? rm >> shift : 0; break;
            case 2: off = (uint32_t) ((int32_t) rm >> (shift ? shift : 31)); break;
            default: off = shift ? (rm >> shift) | (rm << (32 - shift)) : rm; break;
            }
        } else {
            off = insn & 0xfff;
        }
        uint32_t base = r[rn], next = u ? base + off : base - off;
        if (load) r[rd] = 0;
        if ((!p || w) && rn != rd) r[rn] = next;
    } else if ((insn & 0x0e000090) == 0x00000090 && (insn & 0x60)) { // LDRH/STRH/LDRSB/LDRSH/LDRD/STRD
        bool p = (insn >> 24) & 1, w = (insn >> 21) & 1, u = (insn >> 23) & 1;
        uint32_t off = (insn & (1u << 22)) ? (((insn >> 4) & 0xf0) | (insn & 15)) : r[insn & 15];
        uint32_t base = r[rn], next = u ? base + off : base - off;
        uint32_t sh = (insn >> 5) & 3;
        if (load) {
            r[rd] = 0;
        } else if (sh == 2) { // LDRD (L=0, SH=10)
            r[rd] = 0;
            if (rd < 14) r[rd + 1] = 0;
        }
        if ((!p || w) && rn != rd) r[rn] = next;
    } else if ((insn & 0x0e000000) == 0x08000000) { // LDM/STM
        uint32_t list = insn & 0xffff;
        int n = __builtin_popcount(list);
        if (load) {
            for (int i = 0; i < 15; i++) if (list & (1u << i)) r[i] = 0;
        }
        if (((insn >> 21) & 1) && !(load && (list & (1u << rn)))) {
            r[rn] += ((insn >> 23) & 1) ? 4u * n : -4u * n;
        }
    } else if ((insn & 0x0e000e00) == 0x0c000a00) { // VLDR/VSTR/VLDM/VSTM: no zeroing of VFP registers needed here
        if (load) return false;
    } else {
        return false;
    }
    m->arm_pc += 4;
    return true;
}
#define FAULT_PC(uc) ((uintptr_t) (uc)->uc_mcontext.arm_pc)
#elif defined(__aarch64__)
static bool emulate(ucontext_t *uc) {
    mcontext_t *m = &uc->uc_mcontext;
    uint32_t insn = *(const uint32_t *) m->pc;
    // Load/store register (immediate unsigned offset, unscaled, pre/post-indexed, register offset), pair forms.
    if ((insn & 0x0a000000) != 0x08000000) {
        return false;
    }
    bool vector = (insn >> 26) & 1;
    uint32_t rt = insn & 31, rn = (insn >> 5) & 31;
    if ((insn & 0x3a000000) == 0x28000000) { // LDP/STP (and LDPSW)
        bool load = (insn >> 22) & 1;
        uint32_t rt2 = (insn >> 10) & 31, idx = (insn >> 23) & 3;
        if (load && !vector) {
            if (rt != 31) m->regs[rt] = 0;
            if (rt2 != 31) m->regs[rt2] = 0;
        } else if (load) {
            return false;
        }
        if (idx == 1 || idx == 3) { // post / pre-index: write back
            int32_t imm7 = (int32_t) (insn << 10) >> 25;
            uint32_t scale = vector ? 2 + (insn >> 30) : 2 + ((insn >> 31) & 1);
            uint64_t *base = rn == 31 ? &m->sp : &m->regs[rn];
            *base += (int64_t) imm7 << scale;
        }
    } else if ((insn & 0x3b000000) == 0x39000000 || (insn & 0x3b200000) == 0x38000000 ||
               (insn & 0x3b200c00) == 0x38200800) { // LDR/STR (unsigned imm), (unscaled/pre/post), (register)
        uint32_t opc = (insn >> 22) & 3;
        bool load = opc != 0;
        if (load && vector) return false;
        if (load && rt != 31) m->regs[rt] = 0;
        if ((insn & 0x3b200000) == 0x38000000 && (insn & 0x400)) { // pre/post-indexed: write back
            int32_t imm9 = (int32_t) (insn << 11) >> 23;
            uint64_t *base = rn == 31 ? &m->sp : &m->regs[rn];
            *base += imm9;
        }
    } else {
        return false;
    }
    m->pc += 4;
    return true;
}
#define FAULT_PC(uc) ((uintptr_t) (uc)->uc_mcontext.pc)
#endif

static void handler(int sig, siginfo_t *info, void *ctx) {
#if defined(__arm__) || defined(__aarch64__)
    uintptr_t addr = (uintptr_t) info->si_addr;
    if (addr < NULL_REGION_END && emulate((ucontext_t *) ctx)) {
        s_count++;
        return;
    }
#endif
    // Not ours: the previous handler (crash reporting), or the default action.
    if (s_previous.sa_flags & SA_SIGINFO) {
        s_previous.sa_sigaction(sig, info, ctx);
    } else if (s_previous.sa_handler != SIG_DFL && s_previous.sa_handler != SIG_IGN) {
        s_previous.sa_handler(sig);
    } else {
        signal(sig, SIG_DFL);
        raise(sig);
    }
}

unsigned port_null_access_count(void) { return s_count; }

void port_null_access_init(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = handler;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &s_previous);
}
