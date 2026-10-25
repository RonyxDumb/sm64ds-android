#include "runtime/resources.h"
#include "SharedFilePtr.h"
#include "ntr/fs.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <stdexcept>
#include <vector>
#include <unistd.h>

using namespace sm64ds;
template<class F> static void rejects(F f) {
    bool bad = false;
    try { f(); } catch (const std::exception &) { bad = true; }
    assert(bad);
}
static void put(std::vector<uint8_t> &bytes, size_t offset, uint32_t value) {
    for (int b = 0; b < 4; ++b) bytes[offset + b] = value >> (b * 8);
}
static void fixture(const char *path) {
    constexpr size_t count = 0x80a, overlaySize = count * 4 + 2, fileStart = 0x300 + overlaySize;
    const std::vector<uint8_t> packed{'L','Z','7','7',0x10,3,0,0,0,'a','b','c'};
    std::vector<uint8_t> bytes(fileStart + packed.size());
    put(bytes,0x40,0x200); put(bytes,0x44,16); put(bytes,0x48,0x210); put(bytes,0x4c,16);
    put(bytes,0x50,0x230); put(bytes,0x54,32);
    put(bytes,0x200,8); bytes[0x204]=1; bytes[0x206]=1; bytes[0x208]=1; bytes[0x209]='a';
    put(bytes,0x210,0x300); put(bytes,0x214,fileStart);
    put(bytes,0x218,fileStart); put(bytes,0x21c,static_cast<uint32_t>(bytes.size()));
    put(bytes,0x234,0x020bd4b8); put(bytes,0x238,overlaySize);
    for(size_t i=0;i<count;++i) put(bytes,0x300+i*4,0x020bd4b8+count*4);
    bytes[0x300+count*4]='a';
    std::copy(packed.begin(), packed.end(), bytes.begin()+fileStart);
    std::ofstream f(path,std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    assert(f.good());
}
int main(int argc, char **argv) {
    {
        GameHeap heap(65536);
        const size_t capacity = heap.memory_left();
        std::vector<void *> pointers;
        std::mt19937 random(0x64d5);
        for (int n=0;n<1000;++n) {
            if (!pointers.empty() && random()%3==0) {
                size_t i=random()%pointers.size();heap.deallocate(pointers[i]);
                pointers.erase(pointers.begin()+static_cast<ptrdiff_t>(i));
            } else {
                const int alignment = (1 << (random()%7)) * (random()%2 ? 1 : -1);
                const uint32_t size=1+random()%128;
                if(void *p=heap.allocate(size,alignment)) {
                    assert(reinterpret_cast<uintptr_t>(p) % static_cast<unsigned>(std::abs(alignment)) == 0);
                    assert(heap.at(heap.offset_of(p),size)==p);
                    std::memset(p,0x5a,size);
                    for(void *other:pointers) {
                        const auto a=heap.offset_of(p), b=heap.offset_of(other);
                        assert(a+heap.size_of(p)<=b || b+heap.size_of(other)<=a);
                    }
                    pointers.push_back(p);
                }
            }
        }
        for(void *p:pointers) heap.deallocate(p);
        assert(heap.memory_left()==capacity && heap.allocations()==0);
        auto *p=static_cast<uint8_t*>(heap.allocate(16,64));
        p[0]=42;
        assert(heap.resize(p,32)==32 && p[0]==42);
        assert(heap.resize(p,8)==8 && p[0]==42);
        rejects([&]{heap.deallocate(p+1);});
        rejects([&]{heap.allocate(1,3);});
        assert(!heap.allocate(UINT32_MAX));
        rejects([&]{heap.at(UINT32_MAX,8);});
        std::printf("Native resource heap pointer: %p (pointer width %zu)\n",p,sizeof(void*));
        heap.deallocate(p);
        rejects([&]{heap.deallocate(p);});
        assert(heap.memory_left()==capacity);
    }
    {
        // BLZ stream: three literals, then 18 bytes copied from distance 3.
        std::vector<uint8_t> packed{0,0xf0,'a','b','c',0x10,0,0,0,0,0,0,0,0};
        put(packed,6,0x0800000e); put(packed,10,7);
        auto decoded=unpack_overlay(packed,21,true);
        for(size_t i=0;i<decoded.size();++i) assert(decoded[i]=="abc"[i%3]);
        packed[1]=0xff; // invalid reference distance
        rejects([&]{unpack_overlay(packed,21,true);});
        rejects([&]{unpack_overlay({},21,true);});
        rejects([&]{unpack_overlay({0,1},3,false);});
    }
    char path[]="/tmp/sm64ds-resources-XXXXXX";
    const int fd=mkstemp(path);assert(fd>=0);close(fd);fixture(path);
    {
        Resources resources(path);
        assert(resources.handle_count()==0x80a && resources.file_id(0)==1);
        assert(resources.load("a")==std::vector<uint8_t>({'a','b','c'}));
        assert(resources.heap().allocations()==0);
        rejects([&]{resources.file_id(0x8000);});
        rejects([&]{resources.load("missing");});
        SharedFilePtr file{};file.Construct(0);
        void *data=file.LoadFile();assert(data && std::memcmp(data,"abc",3)==0);
        for(int i=1;i<255;++i) assert(file.LoadFile()==data);
        assert(file.numRefs==255 && !file.LoadFile());
        for(int i=0;i<255;++i) file.Release();
        assert(file.numRefs==0 && !file.filePtr && resources.heap().allocations()==0);
        file.Release();
        assert(!file.filePtr);
    }
    assert(!ntr::fs_mounted());
    {
        Resources resources(path); // mount/heap lifetime can be repeated
        assert(resources.load("a").size()==3);
    }
    std::remove(path);
    if(argc>1) {
        Resources resources(argv[1]);
        auto mario=resources.load("data/DSMT/face_demo_mario.bmd");
        auto pose=resources.load("data/DSMT/face_demo_wait_mario.bca");
        assert(mario.size()>0x38 && pose.size()>0x18);
        assert(resources.heap().allocations()==0);
        std::printf("ROM catalog: %zu handles, recovered model %zu bytes, animation %zu bytes.\n",
                    resources.handle_count(),mario.size(),pose.size());
    }
    std::puts("Native heap, overlay BLZ/catalog and recovered shared-file ownership passed.");
}
