#include "platform/native_context.h"
#if defined(__aarch64__)
extern "C" __attribute__((naked,returns_twice))int ARMSaveContext(void *){
 __asm__ volatile("stp x0,x30,[sp,#-16]!\nbl sm64ds_context_save_storage\nldp x1,x30,[sp],#16\n"
 "stp x19,x20,[x0,#0]\nstp x21,x22,[x0,#16]\nstp x23,x24,[x0,#32]\nstp x25,x26,[x0,#48]\nstp x27,x28,[x0,#64]\nstp x29,x30,[x0,#80]\n"
 "mov x2,sp\nstr x2,[x0,#96]\nstp d8,d9,[x0,#104]\nstp d10,d11,[x0,#120]\nstp d12,d13,[x0,#136]\nstp d14,d15,[x0,#152]\n"
 "mrs x2,fpcr\nstr w2,[x0,#168]\nmrs x2,fpsr\nstr w2,[x0,#172]\nmov w0,#0\nret\n");
}
#elif defined(__x86_64__)
extern "C" __attribute__((naked,returns_twice))int ARMSaveContext(void *){
 __asm__ volatile("push %rdi\ncall sm64ds_context_save_storage\npop %rdi\n"
 "mov %rbx,0(%rax)\nmov %rbp,8(%rax)\nmov %r12,16(%rax)\nmov %r13,24(%rax)\nmov %r14,32(%rax)\nmov %r15,40(%rax)\n"
 "lea 8(%rsp),%rdx\nmov %rdx,48(%rax)\nmov (%rsp),%rdx\nmov %rdx,56(%rax)\nstmxcsr 64(%rax)\nfnstcw 68(%rax)\nxor %eax,%eax\nret\n");
}
#else
#error Native context save needs an architecture implementation
#endif
