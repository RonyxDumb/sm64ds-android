#include "frontend_core.h"
#include "packed_ptr.h"
#include "title_config.h"
#include "ntr/mmio.h"
#include "ntr/gx.h"
#include <stdexcept>

extern "C" {
void abi_func_ov007_020b6a84();
void abi_func_ov007_020b68e8();
uint32_t abi_func_ov007_020b7138(uint32_t,uint32_t);
void abi_GX_DispOn();
uint32_t abi_func_ov007_020b7090(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
void abi_func_ov007_020b7040();
void abi_func_ov007_020b6eb4();
uint8_t pk_frontend_command(int);
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
namespace sm64ds::frontend { void bind_sources(packed::Space &); }
namespace sm64ds {
namespace { thread_local FrontendCore *core=nullptr; }
FrontendCore &FrontendCore::current() {
    if (!core) throw std::runtime_error("No native frontend callback owner.");
    return *core;
}
FrontendCore::Scope::Scope(FrontendCore &next) : address_(next.space_), previous_(core) { core=&next; }
FrontendCore::Scope::~Scope() { core=previous_; }
FrontendCore::FrontendCore(GameHeap &heap, Cartridge::DataImage data, int ownerLanguage, FileLoader loader,
                           Cartridge::DataImage arm9, const TitleConfig *config, Command command, Audio audio)
    : space_(heap), image_(std::move(data)), arm9_(std::move(arm9)), ownerLanguage_(ownerLanguage),
      loader_(std::move(loader)), command_(std::move(command)), audio_(std::move(audio)), config_(config) {
    if (ownerLanguage<0 || ownerLanguage>5) throw std::runtime_error("Invalid owner language.");
    if (!ntr::io_init()) throw std::runtime_error("Cannot map native hardware latches.");
    space_.map(ntr::IO_BASE,ntr::host_ptr(ntr::IO_BASE),ntr::IO_SIZE);
    space_.map(ntr::PLTT_BASE,ntr::host_ptr(ntr::PLTT_BASE),ntr::PLTT_SIZE);
    space_.map(ntr::VRAM_BASE,ntr::host_ptr(ntr::VRAM_BASE),ntr::VRAM_SIZE);
    space_.map(ntr::OAM_BASE,ntr::host_ptr(ntr::OAM_BASE),ntr::OAM_SIZE);
    if (!arm9_.bytes.empty()) space_.map(arm9_.base,arm9_.bytes.data(),arm9_.bytes.size());
    space_.map(image_.base,image_.bytes.data(),image_.bytes.size());
    frontend::bind_sources(space_);
    space_.bind(0x020ccab4,pk_frontend_allocate);
    space_.bind(0x020cca98,pk_frontend_deallocate);
    space_.bind(0x020cca80,pk_frontend_largest);
    space_.bind(0x020cca74,pk_frontend_load);
    space_.bind(0x020cca68,pk_frontend_deallocate);
    space_.bind(0x020cc600,pk_frontend_command);
    Scope scope(*this);
    // IDs from the original callback table, bound only to compiled native code.
    abi_func_ov007_020c3e3c(0x020ccab4);
    abi_func_ov007_020c3e2c(0x020cca98);
    abi_func_ov007_020c3e1c(0x020cca80);
    abi_func_ov007_020b4100(0x020cca74,0x020cca68);
    if (config) {
        if (!loader_ || arm9_.bytes.empty() || !config->messageBank)
            throw std::runtime_error("Incomplete native title configuration.");
        auto record=configAddress_=space_.allocate(0xa0);
        std::memcpy(space_.data(record,0x9c),config->bytes.data(),0x9c);
        *packed::Ptr<uint32_t>(record+0x9c)=packed::Ptr<const uint8_t>(config->messageBank).address();
        ntr::io_write(0x04000304,(ntr::io_read(0x04000304,2)&~0x20eu)|0x20e,2);
        abi_GX_DispOn();
        ntr::io_write(0x04001000,ntr::io_read(0x04001000,4)|0x10000,4);
        if (abi_func_ov007_020b7138(0x02103290,record)!=1)
            throw std::runtime_error("Original title scene initialization failed.");
        root_=word_at(0x0210342c);
    } else if (loader_) {
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
FrontendCore::~FrontendCore() {
    if (audio_) for (const auto &voice:voices_) audio_({SoundOp::Stop,voice.second.player,voice.first});
}
uint8_t FrontendCore::command(int value) {
    if (!command_ || !config_) throw std::runtime_error("No native title command owner.");
    auto result=command_(value);
    std::memcpy(space_.data(configAddress_,0x9c),config_->bytes.data(),0x9c);
    return result;
}
int FrontendCore::update(uint16_t keys,int x,int y) {
    Scope scope(*this);
    const bool down=x>=0 && x<256 && y>=0 && y<192;
    if (down) { touch_[0]=static_cast<uint16_t>(x); touch_[1]=static_cast<uint16_t>(y); }
    touch_[2]=static_cast<uint16_t>(down); touch_[3]=0;
    update_sounds();
    return static_cast<int>(abi_func_ov007_020b7090(touch_[0],touch_[1],touch_[2],touch_[3],keys));
}
void FrontendCore::render() {
    Scope scope(*this);
    ntr::gx_begin_frame();
    abi_func_ov007_020b7040();
    abi_func_ov007_020b6eb4();
}
void FrontendCore::retire_sound(uint32_t token) {
    auto found=voices_.find(token);
    if (found==voices_.end()) return;
    auto handle=packed::Ptr<uint32_t>(found->second.handle);
    if (*handle==token) *handle=0;
    voices_.erase(found);
}
void FrontendCore::update_sounds() {
    if (!audio_) return;
    for (auto it=voices_.begin();it!=voices_.end();) {
        const auto token=it->first; const auto voice=it->second; ++it;
        if (!audio_({SoundOp::Status,voice.player,token})) retire_sound(token);
    }
}
int FrontendCore::start_sound(uint32_t handle,unsigned archive,int entry) {
    if (!audio_) throw std::runtime_error("No native frontend audio backend.");
    auto selected=voices_.end();
    int player=2;
    for (;player<16;++player) {
        bool used=false;
        for (const auto &v:voices_) used|=v.second.player==player;
        if (!used) break;
    }
    if (player==16) {
        for (auto it=voices_.begin();it!=voices_.end();++it)
            if (selected==voices_.end() || it->second.priority<selected->second.priority) selected=it;
        player=selected->second.player;
        if (!audio_({SoundOp::Stop,player,selected->first})) return 0;
        retire_sound(selected->first);
    }
    const auto token=nextVoice_++;
    if (!token) throw std::runtime_error("Native sound identifiers exhausted.");
    if (!audio_({SoundOp::Effect,player,token,static_cast<int>(archive),entry})) return 0;
    voices_.emplace(token,Voice{player,handle,64,false});
    *packed::Ptr<uint32_t>(handle)=token;
    return 1;
}
int FrontendCore::control_sound(uint32_t handle,SoundOp op,int a,int b,int c) {
    const auto token=word_at(handle); auto found=voices_.find(token);
    if (found==voices_.end()) return 0;
    if (op==SoundOp::Stop && found->second.stopping) return 1;
    int result=audio_({op,found->second.player,token,a,b,c});
    if (result && op==SoundOp::Stop) found->second.stopping=true;
    return result;
}
void FrontendCore::sound_priority(uint32_t handle,uint8_t priority) {
    auto found=voices_.find(word_at(handle));
    if (found!=voices_.end()) found->second.priority=priority;
}
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
