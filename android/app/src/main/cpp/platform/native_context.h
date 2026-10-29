#pragma once
#include "arm9_services.h"
#include <memory>
#include <thread>
namespace sm64ds::platform {
struct alignas(16) NativeContext{
 std::array<uint64_t,22> state{};std::vector<uint8_t> stack;void *guest=nullptr;
 uint32_t entry=0,irq=0;std::thread::id owner;bool saved=false;
};
NativeContext &context(void *guest,bool create);
void init_context(void *guest,uint32_t entry,uint32_t stack);
[[noreturn]]void restore_context(void *guest);
void capture_fault(void *destination,int reason);
}
extern "C" int ARMSaveContext(void *) __attribute__((returns_twice));
extern "C" [[noreturn]]void ARMRestoreContext(void *);
extern "C" sm64ds::platform::NativeContext *sm64ds_context_save_storage(void *);
extern "C" [[noreturn]]void sm64ds_context_load_registers(const uint64_t *);
