#include "platform/native_context.h"
#include <cassert>
#include <cstdio>
using namespace sm64ds;
alignas(16)uint8_t parentContext[0x64]{},childContext[0x64]{};
volatile int step=0;
void child(uint32_t argument){assert(argument==47);step=1;if(!ARMSaveContext(childContext))ARMRestoreContext(parentContext);assert(step==2);step=3;ARMRestoreContext(parentContext);}
int main(){
 assert(ntr::io_init());GameHeap heap;packed::Space space(heap);packed::Space::Scope scope(space);space.bind(0x02001234,&child);platform::init_context(childContext,0x02001234,0x020f0000);uint32_t argument=47;std::memcpy(childContext+4,&argument,4);
 assert(platform::mask_update(0x80,0x80)==0);if(!ARMSaveContext(parentContext))ARMRestoreContext(childContext);assert(step==1&&ntr::rt_irq_masked());step=2;
 if(!ARMSaveContext(parentContext))ARMRestoreContext(childContext);assert(step==3&&ntr::rt_irq_masked());std::puts("Native contexts: retained stacks, argument, resume value and IRQ state passed.");
}
