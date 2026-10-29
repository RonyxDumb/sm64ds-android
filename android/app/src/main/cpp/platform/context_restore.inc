#include "platform/native_context.h"
#if defined(__aarch64__)
extern "C" __attribute__((naked,noreturn))void sm64ds_context_load_registers(const uint64_t *){
 __asm__ volatile("ldp x19,x20,[x0,#0]\nldp x21,x22,[x0,#16]\nldp x23,x24,[x0,#32]\nldp x25,x26,[x0,#48]\nldp x27,x28,[x0,#64]\nldp x29,x30,[x0,#80]\n"
 "ldr x2,[x0,#96]\nmov sp,x2\nldp d8,d9,[x0,#104]\nldp d10,d11,[x0,#120]\nldp d12,d13,[x0,#136]\nldp d14,d15,[x0,#152]\n"
 "ldr w2,[x0,#168]\nmsr fpcr,x2\nldr w2,[x0,#172]\nmsr fpsr,x2\nmov w0,#1\nret\n");
}
#elif defined(__x86_64__)
extern "C" __attribute__((naked,noreturn))void sm64ds_context_load_registers(const uint64_t *){
 __asm__ volatile("mov %rdi,%rax\nmov 0(%rax),%rbx\nmov 8(%rax),%rbp\nmov 16(%rax),%r12\nmov 24(%rax),%r13\nmov 32(%rax),%r14\nmov 40(%rax),%r15\n"
 "ldmxcsr 64(%rax)\nfldcw 68(%rax)\nmov 48(%rax),%rsp\nmov 56(%rax),%rdx\nmov $1,%eax\njmp *%rdx\n");
}
#else
#error Native context restore needs an architecture implementation
#endif
extern "C" [[noreturn]]void ARMRestoreContext(void *guest){sm64ds::platform::restore_context(guest);}
