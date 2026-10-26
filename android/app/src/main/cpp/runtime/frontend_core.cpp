#include "frontend_core.h"
#include "packed_ptr.h"
#include "ntr/mmio.h"
#include <stdexcept>

extern "C" {
void abi_func_ov007_020b6a84();
void abi_func_ov007_020b68e8();
void abi_func_ov007_020b697c();
void abi_func_ov007_020b69c4();
void abi_func_ov007_020b693c();
void abi_func_ov007_020b4100(uint32_t,uint32_t);
void abi_func_ov007_020c3e3c(uint32_t);
void abi_func_ov007_020c3e2c(uint32_t);
void abi_func_ov007_020c3e1c(uint32_t);
void abi_func_ov007_020c20b8(uint32_t,uint32_t);
void abi_func_ov007_020c1db0(uint32_t,uint32_t);
void abi_func_ov007_020c92d0(uint32_t);
int pk_frontend_allocate(sm64ds::packed::Ptr<void>);
void pk_frontend_deallocate(sm64ds::packed::Ptr<void>);
int pk_frontend_largest();
sm64ds::packed::Ptr<void> pk_frontend_load(uint16_t);
}
namespace sm64ds {
namespace { thread_local FrontendCore *core=nullptr; }
FrontendCore &FrontendCore::current() {
    if (!core) throw std::runtime_error("No native frontend callback owner.");
    return *core;
}
FrontendCore::Scope::Scope(FrontendCore &next) : address_(next.space_), previous_(core) { core=&next; }
FrontendCore::Scope::~Scope() { core=previous_; }
FrontendCore::FrontendCore(GameHeap &heap, Cartridge::DataImage data, int ownerLanguage, FileLoader loader)
    : space_(heap), image_(std::move(data)), ownerLanguage_(ownerLanguage), loader_(std::move(loader)) {
    if (ownerLanguage<0 || ownerLanguage>5) throw std::runtime_error("Invalid owner language.");
    if (!ntr::io_init()) throw std::runtime_error("Cannot map native hardware latches.");
    space_.map(ntr::IO_BASE,ntr::host_ptr(ntr::IO_BASE),ntr::IO_SIZE);
    space_.map(image_.base,image_.bytes.data(),image_.bytes.size());
    space_.bind(0x020ccab4,pk_frontend_allocate);
    space_.bind(0x020cca98,pk_frontend_deallocate);
    space_.bind(0x020cca80,pk_frontend_largest);
    space_.bind(0x020cca74,pk_frontend_load);
    space_.bind(0x020cca68,pk_frontend_deallocate);
    Scope scope(*this);
    // IDs from the original callback table, bound only to compiled native code.
    abi_func_ov007_020c3e3c(0x020ccab4);
    abi_func_ov007_020c3e2c(0x020cca98);
    abi_func_ov007_020c3e1c(0x020cca80);
    abi_func_ov007_020b4100(0x020cca74,0x020cca68);
    if (loader_) {
        // Execute all five genuine builders in the original root factory.
        abi_func_ov007_020b68e8();
        root_=word_at(0x0210342c);
    } else {
        // ROM-free dependency check: only source records requiring no embedded
        // screen/sprite-bank tables. Production always takes the factory above.
        root_=space_.allocate(0x188);
        *packed::Ptr<packed::Ptr<void>>(0x0210342c)=packed::Ptr<void>(root_);
        abi_func_ov007_020b6a84();
        abi_func_ov007_020b69c4();
    }
}
FrontendCore::~FrontendCore() = default;
uint32_t FrontendCore::load_file(uint16_t handle) {
    if (!loader_) throw std::runtime_error("No native frontend file loader.");
    auto bytes=loader_(handle);
    if (bytes.empty() || bytes.size()>16*1024*1024) throw std::runtime_error("Invalid frontend file extent.");
    auto a=space_.allocate(static_cast<uint32_t>(bytes.size()));
    std::memcpy(space_.data(a,bytes.size()),bytes.data(),bytes.size());
    return a;
}
uint32_t FrontendCore::word_at(uint32_t a) const {
    uint32_t v; std::memcpy(&v,space_.data(a,sizeof v),sizeof v); return v;
}
void FrontendCore::sample(uint16_t keys, int x, int y) {
    Scope scope(*this);
    bool down=x>=0 && x<256 && y>=0 && y<192;
    touch_={uint16_t(down?x:0),uint16_t(down?y:0),uint16_t(down),0};
    auto sample=touch_;
    abi_func_ov007_020c20b8(word_at(root_+0x54),packed::Ptr<uint16_t>(&keys).address());
    abi_func_ov007_020c1db0(word_at(root_+0x50),packed::Ptr<uint16_t>(sample.data()).address());
}
void FrontendCore::step_state(unsigned index) {
    if (index>=3) throw std::out_of_range("Invalid frontend state index.");
    Scope scope(*this);
    abi_func_ov007_020c92d0(word_at(root_+4+4*index));
}
}
