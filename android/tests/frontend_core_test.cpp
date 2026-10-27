#include "runtime/frontend_core.h"
#include "runtime/packed_ptr.h"
#include "runtime/actor_dispatch.h"
#include "ntr/mmio.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <sys/mman.h>
#include <stdexcept>
#include <thread>

using namespace sm64ds;
namespace {
int actorTestMode = 0;
unsigned actorVisitCount = 0;
uint32_t actorVisited[3]{};
uint32_t actorLastStatus = 0xffffffffu;
}
extern "C" bool actor_test_before(uint32_t) { return actorTestMode != 0; }
extern "C" int actor_test_action(uint32_t) {
    return actorTestMode == 2 ? -1 : actorTestMode == 3 ? 0 : 1;
}
extern "C" void actor_test_after(uint32_t, uint32_t status) {
    actorLastStatus = status;
}
extern "C" int actor_list_callback(uint32_t owner) {
    if(actorVisitCount<3) actorVisited[actorVisitCount]=owner;
    ++actorVisitCount;
    return 1;
}
extern "C" void abi__ZN15dExtFrameCtrl_c17UpdateFileOffsetsER8BCA_File(uint32_t);
extern "C" void abi__ZN15dExtAnmTexPat_c17UpdateFileOffsetsER8BTP_File(uint32_t);
extern "C" uint32_t native_member_probe(uint32_t object, uint32_t value) {
    return object ^ value;
}
template<class T> T at(const FrontendCore &c, uint32_t a) {
    T v; std::memcpy(&v,c.data_at(a,sizeof v),sizeof v); return v;
}
namespace {
// Force a real new-thread stack object so ASan's automatically mapped fake
// stack cannot hide a stale constructor-thread mapping in this check.
__attribute__((no_sanitize("address"))) void migrate_thread(FrontendCore &core) {
    FrontendCore::Scope scope(core);
    uint32_t local=0x12345678;
    packed::Ptr<uint32_t> handle(&local);
    assert(handle.get()==&local && *handle==0x12345678);
    core.sample(0x41,7,9);
}
void put(std::vector<uint8_t> &v, size_t a, uint32_t value, unsigned bytes=4) {
    assert(a+bytes<=v.size());
    for (unsigned i=0;i<bytes;++i) v[a+i]=uint8_t(value>>(8*i));
}
std::vector<uint8_t> model_fixture() {
    // Invented data, not cartridge assets: eleven tiny paletted texture pairs.
    std::vector<uint8_t> v(0x1000);
    put(v,0x14,11); put(v,0x18,0x38); put(v,0x1c,11); put(v,0x20,0x114);
    for (unsigned i=0;i<11;++i) {
        put(v,0x38+i*20+4,0x300+i*64); put(v,0x38+i*20+8,64);
        put(v,0x38+i*20+16,4u<<26);
        put(v,0x114+i*16+4,0x700+i*32); put(v,0x114+i*16+8,32);
    }
    return v;
}
Cartridge::DataImage factory_fixture() {
    Cartridge::DataImage image{0x020c0000,std::vector<uint8_t>(0x45000)};
    auto set=[&](uint32_t a,uint32_t value,unsigned n=4) { put(image.bytes,a-image.base,value,n); };
    auto binary=[&](uint32_t a,const std::vector<std::vector<uint8_t>> &blocks) {
        set(a+14,blocks.size(),2);
        uint32_t pos=a+16;
        for (const auto &b:blocks) {
            set(pos+4,b.size()+8);
            std::memcpy(image.bytes.data()+pos+8-image.base,b.data(),b.size());
            pos+=8+b.size();
        }
    };
    std::vector<uint8_t> character(12+32), palette(8+0x200), entries(28), groups(40);
    put(character,0,1); put(character,4,1);
    put(palette,0,1); put(palette,4,0xd0);
    put(entries,0,1); put(entries,4,1);
    put(groups,0,1); put(groups,12,1,1);
    binary(0x020f8000,{character}); binary(0x020f8100,{palette});
    binary(0x020f8400,{entries,std::vector<uint8_t>(4),std::vector<uint8_t>(4),groups});
    set(0x020d7db4,4096);
    set(0x020d76a2,16,1); set(0x020d76a3,16,1);
    set(0x020d7648,uint32_t(-4096));
    for (unsigned i=0;i<24;++i) {
        set(0x020d77d8+i*12,1);
        set(0x020d77e0+i*12,1,1);
    }
    for (unsigned i=0;i<9;++i) {
        set(0x020ccb7d+i*8,1,1);
        set(0x020ccb80+i*8,0x020f8300);
    }
    for (unsigned i=0;i<4;++i) set(0x020d7e28+i*4,0x020f8000);
    for (unsigned i=0;i<6;++i) {
        set(0x020d7e38+i*4,0x020f8100); set(0x020d7e50+i*4,0x020f8400);
    }
    return image;
}
}
int main() {
    GameHeap heap;
    size_t available=heap.memory_left();
    for (int life=0; life<3; ++life) {
        Cartridge::DataImage image{0x020d0000,std::vector<uint8_t>(0x35000)};
        int one=4096;
        std::memcpy(image.bytes.data()+0x7db4,&one,4);
        FrontendCore c(heap,std::move(image),4);
        NTR_MMIO(uint16_t,0x04000304)=0xffff;
        NTR_MMIO(uint16_t,0x04000304)&=~0x8000;
        assert(ntr::io_read(0x04000304,2)==0x7fff);
        NTR_MMIO(uint16_t,0x04000306)=0x1234;
        NTR_MMIO(uint16_t,0x04000304)=NTR_MMIO(uint16_t,0x04000306);
        assert(ntr::io_read(0x04000304,2)==0x1234);
        assert(c.allocations()==23 && !c.has_items());
        uint32_t root=c.root_address();
        assert(c.word_at(0x0210342c)==root);
        assert(reinterpret_cast<uintptr_t>(c.data_at(root,0x188))>UINT32_MAX);
        assert(reinterpret_cast<uintptr_t>(c.data_at(root,0x188))!=root);
        assert(at<uint8_t>(c,c.word_at(root+0x28)+0x3a)==5);
        assert(c.word_at(c.word_at(root+0x44))==c.word_at(root+0x4c)+4);
        assert(c.word_at(c.word_at(root+0x48))==c.word_at(root+0x44)+4);
        for (unsigned i=0;i<3;++i) {
            auto state=c.word_at(root+4+i*4);
            assert(at<int16_t>(c,state)==(i==0?0:i==1?-1:1));
            c.step_state(i);
            assert(at<uint32_t>(c,state+12)==0);
            c.step_state(i);
            assert(at<uint32_t>(c,state+12)==1);
        }
        for (unsigned i=0;i<2;++i) {
            auto cam=c.word_at(root+0x30+i*4);
            assert(at<int>(c,cam+0x10)==4096);
            assert(at<int>(c,cam+0x24)==4096);
            assert(at<int64_t>(c,cam+0xbc)==0x155555555LL);
            for(unsigned j=0;j<12;++j) assert(at<int>(c,cam+0x44+j*4)==(j==0||j==4||j==8?4096:0));
            for(unsigned j=0;j<16;++j) assert(at<int>(c,cam+0x74+j*4)==(j==0||j==5||j==10||j==15?4096:0));
            auto light=c.word_at(root+0x38+i*4);
            assert(at<int16_t>(c,light+0x20)==-4096);
            auto tween=c.word_at(root+0x6c+i*4);
            assert(c.word_at(tween+0x10)==root+0x64+i*4);
        }
        auto pad=c.word_at(root+0x54), touch=c.word_at(root+0x50);
        c.sample(0x41,10,20);
        assert(at<uint16_t>(c,pad)==0x41);
        assert(at<uint32_t>(c,pad+4)==1);
        c.sample(0x41,14,25);
        assert(at<uint32_t>(c,pad+4)==2);
        assert(at<uint16_t>(c,touch)==14);
        c.sample(0,-1,-1);
        assert(at<uint16_t>(c,pad)==0);
        assert(at<uint32_t>(c,pad+0x34)==1);
        FrontendCore::Scope scope(c);
        auto p=packed::Ptr<int>(root);
        assert((p+1).address()==root+4);
        assert(packed::Ptr<int>(p.get()).address()==root);
        // Native new/malloc buffers are outside the DS game heap and ELF PT_LOAD.
        // Their handles must retain identity and never truncate ARM64 pointers.
        auto nativeBuffer=std::make_unique<uint32_t[]>(32);
        nativeBuffer[7]=0x12481248;
        auto nativeCell=packed::Ptr<uint32_t>(&nativeBuffer[7]);
        assert(*nativeCell==0x12481248);
        assert(nativeCell.get()==&nativeBuffer[7]);
        assert(packed::Ptr<uint32_t>(&nativeBuffer[8]).address()==nativeCell.address()+4);
        *nativeCell=0xaabbccdd;
        assert(nativeBuffer[7]==0xaabbccdd);
        // An inaccessible anonymous mapping must not become a data handle.
        void *guard=mmap(nullptr,4096,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        assert(guard!=MAP_FAILED);
        bool inaccessible=false;
        try { (void)packed::Ptr<uint8_t>(static_cast<uint8_t *>(guard)); }
        catch (const std::runtime_error &) { inaccessible=true; }
        assert(inaccessible);
        assert(munmap(guard,4096)==0);
        // An odd-address byte alias must not bind a misaligned u16[] reference
        // before the source casts it to its real byte view.
        auto alias=packed::ArrayRef<uint16_t>(root+1);
        assert(packed::Ptr<uint8_t>(alias).address()==root+1);
        auto &memory=packed::Space::current();
#if !defined(_WIN32)
        // Raw DS VRAMCNT writes must take effect before the source writes
        // graphics through a packed VRAM pointer. Waiting until PPU scanout
        // loses the upload in the previously selected anonymous window.
        const auto oldC=ntr::io_read(0x04000242u,1);
        const auto oldH=ntr::io_read(0x04000248u,1);
        ntr::io_write(0x04000242u,0,1); // avoid two banks at the same address
        auto *physicalH=static_cast<uint8_t *>(ntr::host_ptr(0x06898000u));
        *static_cast<volatile uint8_t *>(ntr::host_ptr(0x04000248u))=0x81;
        *static_cast<uint8_t *>(memory.data(0x06200000u,1))=0x5d;
        assert(physicalH[0]==0x5d);
        ntr::io_write(0x04000248u,oldH,1);
        ntr::io_write(0x04000242u,oldC,1);
#endif
        // Recovered fBase_c lifecycle callbacks are executable native
        // functions, with the exact 0x50-byte packed actor layout.
        memory.bind(0x02043afcu, &native_fbase_before_behavior);
        memory.bind(0x02043ac8u, &native_fbase_before_render);
        memory.bind(0x02043c80u, &native_fbase_init);
        auto baseActor = memory.allocate(0x50);
        assert(native_fbase_before_behavior(baseActor)==1);
        assert(native_fbase_before_render(baseActor)==1);
        assert(native_fbase_init(baseActor)==1);
        *packed::Ptr<uint8_t>(baseActor+0x13)=2;
        assert(native_fbase_before_behavior(baseActor)==0);
        assert(native_fbase_before_render(baseActor)==1);
        *packed::Ptr<uint8_t>(baseActor+0x13)=8;
        assert(native_fbase_before_behavior(baseActor)==1);
        assert(native_fbase_before_render(baseActor)==0);
        *packed::Ptr<uint8_t>(baseActor+0x0f)=1;
        assert(native_fbase_before_behavior(baseActor)==0);
        assert(native_fbase_before_render(baseActor)==0);
        memory.release(baseActor);
        // The original Player::State uses {function_or_vtable_offset,
        // signed_this_adjustment_with_virtual_bit}. Check both paths against
        // actual native code, never a cartridge instruction address.
        constexpr uint32_t nativeEntry = 0x62000000;
        memory.bind(nativeEntry, &native_member_probe);
        auto actor = memory.allocate(64);
        auto vtable = memory.allocate(32);
        *packed::Ptr<uint32_t>(actor+8) = vtable;
        *packed::Ptr<uint32_t>(vtable+12) = nativeEntry;
        const auto direct = memory.resolve_member(actor+8, {nativeEntry, -16});
        assert(direct.object == actor);
        const auto virtualCall = memory.resolve_member(actor, {12, 17});
        assert(virtualCall.object == actor+8 && virtualCall.code == direct.code);
        using MemberBody = uint32_t (*)(uint32_t, uint32_t);
        MemberBody invoke = nullptr;
        static_assert(sizeof(invoke) == sizeof(virtualCall.code));
        std::memcpy(&invoke, &virtualCall.code, sizeof invoke);
        assert(invoke(virtualCall.object, 0x1337) == (virtualCall.object ^ 0x1337));
        bool badMethod = false;
        try { (void)memory.resolve_member(actor, {0, 0}); }
        catch (const std::runtime_error &) { badMethod = true; }
        assert(badMethod);
        badMethod = false;
        try { (void)memory.resolve_member(actor, {3, 17}); }
        catch (const std::runtime_error &) { badMethod = true; }
        assert(badMethod);
        badMethod = false;
        try { (void)memory.resolve_member(actor, {0x02020000, 0}); }
        catch (const std::runtime_error &) { badMethod = true; }
        assert(badMethod);
        // The original fBase_c::Process runs Before, Action, After in
        // order. Dispatch through the DS member-pointer pair and map the
        // original return code to the 0/1/2/3 completion status.
        memory.bind(0x62000010u, &actor_test_before);
        memory.bind(0x62000014u, &actor_test_action);
        memory.bind(0x62000018u, &actor_test_after);
        *packed::Ptr<uint32_t>(vtable+16) = 0x62000014u;
        const ActorProcessMethods actorMethods{
            {16,17}, {0x62000010u,0}, {0x62000018u,0}
        };
        ActorDispatch dispatcher(memory);
        actorTestMode=0;
        assert(dispatcher.process(actor,actorMethods)==0 && actorLastStatus==0);
        actorTestMode=1;
        assert(dispatcher.process(actor,actorMethods)==1 && actorLastStatus==2);
        actorTestMode=2;
        assert(dispatcher.process(actor,actorMethods)==-1 && actorLastStatus==3);
        actorTestMode=3;
        assert(dispatcher.process(actor,actorMethods)==0 && actorLastStatus==1);
        bool unboundRejected=false;
        try {
            ActorProcessMethods badCallbacks=actorMethods;
            badCallbacks.action={0x02021000u,0};
            (void)dispatcher.process(actor,badCallbacks);
        } catch(const std::runtime_error &) { unboundRejected=true; }
        assert(unboundRejected);
        // Production actor scheduler from the original fBase_c list walker:
        // {head,tail,member function} and two 0x10 intrusive nodes. A
        // deliberately malformed cycle must fail rather than looping.
        memory.bind(0x6200001cu,&actor_list_callback);
        const uint32_t list = memory.allocate(16);
        const uint32_t currentNode = memory.allocate(4);
        const uint32_t firstNode = memory.allocate(16);
        const uint32_t secondNode = memory.allocate(16);
        const uint32_t firstOwner = memory.allocate(0x50);
        const uint32_t secondOwner = memory.allocate(0x50);
        *packed::Ptr<uint32_t>(list)=firstNode;
        *packed::Ptr<uint32_t>(list+4)=secondNode;
        *packed::Ptr<uint32_t>(list+8)=0x6200001cu;
        *packed::Ptr<int32_t>(list+12)=0;
        *packed::Ptr<uint32_t>(firstNode+4)=secondNode;
        *packed::Ptr<uint32_t>(firstNode+8)=firstOwner;
        *packed::Ptr<uint32_t>(secondNode+4)=0;
        *packed::Ptr<uint32_t>(secondNode+8)=secondOwner;
        actorVisitCount=0;
        assert(dispatcher.process_list(list,currentNode)==1);
        assert(actorVisitCount==2 && actorVisited[0]==firstOwner &&
               actorVisited[1]==secondOwner);
        assert(*packed::Ptr<uint32_t>(currentNode)==0);
        // Unbound callback addresses remain fatal instead of becoming
        // permissive no-op/synthetic actors.
        *packed::Ptr<uint32_t>(list+8)=0x02000000u;
        bool failedList=false;
        try { (void)dispatcher.process_list(list,currentNode); }
        catch(const std::runtime_error &) { failedList=true; }
        assert(failedList);
        *packed::Ptr<uint32_t>(list+8)=0x6200001cu;
        *packed::Ptr<uint32_t>(secondNode+4)=firstNode;
        failedList=false;
        try { (void)dispatcher.process_list(list,currentNode); }
        catch(const std::runtime_error &) { failedList=true; }
        assert(failedList);
        memory.release(firstOwner);
        memory.release(secondOwner);
        memory.release(firstNode);
        memory.release(secondNode);
        memory.release(currentNode);
        memory.release(list);
        // Recovered Player::State has exactly three 8-byte method pairs.
        // Exercise each with an original-width overlay record, including a
        // virtual Main and a null Cleanup (the source treats that as success).
        const uint32_t state = memory.allocate(0x18);
        const PlayerStateMethods playerState{
            {0x62000014u,0}, {16,17}, {0x62000014u,0}
        };
        std::memcpy(memory.data(state,sizeof playerState),&playerState,sizeof playerState);
        PlayerStateDispatch playerDispatch(memory);
        actorTestMode=1;
        assert(playerDispatch.invoke(actor,state,PlayerStatePhase::Init)==1);
        assert(playerDispatch.invoke(actor,state,PlayerStatePhase::Main)==1);
        assert(playerDispatch.invoke(actor,state,PlayerStatePhase::Cleanup)==1);
        auto noCleanup = playerState;
        noCleanup.cleanup={0,0};
        std::memcpy(memory.data(state,sizeof noCleanup),&noCleanup,sizeof noCleanup);
        assert(playerDispatch.invoke(actor,state,PlayerStatePhase::Cleanup)==1);
        auto noMain = noCleanup;
        noMain.main={0,0};
        std::memcpy(memory.data(state,sizeof noMain),&noMain,sizeof noMain);
        assert(playerDispatch.invoke(actor,state,PlayerStatePhase::Main)==0);
        memory.release(state);
        memory.release(vtable);
        memory.release(actor);
        auto bca=memory.allocate(0x40);
        for (unsigned i=0;i<4;++i) *packed::Ptr<uint32_t>(bca+8+i*4)=0x18+i*4;
        abi__ZN15dExtFrameCtrl_c17UpdateFileOffsetsER8BCA_File(bca);
        for (unsigned i=0;i<4;++i) assert(c.word_at(bca+8+i*4)==bca+0x18+i*4);
        memory.release(bca);
        auto btp=memory.allocate(0x80);
        for (auto count : {2,8,0x1c}) *packed::Ptr<uint16_t>(btp+count)=1;
        for (auto pair : {std::pair<unsigned,unsigned>{4,0x24},{12,0x2c},{32,0x34}})
            *packed::Ptr<uint32_t>(btp+pair.first)=pair.second;
        for (auto name : {0x28,0x30,0x38}) *packed::Ptr<uint32_t>(btp+name)=0x48;
        abi__ZN15dExtAnmTexPat_c17UpdateFileOffsetsER8BTP_File(btp);
        assert(c.word_at(btp+4)==btp+0x24 && c.word_at(btp+12)==btp+0x2c);
        assert(c.word_at(btp+32)==btp+0x34);
        for (auto name : {0x28,0x30,0x38}) assert(c.word_at(btp+name)==btp+0x48);
        memory.release(btp);
        bool bad=false;
        try { packed::Ptr<void()>(0x021ffffc)(); } catch (const std::runtime_error &) { bad=true; }
        assert(bad);
        bad=false;
        try { (void)*packed::Ptr<int>(0xfffffff0); } catch (const std::runtime_error &) { bad=true; }
        assert(bad);
    }
    {
        unsigned loads=0;
        FrontendCore full(heap,factory_fixture(),1,[&](uint16_t handle) {
            assert(handle==0); ++loads; return model_fixture();
        });
        assert(loads==1 && full.has_items());
        auto root=full.root_address(), display=full.word_at(root+0x74);
        assert(full.word_at(display+0x20)==4 && full.word_at(display+0x28)==6);
        assert(full.word_at(display+0x30)==8 && full.word_at(display+0x38)==6);
        for (unsigned i=0;i<4;++i) {
            auto character=full.word_at(full.word_at(display+0x24)+i*4);
            assert(full.word_at(character)==0x020f8024);
        }
        for (unsigned i=0;i<24;++i) {
            auto view=full.word_at(root+0x114+i*4);
            assert(at<uint16_t>(full,full.word_at(view))==i);
            auto sprite=full.word_at(view+4);
            assert(at<uint16_t>(full,sprite+0xc)==16 && at<uint16_t>(full,sprite+0xe)==16);
        }
        assert(full.word_at(root+0x184)==0);
        std::thread migrated([&] { migrate_thread(full); });
        migrated.join();
        assert(at<uint16_t>(full,full.word_at(root+0x54))==0x41);
    }
    assert(heap.allocations()==0 && heap.memory_left()==available);
    std::puts("Native frontend: original five root builders, state/input, model fixups and teardown passed.");
}
