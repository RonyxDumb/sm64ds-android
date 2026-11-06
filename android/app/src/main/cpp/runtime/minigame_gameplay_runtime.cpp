#include "minigame_gameplay_runtime.h"
#include "minigame_shared.h"
#include "minigame_scene_constructors.h"
#include "stage_runtime.h"
#include "resources.h"
#include "game_program.h"
#include "boot_seams.h"
#include "wipe_runtime.h"
#include "actor_dispatch.h"
#include "packed_ptr.h"
#include "ntr/gx.h"
#include "ntr/mmio.h"
#include "ntr/lz77.h"
#include "ntr/fs.h"
#include <cstring>
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <utility>
#include <unordered_set>

namespace sm64ds::frontend { void bind_arm9_sources(packed::Space &); }
namespace sm64ds::stage_sources { void bind_sources(packed::Space &); }
namespace sm64ds::minigame_sources { void bind_overlay_sources(packed::Space &,unsigned); }
int ApproachLinear(int &,int,int);
extern "C" {
void abi_mg_InitNativeStatics();
void pk_mg_Cache(uint32_t,uint32_t);
void abi_mg_OamReset(); void abi_mg_OamFlush(); void abi_mg_OamLoad();
void pk_mg_DecompressLZ16(sm64ds::packed::Ptr<void>,sm64ds::packed::Ptr<void>);
void pk_mg_LoadGroup(int,int); void pk_mg_Music(int); void pk_mg_StopMusic(unsigned);
void SetSoundMode(int); void TurnBacklightOn(); void TurnBacklightOff();
int GetSoundMode();
}
namespace {
using sm64ds::packed::Ptr;
using sm64ds::packed::Space;
thread_local sm64ds::MinigameGameplayRuntime *gameOwner=nullptr;
thread_local sm64ds::MinigameGameplayRuntime *liveGame=nullptr;
template<class T> T &at(uint32_t a) { return *Ptr<T>(a); }
uint32_t game_load(int handle) {
    if (handle<0) throw std::out_of_range("Negative minigame asset handle.");
    return sm64ds::MinigameGameplayRuntime::current().load(static_cast<uint32_t>(handle));
}
uint32_t game_allocate(uint32_t bytes) { return Space::current().allocate(bytes); }
uint32_t game_path(uint32_t path) { return sm64ds::MinigameGameplayRuntime::current().load_path(path); }
void game_free(uint32_t address) { if (address) sm64ds::MinigameGameplayRuntime::current().free(address); }
uint32_t game_compressed(int handle,uint32_t target) {
    const auto file=game_load(handle);
    const auto size=Space::current().allocation_size(file);
    const int expanded=ntr::lz77_size(static_cast<const uint8_t *>(Space::current().data(file,size)),static_cast<int>(size));
    try { pk_mg_DecompressLZ16(Ptr<void>(file),Ptr<void>(target)); }
    catch (...) { game_free(file); throw; }
    game_free(file); return static_cast<uint32_t>(expanded);
}
void game_reset_files() {
    // func_02019028 resets the 2D graphics manager, not cartridge bytes.
    ntr::io_write(0x04000304,ntr::io_read(0x04000304,2)&~12u,2);
    at<uint8_t>(0x0208ee00)=0;
}
void game_stop_effects() {
    // Stop the native effect players owned by the scene sound manager.
    auto &program=sm64ds::GameProgram::active();
    for (int player=1;player<4;++player)
        program.native_audio({sm64ds::SoundOp::Stop,player,0});
}
int game_effect(unsigned entry) { return sm64ds::GameProgram::active().sound(2,entry)?1:0; }
uint32_t nextEffectToken=0x200000;
void game_effect_params(int bank,int entry,int tracks,int pan) {
    auto &program=sm64ds::GameProgram::active();
    const uint32_t token=++nextEffectToken;
    if (program.native_audio({sm64ds::SoundOp::Effect,1,token,bank,entry}))
        program.native_audio({sm64ds::SoundOp::TrackParam,1,token,tracks,9,pan});
}
int game_effect_pan(int x) {
    // func_020126e8: screen Fix12 x -> original 26..102 stereo pan.
    return static_cast<int>(std::clamp<int64_t>(int64_t(x)*0x4c/0x100000+0x1a,0x1a,0x66));
}
void game_effect_at(int entry,int x) { game_effect_params(2,entry,0xffff,game_effect_pan(x)-64); }
int game_effect_if_idle(int previous,int archive,int entry,int) {
    auto &program=sm64ds::GameProgram::active();
    if (previous>0 && program.native_audio({sm64ds::SoundOp::Status,1,static_cast<uint32_t>(previous)})) return previous;
    const uint32_t token=++nextEffectToken;
    return program.native_audio({sm64ds::SoundOp::Effect,1,token,archive,entry}) ? static_cast<int>(token) : 0;
}
int game_effect_configured(int previous,int archive,int entry,int flags,int volume,int pitch,int pan,short priority) {
    const int token=game_effect_if_idle(previous,archive,entry,priority);
    if (token) {
        auto &program=sm64ds::GameProgram::active();
        if (flags&1) program.native_audio({sm64ds::SoundOp::TrackParam,1,static_cast<uint32_t>(token),0xffff,0x0a,volume});
        if (flags&2) program.native_audio({sm64ds::SoundOp::TrackParam,1,static_cast<uint32_t>(token),0xffff,0x0c,pitch});
        if (flags&4) program.native_audio({sm64ds::SoundOp::TrackParam,1,static_cast<uint32_t>(token),0xffff,9,pan-64});
    }
    return token;
}
int game_submusic(unsigned entry,unsigned,unsigned volume,int,bool start) {
    // PlaySub's zero-volume form stops its current jingle.
    auto &program=sm64ds::GameProgram::active();
    if (!start || !volume) return program.native_audio({sm64ds::SoundOp::Stop,1,0});
    const uint32_t token=++nextEffectToken;
    if (!program.native_audio({sm64ds::SoundOp::Effect,1,token,0,static_cast<int>(entry)}))
        throw std::runtime_error("Could not play original minigame jingle.");
    return 1;
}
int game_sound_mode() { return GetSoundMode(); }
void game_set_sound_mode(int mode) {
    SetSoundMode(mode); at<uint8_t>(0x0209d4b4)=static_cast<uint8_t>(mode);
}
void game_backlight_on() { TurnBacklightOn(); at<uint8_t>(0x0208ee3c)=1; }
void game_backlight_off() { TurnBacklightOff(); at<uint8_t>(0x0208ee3c)=0; }
int game_sqrt(uint64_t value) { return cstd::sqrt(value); }

// Native implementations of the original dScene_c lifecycle. Faders remain
// native objects; the scene and actor-manager fields remain DS-width records.
int game_scene_before_init(uint32_t actor) {
    at<uint32_t>(0x0209f5c0)=actor;
    dScene_c::SetFaders(&data_0209f5e8);
    at<uint32_t>(0x0209f1e4)=0;
    game_stop_effects();
    // dScene_c::BeforeInitResources also executes its full original graphics
    // reset. Title display-base/window state must not survive into a game.
    Ptr<void()>(0x0202e6c8u)();
    return 1; // fBase_c::BeforeInitResources is true
}
int game_scene_before_render(uint32_t actor) { return sm64ds::native_fbase_before_render(actor); }
void game_scene_after_cleanup(uint32_t actor,uint32_t result) {
    if (result==2) data_02092660=0;
    Ptr<void(uint32_t,uint32_t)>(0x02043b2c)(actor,result);
}
void game_set_irq(uint32_t mask,uint32_t handler) {
    if (mask!=2) throw std::runtime_error("Unexpected minigame IRQ mask.");
    at<uint32_t>(0x02099fe4+4)=handler;
    // Resolve now, before the scanout callback can outlive its packed scope.
    if (handler) (void)Space::current().code(handler);
    sm64ds::wipe_detail::set_external_handlers(
        handler?+[](){ if (liveGame) liveGame->invoke_irq(false); }:nullptr,
        handler?+[](){ if (liveGame) liveGame->invoke_irq(true); }:nullptr);
}
uint32_t game_get_irq(uint32_t mask) {
    if (mask!=2) throw std::runtime_error("Unexpected minigame IRQ query.");
    return at<uint32_t>(0x02099fe4+4);
}
uint32_t game_irq_enable(uint32_t mask) { return sm64ds::wipe_detail::enable(mask); }
uint32_t game_irq_disable(uint32_t mask) { return sm64ds::wipe_detail::disable(mask); }
void game_null_destructor(uint32_t) {} // original NullDestructor_0203d47c

void bind_game_services(Space &space) {
    for (uint32_t address:{0x0201816cu,0x020adc68u}) space.bind(address,&game_load);
    for (uint32_t address:{0x020182bcu,0x020adc74u}) space.bind(address,&game_path);
    for (uint32_t address:{0x02018144u,0x020adc5cu}) space.bind(address,&game_free);
    space.bind(0x0205a754,&pk_mg_DecompressLZ16);
    for (uint32_t address:{0x02058c08u,0x02058c24u,0x02058c40u,0x02058c68u})
        space.bind(address,&pk_mg_Cache);
    space.bind(0x02019028,&game_reset_files);
    space.bind(0x0202e3a4,&game_scene_before_render);
    space.bind(0x0202e3d4,&sm64ds::scene_before_behavior);
    space.bind(0x020431c4,&sm64ds::scene_waiting_children);
    space.bind(0x02043c80,&sm64ds::native_fbase_init);
    space.bind(0x02043b24,&sm64ds::native_fbase_behavior);
    space.bind(0x02043af0,&sm64ds::native_fbase_render);
    space.bind(0x02043afc,&sm64ds::native_fbase_before_behavior);
    space.bind(0x02043ac8,&sm64ds::native_fbase_before_render);
    space.bind(0x02043fdc,&sm64ds::native_fbase_process_list);
    space.bind(0x02043af8,&sm64ds::fbase_after_process);
    space.bind(0x02043ac4,&sm64ds::fbase_after_process);
    space.bind(0x0202e3c8,&sm64ds::fbase_after_process);
    space.bind(0x0202e398,&sm64ds::fbase_after_process);
    space.bind(0x0202e5d0,&game_scene_after_cleanup);
    space.bind(0x02012790,&game_effect);
    space.bind(0x020127a4,&game_effect_params);
    space.bind(0x02012754,&game_effect);
    space.bind(0x020126e8,&game_effect_pan);
    space.bind(0x02012718,&game_effect_at);
    space.bind(0x02012468,&game_effect_configured);
    space.bind(0x020124c4,&game_effect_if_idle);
    space.bind(0x02013050,&pk_mg_StopMusic);
    space.bind(0x0201320c,&pk_mg_Music);
    space.bind(0x02012be0,&game_submusic);
    space.bind(0x02056f70,&game_set_irq);
    space.bind(0x02056ee4,&game_get_irq);
    space.bind(0x02056de0,&game_irq_enable);
    space.bind(0x02056d98,&game_irq_disable);
    space.bind(0x0203d744,&game_sqrt);
    space.bind(0x0203c1c4,&game_allocate);
    space.bind(0x0203c1b4,&game_free);
    space.bind(0x0203d47c,&game_null_destructor);
    space.bind(0x02019574,&game_sound_mode);
    space.bind(0x02019510,&game_set_sound_mode);
    space.bind(0x02019698,&game_backlight_on);
    space.bind(0x02019664,&game_backlight_off);
    // Resolve these SDK names from the pinned symbol map below, alongside
    // the original controller's score and sound mode imports.
}
int virtual_word(uint32_t actor,int slot,int a=0,int b=0,int c=0,int d=0) {
    const uint32_t vptr=at<uint32_t>(actor);
    const uint32_t code=at<uint32_t>(vptr+static_cast<uint32_t>(slot)*4);
    return Ptr<int(uint32_t,int,int,int,int)>(code)(actor,a,b,c,d);
}
}
namespace sm64ds {
MinigameGameplayRuntime::MinigameGameplayRuntime(GameHeap &heap, Cartridge &cartridge,
                                                 uint32_t scene, uint32_t parameter, const uint8_t *selectedRow)
    : space_(heap), cartridge_(cartridge), arm9_(cartridge.arm9_data(0x020aa420u)),
      common_(cartridge.overlay_data(4)), game_(cartridge.overlay_data(6)),
      scene_(scene), parameter_(parameter) {
    map_minigame_live_arm9(space_,arm9_);
    // The selected 0x34-byte ROM row belongs to mutable menu ARM9 globals,
    // NOT immutable cartridge data. Carry the original menu's exact bytes
    // into the new overlay-6 session before constructing its scene.
    if (selectedRow)
        std::memcpy(space_.data(0x0209b308u,0x34u),selectedRow,0x34u);
    space_.map(common_.base,common_.bytes.data(),common_.bytes.size());
    // The game overlay replaces the menu overlay at the identical DS address.
    // Never map both overlay 5 and 6 into one address space.
    space_.map(game_.base,game_.bytes.data(),game_.bytes.size());
    for (const auto &span:{std::make_pair(ntr::IO_BASE,ntr::IO_SIZE),
                           std::make_pair(ntr::VRAM_BASE,ntr::VRAM_SIZE),
                           std::make_pair(ntr::PLTT_BASE,ntr::PLTT_SIZE),
                           std::make_pair(ntr::OAM_BASE,ntr::OAM_SIZE)})
        space_.map(span.first,ntr::host_ptr(span.first),span.second);
    frontend::bind_arm9_sources(space_);
    stage_sources::bind_sources(space_);
    bind_stage_runtime(space_);
    bind_minigame_shared_runtime(space_);
    minigame_sources::bind_overlay_sources(space_,6);
    bind_game_services(space_);
    validate_minigame_shared_runtime(space_);
    bind_minigame_scene_factory(space_,game_,scene_);
    Scope scope(*this);
    packed::Ptr<void()>(0x02075154u)(); // original ARM9 process-list/scene-tree initializer
    abi_mg_InitNativeStatics();
    liveGame=this;
}
MinigameGameplayRuntime &MinigameGameplayRuntime::current() {
    if (!gameOwner) throw std::runtime_error("No native gameplay minigame owner.");
    return *gameOwner;
}
bool MinigameGameplayRuntime::active() { return gameOwner!=nullptr; }
MinigameGameplayRuntime::Scope::Scope(MinigameGameplayRuntime &next):address_(next.space_),previous_(gameOwner) { gameOwner=&next; }
MinigameGameplayRuntime::Scope::~Scope() { gameOwner=previous_; }
MinigameGameplayRuntime::~MinigameGameplayRuntime() {
    if (liveGame==this) {
        wipe_detail::set_external_handlers(nullptr,nullptr);
        liveGame=nullptr;
    }
}
void MinigameGameplayRuntime::invoke_irq(bool tableFlip) {
    Scope scope(*this);
    const uint32_t handler=at<uint32_t>(0x02099fe4+4);
    if (!handler) return;
    const auto line=ntr::io_read(0x04000006,2);
    if (tableFlip) ntr::io_write(0x04000006,191,2);
    Ptr<void()>{handler}();
    if (tableFlip) ntr::io_write(0x04000006,line,2);
}
uint32_t MinigameGameplayRuntime::load(uint32_t handle) {
    const auto bytes=cartridge_.file(handle,false);
    if (bytes.empty() || bytes.size()>16*1024*1024) throw std::runtime_error("Invalid minigame resource length.");
    const auto address=space_.allocate(static_cast<uint32_t>(bytes.size()));
    std::memcpy(space_.data(address,bytes.size()),bytes.data(),bytes.size());
    return address;
}
uint32_t MinigameGameplayRuntime::load_path(uint32_t path) {
    std::string name;
    for (size_t i=0;i<1024;++i) {
        const char c=*Ptr<char>(path+static_cast<uint32_t>(i));
        if (!c) {
            // Ov004_LoadFileByPath uses FS paths (Wanted uses /MG/...),
            // not overlay-0 asset handles. Nitro accepts a leading root.
            const auto first=name.find_first_not_of('/');
            if (first==std::string::npos) throw std::runtime_error("Empty minigame resource path.");
            name.erase(0,first);
            const int32_t id=ntr::fs_id_by_path(name.c_str());
            const int32_t bytes=ntr::fs_size(id);
            if (bytes<=0 || bytes>16*1024*1024)
                throw std::runtime_error("Missing original minigame file: "+name);
            const uint32_t address=space_.allocate(static_cast<uint32_t>(bytes));
            if (ntr::fs_read(id,space_.data(address,bytes),bytes)!=bytes) {
                space_.release(address);
                throw std::runtime_error("Could not read original minigame file: "+name);
            }
            return address;
        }
        name.push_back(c);
    }
    throw std::runtime_error("Unterminated original minigame resource path.");
}
void MinigameGameplayRuntime::free(uint32_t address) { space_.release(address); }
void MinigameGameplayRuntime::register_global(uint32_t object,uint32_t destructor,uint32_t node) {
    (void)space_.data(object,1); (void)space_.data(node,12);
    (void)space_.code(destructor);
    at<uint32_t>(node)=destructor; at<uint32_t>(node+4)=object;
    at<uint32_t>(node+8)=0;
    destructors_.emplace_back(object,destructor);
}
void MinigameGameplayRuntime::cleanup() {
    if (cleaned_) return;
    Scope scope(*this);
    if (actor_) {
        virtual_word(actor_,5,2);
        at<uint32_t>(0x020beb68)=0;
    }
    for (auto it=destructors_.rbegin();it!=destructors_.rend();++it)
        Ptr<void(uint32_t)>(it->second)(it->first);
    destructors_.clear();
    cleaned_=true;
}
uint32_t MinigameGameplayRuntime::spawn(uint8_t kind) {
    Scope scope(*this);
    using packed::Ptr;
    // Source path: dScene_c::SpawnIfNecessary -> scene profile -> factory.
    // Look up the actual original profile rather than a capability table.
    const uint32_t profile=*Ptr<uint32_t>(0x02090864u+scene_*4u);
    const uint32_t factory=*Ptr<uint32_t>(profile);
    // Mirror the original scene-manager profile-table startup. The boot
    // path did not run its usual overlay manager initialization.
    if (!*Ptr<uint32_t>(0x020a4bb8u))
        *Ptr<uint32_t>(0x020a4bb8u)=0x02090864u;
    *Ptr<uint16_t>(0x020a4b50u)=static_cast<uint16_t>(scene_);
    *Ptr<uint16_t>(0x020a4b54u)=static_cast<uint16_t>(scene_);
    *Ptr<uint32_t>(0x020a4b60u)=parameter_;
    *Ptr<uint8_t>(0x020a4b48u)=kind;
    // Only original recovered factories have native code bindings.
    actor_=Ptr<uint32_t()>(factory)();
    if (actor_) {
        for (uint32_t i=0;i<8;++i) at<uint32_t>(actor_+0xf4+i*0x24)=0x020bca84;
        Ptr<void(uint32_t)>(0x020b92c4)(actor_+0xf4); // original options placement constructor
        *Ptr<uint8_t>(0x020a4b4cu)=5;
        Ptr<void(uint32_t)>(0x020433b8u)(actor_); // actor post-construction
        *Ptr<uint8_t>(0x020a4b4cu)=0;
        *Ptr<uint16_t>(0x020a4b50u)=0xffffu;
    }
    return actor_;
}
void MinigameGameplayRuntime::update() {
    Scope scope(*this);
    ntr::gx_begin_frame();
    abi_mg_OamReset();
    // Actual recovered actor scheduler. Further missing overlay-6 component
    // callbacks will likewise surface as unlinked original code handles.
    packed::Ptr<void()>(0x02044120u)();
    if (actor_ && at<uint8_t>(actor_+14)==1) {
        virtual_word(actor_,22);
        virtual_word(actor_,24);
        virtual_word(actor_,25);
    }
    abi_mg_OamFlush(); abi_mg_OamLoad();
    for (auto item:{std::pair<uint32_t,uint32_t>{0x04000000,0x0209d45c},
                    {0x04001000,0x0209d454}})
        ntr::io_write(item.first,(ntr::io_read(item.first,4)&~0x1f00u)|
                      (uint32_t(at<uint8_t>(item.second)&31)<<8),4);
    constexpr uint32_t offsets[8][2]={{0x0209d468,0x0209d46c},{0x0209d4a4,0x0209d4a0},
        {0x0209d49c,0x0209d478},{0x0209d48c,0x0209d490},{0x0209d494,0x0209d498},
        {0x0209d484,0x0209d480},{0x0209d47c,0x0209d470},{0x0209d474,0x0209d488}};
    for (unsigned i=0;i<8;++i) {
        const uint32_t port=0x04000010+(i/4)*0x1000+(i%4)*4;
        ntr::io_write(port,uint32_t(at<uint16_t>(offsets[i][0])&511)|
                     uint32_t(at<uint16_t>(offsets[i][1])&511)<<16,4);
    }
}
}

