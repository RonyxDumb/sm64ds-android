#include "minigame_scene_constructors.h"
#include "minigame_progress.h"
#include "stage_runtime.h"
#include "packed_ptr.h"
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using sm64ds::packed::Ptr;
template<class T> T &at(uint32_t address) { return *Ptr<T>(address); }
uint32_t read32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1])<<8 |
           uint32_t(p[2])<<16 | uint32_t(p[3])<<24;
}
// The registered factory is called only while the matching packed address
// space is active. Single-active-game ownership matches GameProgram.
thread_local uint32_t boundVptr=0;
uint32_t luigi_factory() {
    return sm64ds::construct_minigame_2d(0x16eu,boundVptr);
}
uint32_t amida_factory() {
    return sm64ds::construct_minigame_2d(0x173u,boundVptr);
}
}

namespace sm64ds {
uint32_t original_minigame_vptr(const Cartridge::DataImage &image,const char *typeName) {
    if (!typeName || !*typeName) throw std::invalid_argument("Empty original minigame RTTI name.");
    const auto &b=image.bytes;
    const size_t nameSize=std::strlen(typeName)+1;
    if (nameSize>b.size()) throw std::runtime_error("Missing original minigame RTTI name.");
    std::vector<uint32_t> names;
    for (size_t i=0;i+nameSize<=b.size();++i)
        if (std::memcmp(b.data()+i,typeName,nameSize)==0)
            names.push_back(image.base+static_cast<uint32_t>(i));
    std::vector<uint32_t> vtables;
    for (size_t i=4;i+4<=b.size();i+=4) {
        const uint32_t name=read32(b.data()+i);
        bool matches=false;
        for (const uint32_t candidate:names) matches|=name==candidate;
        if (!matches) continue;
        // The RTTI object has a 4-byte vptr followed by a name pointer.
        const uint32_t typeinfo=image.base+static_cast<uint32_t>(i-4);
        for (size_t j=4;j+4<=b.size();j+=4) {
            // Retail 32-bit Itanium table: offset-to-top=0, RTTI word, slots.
            if (read32(b.data()+j)==typeinfo && read32(b.data()+j-4)==0)
                vtables.push_back(image.base+static_cast<uint32_t>(j+4));
        }
    }
    if (vtables.size()!=1)
        throw std::runtime_error("Original minigame RTTI/vtable is missing or ambiguous: "+std::string(typeName));
    return vtables.front();
}

uint32_t construct_minigame_2d(uint32_t scene,uint32_t originalVptr) {
    auto &space=packed::Space::current();
    const uint32_t size=scene==0x16eu ? 0x545cu :
                        scene==0x173u ? 0x53fcu : 0;
    if (!size || !originalVptr)
        throw std::runtime_error("Minigame scene constructor not linked.");
    // fBase_c::operator new and its native actor manager already preserve
    // the cartridge's cleared allocations, scene nodes and packed handles.
    const uint32_t actor=space.allocate(size);
    native_fbase_construct(actor);
    at<uint32_t>(actor)=0x0208e4b8u; // dBase_c intermediate vtable
    at<uint32_t>(actor)=0x02092680u; // dScene_c intermediate vtable
    at<uint8_t>(actor+0x13u)|=5;     // scene behavior/render scheduling
    at<uint32_t>(0x020beb68u)=actor; // gMgScene
    // Recovered dScMgBase_c::dScMgBase_c fields (ov004:0x020b2adc).
    const uint32_t param=at<uint32_t>(actor+8u);
    at<uint32_t>(actor+8u)=param&0xffffu;
    at<uint32_t>(actor+0xbcu)=at<uint32_t>(0x0209b308u+0x30u);
    at<int32_t>(actor+0x064u)=-1;
    at<uint8_t>(actor+0x0c2u)=1;
    at<int32_t>(actor+0x058u)=(scene==0x16eu || scene==0x185u ||
                                 scene==0x16du || scene==0x182u)?0x3c:0x78;
    at<int32_t>(actor+0x0e4u)=-1; // dMgState_c::dMgState_c (state=-1)
    // Pause panel default: shut, no active icons. Icon placement and scene
    // kind table must still be connected before complete scene lifecycle.
    at<int32_t>(actor+0x214u)=0;
    at<uint8_t>(actor+0x218u)=0;
    at<int32_t>(actor+0x4650u)=
        MinigameProgress::record((param>>8)&0xffu,0);
    // Luigi::new is only the base constructor and a final vptr store.
    // Amida's four array constructors are all genuine empty callbacks:
    // 4x8 walker/position/velocity pairs and 128x0x18 falling pieces.
    // The checked GameHeap is already zero initialized.
    if (scene==0x173u) {
        (void)space.data(actor+0x4724u,4u*8u);
        (void)space.data(actor+0x4744u,4u*8u);
        (void)space.data(actor+0x4768u,0x80u*0x18u);
    }
    at<uint32_t>(actor)=originalVptr;
    return actor;
}

void bind_minigame_scene_factory(packed::Space &space,const Cartridge::DataImage &game,uint32_t scene) {
    if (scene!=0x16eu && scene!=0x173u) return;
    packed::Space::Scope scope(space);
    // Verify the actual profile and callback in the selected cartridge.
    const uint32_t profile=at<uint32_t>(0x02090864u+scene*4u);
    const uint32_t factory=at<uint32_t>(profile);
    const char *name=scene==0x16eu?"12dScMgLuigi_c":"12dScMgAmida_c";
    boundVptr=original_minigame_vptr(game,name);
    if (scene==0x16eu) space.bind(factory,&luigi_factory);
    else space.bind(factory,&amida_factory);
}
}
