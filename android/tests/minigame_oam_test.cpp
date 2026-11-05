#include "runtime/packed_memory.h"
#include "ntr/mmio.h"
#include <cassert>
#include <cstring>
#include <iostream>

extern "C" {
void abi_mg_OamReset(); void abi_mg_OamFlush(); void abi_mg_OamLoad();
unsigned abi_mg_OamWidth(unsigned,unsigned);
unsigned abi_mg_OamHeight(unsigned,unsigned);
void abi_mg_Oam10(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,
                  unsigned,unsigned,unsigned,unsigned);
}

void minigame_oam_runtime() {
    using sm64ds::packed::Space;
    sm64ds::GameHeap heap(1<<20);
    Space space(heap);
    Space::Scope scope(space);
    space.map_owned(0x02004000u,0x020aa420u-0x02004000u);
    assert(ntr::io_init());
    space.map(ntr::IO_BASE,ntr::host_ptr(ntr::IO_BASE),ntr::IO_SIZE);
    space.map(ntr::OAM_BASE,ntr::host_ptr(ntr::OAM_BASE),ntr::OAM_SIZE);
    const auto read32=[&](unsigned a) { unsigned n; std::memcpy(&n,space.data(a,4),4); return n; };
    const auto write32=[&](unsigned a,unsigned n) { std::memcpy(space.data(a,4),&n,4); };
    // Standard hardware shape/size tables. In particular sizeBits 1 and 2
    // must do byte reads without constructing an unaligned int lvalue.
    const unsigned char widths[]={8,16,32,64,16,32,32,64,8,8,16,32};
    const unsigned char heights[]={8,16,32,64,8,8,16,32,16,32,32,64};
    std::memcpy(space.data(0x020755a0,sizeof widths),widths,sizeof widths);
    std::memcpy(space.data(0x020755ac,sizeof heights),heights,sizeof heights);
    for (unsigned shape=0;shape<3;++shape) for (unsigned size=0;size<4;++size) {
        assert(abi_mg_OamWidth(shape,size)==widths[shape*4+size]);
        assert(abi_mg_OamHeight(shape,size)==heights[shape*4+size]);
    }
    std::memset(space.data(0x0209e674,0x800),0xa5,0x800);
    abi_mg_OamReset();
    for (unsigned i=0;i<256;++i) {
        assert(read32(0x0209e674+i*8)==0xc0);
        unsigned short tile;
        std::memcpy(&tile,space.data(0x0209e674+i*8+4,2),2);
        assert(tile==0);
    }
    const auto sprite=space.allocate(8);
    // 0xffff marks the final DRAWN entry, not a separate empty sentinel.
    const unsigned short attributes[]={0,0,7,0xffff};
    std::memcpy(space.data(sprite,sizeof attributes),attributes,sizeof attributes);
    abi_mg_Oam10(1,sprite,40,50,unsigned(-1),0,0x1000,0x1000,0,unsigned(-1));
    assert(read32(0x0209e670)==1 && read32(0x0209e664)==0);
    assert(read32(0x0209ea74)==((40u<<16)|50u));
    ntr::io_write(0x04000304,0x8000,2);
    abi_mg_OamFlush(); abi_mg_OamLoad();
    assert(std::memcmp(ntr::host_ptr(0x07000400),space.data(0x0209ea74,0x400),0x400)==0);
    abi_mg_OamReset();
    assert(read32(0x0209e670)==0 && read32(0x0209e664)==0);
    // The single-buffer path must route sub sprite requests to engine A.
    *static_cast<unsigned char *>(space.data(0x0209e660,1))=1;
    abi_mg_Oam10(1,sprite,40,50,unsigned(-1),0,0x1000,0x1000,0,unsigned(-1));
    assert(read32(0x0209e664)==1);
    write32(0x0209e664,128);
    abi_mg_Oam10(0,sprite,40,50,unsigned(-1),0,0x1000,0x1000,0,unsigned(-1));
    assert(read32(0x0209e664)==128); // real OAM capacity, never overflow
    std::cout << "Original minigame OAM: size tables, overlapping reset, rendering, upload and capacity passed.\n";
}
