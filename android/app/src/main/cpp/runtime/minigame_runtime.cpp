#include "minigame_runtime.h"
#include "game_program.h"
#include "boot_seams.h"
#include "minigame_progress.h"
#include "minigame_shared.h"
#include "wipe_runtime.h"
#include "resources.h"
#include "packed_ptr.h"
#include "pc_audio/archive.h"
#include "ntr/gx.h"
#include "ntr/lz77.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <stdexcept>

using sm64ds::packed::Ptr;
using sm64ds::packed::Space;
static_assert(sizeof(Ptr<char>)==sizeof(uint32_t), "Original native minigame pointer ABI");
extern "C" {
uint32_t abi_mg_MenuInitResources(uint32_t);
uint32_t abi_mg_MenuCleanupResources(uint32_t);
uint32_t abi_mg_MenuBehavior(uint32_t);
uint32_t abi_mg_MenuRender(uint32_t);
void abi_mg_OamReset(); void abi_mg_OamFlush(); void abi_mg_OamLoad();
void pk_G3X_SetFog(int,int,int,int);
int pk_func_02053e34(); int pk_func_02053e1c();
extern unsigned char gTouchHeld[],gActivePlayerSlot;
extern int data_0209caa0[];
}
namespace {
thread_local sm64ds::MinigameRuntime *owner=nullptr;
template<class T> T &at(uint32_t a) { return *Ptr<T>(a); }
sm64ds::MinigameRuntime &current() { return sm64ds::MinigameRuntime::current(); }
void offset(uint32_t a,int x,int y,uint32_t mirror) {
    at<short>(mirror)=static_cast<short>(x);
    at<short>(mirror+2)=static_cast<short>(y);
    ntr::io_write(a,(uint32_t(y&511)<<16)|uint32_t(x&511),4);
}
}
namespace sm64ds {
MinigameRuntime &MinigameRuntime::current() {
    if (!owner) throw std::runtime_error("No native minigame scene owner.");
    return *owner;
}
MinigameRuntime::Scope::Scope(MinigameRuntime &next):address_(next.space_),previous_(owner) { owner=&next; }
MinigameRuntime::Scope::~Scope() { owner=previous_; }
MinigameRuntime::MinigameRuntime(Cartridge &cartridge,GameProgram &program,unsigned group,unsigned page)
    :space_(Resources::active().heap()),cartridge_(cartridge),program_(program),
     arm9_(cartridge.arm9_data(0x020aa420u)),common_(cartridge.overlay_data(4)),
     menu_(cartridge.overlay_data(5)) {
    if (group>4 || page>1) throw std::out_of_range("Invalid original menu state.");
    if (common_.base!=0x020ad660u || menu_.base!=0x020bfec0u ||
        program.minigames().menu_table_address()!=0x020c24d8u)
        throw std::runtime_error("Unsupported original minigame overlay layout.");
    // Keep pointer-free save and input records LIVE. Split the ARM9 image at
    // these spans so the projected source and original ARM9 leaf helpers use
    // exactly the same bytes, including all interior aliases.
    struct Live { uint32_t address; void *bytes; size_t size; };
    const Live live[]={{0x0209caa0u,data_0209caa0,0x32c},
                       {0x0209f5f8u,&data_0209f5f8,1},
                       {0x020a0de8u,gTouchHeld,16},
                       {0x020a0e40u,&gActivePlayerSlot,1},
                       {0x020a0e58u,data_020a0e58,16}};
    uint32_t begin=arm9_.base;
    for (const auto &span:live) {
        space_.map(begin,arm9_.bytes.data()+begin-arm9_.base,span.address-begin);
        space_.map(span.address,span.bytes,span.size);
        begin=span.address+static_cast<uint32_t>(span.size);
    }
    space_.map(begin,arm9_.bytes.data()+begin-arm9_.base,
               arm9_.bytes.size()-(begin-arm9_.base));
    for(auto &part:arm9_.relocated) space_.map(part.base,part.bytes.data(),part.bytes.size());
    space_.map(common_.base,common_.bytes.data(),common_.bytes.size());
    space_.map(menu_.base,menu_.bytes.data(),menu_.bytes.size());
    bind_minigame_shared_runtime(space_);
    validate_minigame_shared_runtime(space_);
    for (const auto &span:{std::make_pair(ntr::IO_BASE,ntr::IO_SIZE),
                           std::make_pair(ntr::VRAM_BASE,ntr::VRAM_SIZE),
                           std::make_pair(ntr::PLTT_BASE,ntr::PLTT_SIZE),
                           std::make_pair(ntr::OAM_BASE,ntr::OAM_SIZE)})
        space_.map(span.first,ntr::host_ptr(span.first),span.second);
    Scope scope(*this);
    if (group==4) group=static_cast<unsigned>(at<int>(0x0208a170u));
    if (group>3) throw std::runtime_error("Invalid original minigame initial character.");
    at<int>(0x0208a170u)=group; at<int>(0x0208a174u)=-1;
    at<uint8_t>(0x0209b304u)=static_cast<uint8_t>(page);
    actor_=space_.allocate(0xb0);
    if (abi_mg_MenuInitResources(actor_)!=1)
        throw std::runtime_error("Original minigame menu initialization failed.");
}
MinigameRuntime::~MinigameRuntime() = default;
void MinigameRuntime::cleanup() {
    if (cleaned_) return;
    Scope scope(*this);
    if (abi_mg_MenuCleanupResources(actor_)!=1)
        throw std::runtime_error("Original minigame menu cleanup failed.");
    // Cleanup can clear a NEW badge. Persist it before this address space dies.
    if (at<uint8_t>(0x0209b300u) && !MinigameProgress::persist())
        throw std::runtime_error("Could not persist original minigame records.");
    cleaned_=true;
}
void MinigameRuntime::update() {
    Scope scope(*this);
    if (abi_mg_MenuBehavior(actor_)!=1)
        throw std::runtime_error("Original minigame menu behavior failed.");
    // The ARM9 display/VBlank job normally commits these shadow registers.
    // Keep the exact menu's scrolling values for BG0 and BG3 on engine B.
    for (const auto &v:{std::make_pair(0x04001010u,0x0209d474u),
                        std::make_pair(0x0400101cu,0x0209d494u)})
        offset(v.first,at<short>(v.second),at<short>(v.second+2),v.second);
}
void MinigameRuntime::render() {
    Scope scope(*this);
    ntr::gx_begin_frame();
    abi_mg_OamReset();
    if (abi_mg_MenuRender(actor_)!=1)
        throw std::runtime_error("Original minigame menu render failed.");
    abi_mg_OamFlush(); abi_mg_OamLoad();
    for (const auto &v:{std::make_pair(0x04000000u,0x0209d45cu),
                        std::make_pair(0x04001000u,0x0209d454u)})
        ntr::io_write(v.first,(ntr::io_read(v.first,4)&~0x1f00u)|
                      (uint32_t(at<uint8_t>(v.second)&31)<<8),4);
}
unsigned MinigameRuntime::group() const { int n; std::memcpy(&n,space_.data(0x0208a170u,4),4); return n; }
unsigned MinigameRuntime::page() const { return *static_cast<const uint8_t *>(space_.data(0x0209b304u,1)); }
int MinigameRuntime::scroll() const { int n; std::memcpy(&n,space_.data(actor_+0x50,4),4); return n; }
int MinigameRuntime::language() const { return GetOwnerLanguage(); }
void MinigameRuntime::preload(int index) {
    if (index<0) throw std::out_of_range("Negative minigame archive.");
    cartridge_.preload_archive(static_cast<unsigned>(index));
}
uint32_t MinigameRuntime::load(int handle) {
    if (handle<0) throw std::out_of_range("Negative minigame resource.");
    const auto bytes=cartridge_.file(static_cast<unsigned>(handle),false);
    const uint32_t destination=space_.allocate(static_cast<uint32_t>(bytes.size()));
    std::memcpy(space_.data(destination,bytes.size()),bytes.data(),bytes.size());
    files_.emplace(destination,bytes.size());
    return destination;
}
void MinigameRuntime::free(uint32_t address) {
    if (!files_.count(address)) throw std::runtime_error("Unowned minigame file release.");
    space_.release(address); files_.erase(address);
}
void MinigameRuntime::reset_files() {
    while (!files_.empty()) free(files_.begin()->first);
    // Original func_02019028 restores the graphics-resource manager's mode.
    ntr::io_write(0x04000304,ntr::io_read(0x04000304,2)&~12u,2);
    at<uint8_t>(0x0208ee00u)=0;
}
}
extern "C" {
Ptr<char> pk_mg_LoadFile(int n) { return Ptr<char>(current().load(n)); }
void pk_mg_DecompressLZ16(Ptr<void> source,Ptr<void> target) {
    auto &space=Space::current();
    const auto available=space.allocation_size(source.address());
    if (available>INT32_MAX) throw std::runtime_error("Original minigame LZ source is too large.");
    const auto *input=static_cast<const uint8_t *>(source.get(available));
    const int expanded=ntr::lz77_size(input,static_cast<int>(available));
    if (expanded<=0 || expanded>0x20000)
        throw std::runtime_error("Invalid original minigame LZ extent.");
    // data() checks the complete destination window, including logical VRAM
    // handles and group-icon scratch buffers at character RAM +0x8000.
    auto *output=static_cast<uint8_t *>(target.get(static_cast<size_t>(expanded)));
    if (ntr::lz77_unpack(input,static_cast<int>(available),output,expanded)!=expanded)
        throw std::runtime_error("Corrupt original minigame LZ stream.");
}
void pk_mg_Free(Ptr<void> p) { current().free(p.address()); }
int pk_mg_LoadArchive(int n) { current().preload(n); return 1; }
void pk_mg_LoadTextNarcs() {
    current().preload(6);
    const int lang=current().language();
    current().preload(lang>=2?lang+7:8);
}
void pk_mg_ResetFiles() { current().reset_files(); }
int pk_mg_Language() { return GetOwnerLanguage(); }
void pk_mg_Cache(uint32_t address,uint32_t bytes) {
    if (bytes) (void)Space::current().data(address,bytes);
    std::atomic_thread_fence(std::memory_order_seq_cst);
}
void pk_mg_CopyWords(Ptr<int> source,Ptr<int> target,int bytes) {
    if (bytes<0 || bytes%4) throw std::runtime_error("Invalid OAM word-copy extent.");
    if (!bytes) return;
    const auto *in=source.get(bytes);
    auto *out=target.get(bytes);
    // Original CPU copies advance forwards. Reset intentionally overlaps:
    // copying each completed word seeds the following entries and chunks.
    for (int i=0;i<bytes/4;++i) out[i]=in[i];
}
void pk_mg_DisableBanks() {
    GX::DisableAllBanks();
    std::memset(Space::current().data(0x020a6088u,0x20),0,0x20);
}
void pk_mg_Fog(int a,int b,int c,int d) { pk_G3X_SetFog(a,b,c,d); }
void pk_mg_InitVram() {
    // Complete InitialiseVramGlobals body, using the recovered GX queries.
    const int r=pk_func_02053e34();
    at<int>(0x020a4be4)=0; at<int>(0x020a4bc8)=0;
    at<int>(0x020a4be0)=0x20000; at<int>(0x020a4be8)=0x20000;
    at<int>(0x020a4bdc)=r; at<int>(0x020a4bd8)=pk_func_02053e1c();
    at<int>(0x020a4bcc)=0;
}
void pk_mg_ResetGeometry() { at<uint8_t>(0x0209ee90u+837)=0; }
void pk_mg_LoadGroup(int group,int bank) {
    if (group<0 || !pc_group_ready(static_cast<unsigned>(group)))
        throw std::runtime_error("Original minigame audio group is unavailable.");
    at<int>(0x0209b47cu)=group; at<int>(0x0208e428u)=bank;
    at<uint8_t>(0x0209b478u)=0;
}
void pk_mg_UnsetVoice() { at<uint8_t>(0x0209b478u)=0; }
int pk_mg_Effect(int n) { return current().program().sound(2,static_cast<uint32_t>(n))?1:0; }
void pk_mg_Music(int n) {
    if (!current().program().native_audio({sm64ds::SoundOp::Music,0,0,n}))
        throw std::runtime_error("Could not start original minigame music.");
}
void pk_mg_StopMusic(unsigned frames) {
    if (!current().program().native_audio({sm64ds::SoundOp::Stop,0,0,static_cast<int>(frames)}))
        throw std::runtime_error("Could not stop original minigame music.");
}
int pk_mg_FaderReady() { return data_0209f5bc->IsAtStart(); }
void pk_mg_StopColorFader() { dScene_c::SetAndStopColorFader(); }
void pk_mg_SetFaders(Ptr<void> object) {
    if (object.address()!=0x0209f61c)
        throw std::runtime_error("Invalid original minigame wipe object.");
    dScene_c::SetFaders(&data_0209f61c);
}
void pk_mg_WipeType(Ptr<void> object,int type) {
    if (object.address()!=0x0209f61c || type<0 || type>2)
        throw std::runtime_error("Invalid original minigame wipe request.");
    data_0209f61c.func_0202ec9c(type);
}
void pk_mg_StartScene(int scene,int parameter,int color) {
    dScene_c::StartSceneFade(scene,parameter,static_cast<uint16_t>(color));
}
void pk_mg_ExitMenu() { ExitMinigameMenu(); }
int pk_mg_GetRecord(int slot,int rank) { return sm64ds::MinigameProgress::record(slot,rank); }
void pk_mg_SetRecord(int slot,int rank,int value) { sm64ds::MinigameProgress::set_record(slot,rank,value); }
int pk_mg_IsUnlocked(int n) { return sm64ds::MinigameProgress::unlocked(n+8)?1:0; }
int pk_mg_IsNew(int n) {
    if (n<0 || n>=36) throw std::out_of_range("Invalid minigame badge index.");
    return sm64ds::MinigameProgress::is_new(n>=28?n-28:n+8)?1:0;
}
void pk_mg_ClearNew(int n) {
    if (n<0 || n>=36) throw std::out_of_range("Invalid minigame badge index.");
    sm64ds::MinigameProgress::clear_new(n>=28?n-28:n+8);
}
void pk_mg_Save(Ptr<void> record) {
    if (record.address()!=0x0209cae4u || !sm64ds::MinigameProgress::persist())
        throw std::runtime_error("Could not save original minigame records.");
}
void pk_mg_SetBg2Offset(int x,int y) { offset(0x04000018,x,y,0x0209d484); }
void pk_mg_SetSubBg0Offset(int x,int y) { offset(0x04001010,x,y,0x0209d474); }
void pk_mg_SetSubBg2Offset(int x,int y) { offset(0x04001018,x,y,0x0209d484); }
void pk_mg_SetSubBg3Offset(int x,int y) { offset(0x0400101c,x,y,0x0209d494); }
}