extern "C" {
void pk_mg_GameStopEffects() { game_stop_effects(); }
void pk_mg_GameLoadGroup(int group,int bank) { pk_mg_LoadGroup(group,bank); }
uint32_t pk_mg_GameCompressed(int handle,Ptr<void> destination) { return game_compressed(handle,destination.address()); }
int pk_mg_GameBeforeInit(Ptr<void> actor) { return game_scene_before_init(actor.address()); }
int pk_mg_NativeVirtual(Ptr<void> actor,int slot,int a,int b,int c,int d) {
    if (slot<0 || slot>=37) throw std::out_of_range("Invalid original minigame virtual slot.");
    return virtual_word(actor.address(),slot,a,b,c,d);
}
void pk_mg_NativeInvoke(Ptr<void> object,Ptr<sm64ds::packed::MemberCall> call) {
    const auto target=Space::current().resolve_member(object.address(),*call);
    void (*fn)(uint32_t)=nullptr; std::memcpy(&fn,&target.code,sizeof fn); fn(target.object);
}
void pk_mg_NativeInvokeSlot(Ptr<void> object,Ptr<sm64ds::packed::MemberCall> call,int arg) {
    const auto target=Space::current().resolve_member(object.address(),*call);
    void (*fn)(uint32_t,int)=nullptr; std::memcpy(&fn,&target.code,sizeof fn); fn(target.object,arg);
}
int pk_mg_NativeApproach(Ptr<int> value,int target,int step) {
    return ApproachLinear(*value,target,step);
}
int pk_mg_NativeApproachShort(Ptr<short> value,short target,short step) {
    // _Z15ApproachLinear2Rsss, EU ARM9 0x0203aed8: the store/reload
    // truncates to signed 16 bits before the signed crossing test.
    if (step) {
        if (*value>target) step=static_cast<int16_t>(-int32_t(step));
        *value=static_cast<int16_t>(int32_t(*value)+step);
        if (int32_t(step)*(int32_t(*value)-target)>=0) {
            *value=target; return 1;
        }
    } else if (*value==target) return 1;
    return 0;
}
void pk_mg_RegisterGlobal(Ptr<void> object,Ptr<void> destructor,Ptr<void> node) {
    sm64ds::MinigameGameplayRuntime::current().register_global(object.address(),destructor.address(),node.address());
}
}
