#include "runtime/stage_actor_catalog.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>
using namespace sm64ds;
static void h16(std::vector<uint8_t>& b,size_t i,uint16_t x){
    b[i]=uint8_t(x);b[i+1]=uint8_t(x>>8);
}
static void h32(std::vector<uint8_t>& b,size_t i,uint32_t x){
    h16(b,i,uint16_t(x));h16(b,i+2,uint16_t(x>>16));
}
int main(){
    Cartridge::DataImage level{0x02100000u,std::vector<uint8_t>(0x200)};
    Cartridge::DataImage ov2{0x0210c000u,std::vector<uint8_t>(0x1000)};
    const uint32_t base=level.base;
    StageAssets::Descriptor desc{};
    desc.objectsAddress=base+0x40;
    // Three original 8-byte object-category descriptors.
    h16(level.bytes,0x40,3);h32(level.bytes,0x44,base+0x48);
    for(unsigned i=0;i<3;++i){
        const size_t row=0x48+i*8;
        level.bytes[row]=uint8_t(0x20u+(i+1)); // star 1, category 1..3
        level.bytes[row+1]=1;
        h32(level.bytes,row+4,base+0x80+i*0x20);
    }
    // Categories looked up from the original ov002 dispatch table, not
    // assumed to be in a fixed actor class order.
    h32(ov2.bytes,0xbb8+1*4,0x020fe6c8); // entrance
    h32(ov2.bytes,0xbb8+2*4,0x020fe8ac); // standard
    h32(ov2.bytes,0xbb8+3*4,0x020fe960); // simple
    h16(ov2.bytes,0xbf4+5*2,0x140);
    h16(ov2.bytes,0xbf4+10*2,0x141);
    h16(ov2.bytes,0xbf4+11*2,0x142);
    // Entrance in original StandardEntry format.
    h16(level.bytes,0x80,5);
    h16(level.bytes,0x82,12);h16(level.bytes,0x84,30);
    h16(level.bytes,0x86,16);h16(level.bytes,0x8a,0x2000);
    h16(level.bytes,0x8e,17);
    // A standard actor.
    h16(level.bytes,0xa0,10);h16(level.bytes,0xa2,4);
    h16(level.bytes,0xa4,5);h16(level.bytes,0xa6,6);
    h16(level.bytes,0xae,19);
    // One simple actor (index 11, parameter 3).
    h16(level.bytes,0xc0,uint16_t((3<<9)|11));
    h16(level.bytes,0xc2,7);h16(level.bytes,0xc4,8);h16(level.bytes,0xc6,9);
    StageActorCatalog catalog;
    assert(catalog.read_raw(desc,level,ov2,1,0));
    assert(catalog.has_entrance());
    assert(catalog.entrance().actorId==0x140);
    assert(catalog.entrance().x==12 && catalog.entrance().y==30);
    assert(catalog.entrance().yaw==0x2000);
    assert(catalog.actors().size()==2);
    assert(catalog.actors()[0].actorId==0x141);
    assert(catalog.actors()[1].actorId==0x142);
    assert(catalog.actors()[1].parameter==3);
    assert(catalog.category_count()==3);
    // Same ROM records, wrong star: none of the star-1 entries activate.
    assert(!catalog.read_raw(desc,level,ov2,2,0));
    assert(catalog.actors().empty());
    // Reject a corrupt cartridge pointer and clear previously decoded state.
    h32(level.bytes,0x4c,0xfffffff0u);
    assert(!catalog.read_raw(desc,level,ov2,1,0));
    assert(catalog.actors().empty() && !catalog.has_entrance());
    std::cout<<"Retail level object, actor ID, star filter and entrance parser passed.\n";
}
