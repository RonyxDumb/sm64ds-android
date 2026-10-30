#include "packed_fs.h"
#include "packed_ptr.h"
#include "resources.h"
#include "ntr/fs.h"
#include "ntr/rt.h"
#include <algorithm>
#include <atomic>
#include <climits>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

extern "C" unsigned int abi_func_02018b64(unsigned int);
extern "C" void pk_Crash();

namespace sm64ds {
namespace {
using packed::Ptr;
template<class T> T &at(uint32_t a) { return *Ptr<T>(a); }
constexpr uint32_t RomBase=0x08000000u, NativeHeapTag=0x48454150u;
std::string string_at(uint32_t address) {
    std::string text;
    for (uint64_t i=0;uint64_t(address)+i<=UINT32_MAX;++i) {
        const char c=at<char>(address+static_cast<uint32_t>(i));
        if (!c) return text;
        text.push_back(c);
    }
    throw std::runtime_error("Unterminated cartridge filename.");
}
int cartridge_strlen(uint32_t address) {
    const auto size=string_at(address).size();
    if (size>INT_MAX) throw std::runtime_error("Cartridge string length exceeds its ABI.");
    return static_cast<int>(size);
}
unsigned char lower(unsigned char c) { return c>='A' && c<='Z' ? c+('a'-'A') : c; }
bool equal_name(const uint8_t *bytes,size_t count,const std::string &name) {
    if (count!=name.size()) return false;
    for (size_t i=0;i<count;++i) if (lower(bytes[i])!=lower(static_cast<unsigned char>(name[i]))) return false;
    return true;
}
uint16_t rd16(const uint8_t *p) { return uint16_t(p[0])|uint16_t(p[1])<<8; }
uint32_t rd32(const uint8_t *p) { return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24; }
uint32_t irq_disable() { return ntr::rt_irq_disable(); }
void irq_restore(uint32_t old) { ntr::rt_irq_restore(old); }

// Heap_t is opaque at this platform boundary. Its handle identifies the
// actual native GameHeap owner; it is never a cast of a host Heap_t object.
void check_heap(uint32_t heap) {
    if (!heap || at<uint32_t>(heap)!=NativeHeapTag || at<uint32_t>(heap+4)!=0x10000000u ||
        at<uint32_t>(heap+8)!=Resources::active().heap().capacity())
        throw std::runtime_error("Unregistered native heap owner.");
}
uint32_t heap_allocate(uint32_t heap,uint32_t bytes,int alignment) {
    check_heap(heap);
    return packed::Space::current().allocate(bytes,alignment);
}
void heap_deallocate(uint32_t heap,uint32_t allocation) {
    check_heap(heap);
    packed::Space::current().release(allocation);
}
uint32_t heap_set_default(uint32_t heap) {
    check_heap(heap);
    const uint32_t previous=at<uint32_t>(0x020a0ea0u);
    at<uint32_t>(0x020a0ea0u)=heap;
    return previous;
}
void cache_flush(uint32_t address,uint32_t bytes) {
    if (bytes) (void)packed::Space::current().data(address,bytes);
    std::atomic_thread_fence(std::memory_order_seq_cst);
}
void copy_bytes(uint32_t source,uint32_t destination,uint32_t bytes) {
    if (!bytes) return;
    auto &space=packed::Space::current();
    std::memmove(space.data(destination,bytes),space.data(source,bytes),bytes);
}

struct FileId { uint32_t archive,index; };
static_assert(sizeof(FileId)==8);
int file_open_direct(uint32_t file,uint32_t archive,uint32_t top,uint32_t bottom,int index) {
    auto &space=packed::Space::current();
    (void)space.data(file,0x44);
    if (!archive || !(at<uint32_t>(archive+0x10)&2) || bottom<top) return 0;
    const uint32_t base=at<uint32_t>(archive+0x1c);
    if (uint64_t(base)+bottom>UINT32_MAX || !space.contains(base+top,bottom-top)) return 0;
    at<uint32_t>(file+8)=archive;
    at<uint32_t>(file+0x2c)=top;
    at<uint32_t>(file+0x30)=bottom;
    at<int32_t>(file+0x34)=index;
    at<int32_t>(file+0x1c)=index;
    at<uint32_t>(file+0x20)=top;
    at<uint32_t>(file+0x24)=bottom;
    at<uint32_t>(file+0x28)=top;
    at<uint32_t>(file+0x10)=7;
    at<uint32_t>(file+0x14)=0;
    at<uint32_t>(file+0xc)=(at<uint32_t>(file+0xc)&~0x2fu)|0x10;
    return 1;
}
int file_open_id(uint32_t file,FileId id) {
    auto &space=packed::Space::current();
    (void)space.data(file,0x44);
    if (!id.archive || !(at<uint32_t>(id.archive+0x10)&2)) return 0;
    const uint32_t fatBytes=at<uint32_t>(id.archive+0x24);
    if (id.index>=fatBytes/8) return 0;
    const uint32_t base=at<uint32_t>(id.archive+0x1c);
    const uint32_t fat=at<uint32_t>(id.archive+0x20)+
        ((at<uint32_t>(id.archive+0x10)&4)?0:base);
    const auto *entry=static_cast<const uint8_t *>(space.data(fat+id.index*8,8));
    const uint32_t top=rd32(entry),bottom=rd32(entry+4);
    if (bottom<top || uint64_t(base)+bottom>UINT32_MAX) return 0;
    (void)space.data(base+top,bottom-top);
    // Exact FSFile prop.file and arg.openfilefast lanes from NitroSDK.
    at<uint32_t>(file+8)=id.archive;
    at<uint32_t>(file+0x2c)=id.archive;
    at<uint32_t>(file+0x30)=id.index;
    at<uint32_t>(file+0x1c)=id.index;
    at<uint32_t>(file+0x20)=top;
    at<uint32_t>(file+0x24)=bottom;
    at<uint32_t>(file+0x28)=top;
    at<uint32_t>(file+0x10)=6;
    at<uint32_t>(file+0x14)=0;
    at<uint32_t>(file+0xc)=(at<uint32_t>(file+0xc)&~0x2fu)|0x10;
    return 1;
}

// Walk the actual Nitro FNT. Directory offsets and file indices stay relative
// to the registered archive, including NARC archives mounted by original code.
bool find_file(uint32_t archive,const std::string &path,uint32_t &fileId) {
    auto &space=packed::Space::current();
    const uint32_t base=at<uint32_t>(archive+0x1c);
    const uint32_t size=at<uint32_t>(archive+0x2c);
    if (size<8) return false;
    const uint32_t fntAddress=at<uint32_t>(archive+0x28)+
        ((at<uint32_t>(archive+0x10)&4)?0:base);
    const auto *fnt=static_cast<const uint8_t *>(space.data(fntAddress,size));
    const uint16_t count=rd16(fnt+6);
    if (!count || uint32_t(count)*8>size) return false;
    uint32_t dir=!path.empty() && path.front()=='/' ? 0 : at<uint16_t>(0x020a8050u)&0xfffu;
    size_t cursor=0;
    while (cursor<path.size()) {
        while (cursor<path.size() && path[cursor]=='/') ++cursor;
        if (cursor==path.size()) return false;
        const size_t end=path.find('/',cursor);
        const std::string component=path.substr(cursor,end==std::string::npos ? end : end-cursor);
        const bool last=end==std::string::npos;
        cursor=last?path.size():end+1;
        if (dir>=count) return false;
        if (component==".") { if(last) return false; continue; }
        if (component=="..") { if (dir) dir=rd16(fnt+dir*8+6)&0xfffu; if(last) return false; continue; }
        uint32_t off=rd32(fnt+dir*8),index=rd16(fnt+dir*8+4);
        bool found=false;
        while (off<size) {
            const uint8_t type=fnt[off++];
            if (!type) break;
            const uint32_t length=type&0x7fu;
            const uint32_t extra=type&0x80u?2:0;
            if (!length || uint64_t(off)+length+extra>size) return false;
            if (equal_name(fnt+off,length,component)) {
                if (last) {
                    if (extra) return false;
                    fileId=index;
                    return true;
                }
                if (!extra) return false;
                dir=rd16(fnt+off+length)&0xfffu;
                found=true;
                break;
            }
            off+=length+extra;
            if (!extra) ++index;
        }
        if (!found) return false;
    }
    return false;
}

int file_open_path(uint32_t file,uint32_t pathAddress) {
    std::string path=string_at(pathAddress);
    uint32_t archive=at<uint32_t>(0x020a804cu);
    const size_t colon=path.find(':');
    if (colon!=std::string::npos) {
        if (!colon || colon>3) return 0;
        const auto name=path.substr(0,colon);
        uint32_t key=0;
        for(size_t i=0;i<name.size();++i) key|=uint32_t(lower(static_cast<unsigned char>(name[i])))<<(i*8);
        archive=at<uint32_t>(0x020a8048u);
        std::unordered_set<uint32_t> visited;
        while(archive && at<uint32_t>(archive)!=key) {
            if (!visited.insert(archive).second) throw std::runtime_error("Cyclic Nitro archive list.");
            archive=at<uint32_t>(archive+4);
        }
        path.erase(0,colon+1);
    }
    if (!archive || !(at<uint32_t>(archive+0x10)&2)) return 0;
    uint32_t index=0;
    return find_file(archive,path,index) ? file_open_id(file,{archive,index}) : 0;
}

int file_read(uint32_t file,uint32_t destination,int length,int async) {
    auto &space=packed::Space::current();
    (void)space.data(file,0x44);
    const uint32_t archive=at<uint32_t>(file+8),old=at<uint32_t>(file+0x28),bottom=at<uint32_t>(file+0x24);
    if (!archive || !(at<uint32_t>(archive+0x10)&2) || old>bottom) return -1;
    const uint32_t count=length<0?0:std::min(uint32_t(length),bottom-old);
    const uint32_t base=at<uint32_t>(archive+0x1c);
    if (uint64_t(base)+old+count>UINT32_MAX) return -1;
    at<uint32_t>(file+0x2c)=destination;
    at<int32_t>(file+0x30)=length;
    at<uint32_t>(file+0x34)=count;
    at<uint32_t>(file+0x10)=0;
    if (!async) at<uint32_t>(file+0xc)|=4;
    if (count) std::memcpy(space.data(destination,count),space.data(base+old,count),count);
    at<uint32_t>(file+0x28)=old+count;
    // Native card reads complete synchronously, including async requests.
    // FS_WaitAsync observes completed status/error just as after SDK release.
    at<uint32_t>(file+0xc)&=~0xfu;
    at<uint32_t>(file+0x14)=0;
    return static_cast<int>(count);
}
int file_close(uint32_t file) {
    if (!at<uint32_t>(file+8)) return 0;
    at<uint32_t>(file+8)=0;
    at<uint32_t>(file+0x10)=0xc;
    at<uint32_t>(file+0xc)&=~0x3fu;
    at<uint32_t>(file+0x14)=0;
    return 1;
}

int overlay_load_image(uint32_t info) {
    auto &space=packed::Space::current();
    (void)space.data(info,0x24);
    const uint32_t base=at<uint32_t>(info+4),size=at<uint32_t>(info+8),bss=at<uint32_t>(info+12);
    // FS_LoadOverlay's genuine game RAM bounds. ROM instructions remain data;
    // the original FS_StartOverlay resolves each initializer to native code.
    const uint64_t total=uint64_t(size)+bss;
    if (at<uint32_t>(info+0x20)!=0 || !size || base<0x020aa420u ||
        uint64_t(base)+total>0x0214eaa0u)
        throw std::runtime_error("Unsupported native overlay image extent.");
    if (!space.contains(base,static_cast<size_t>(total)))
        space.map_owned(base,static_cast<size_t>(total));
    return static_cast<int>(abi_func_02018b64(info));
}

void overlay_decompress(uint32_t end) {
    auto &space=packed::Space::current();
    if (end<8) throw std::runtime_error("Invalid native BLZ image end.");
    const auto *footer=static_cast<const uint8_t *>(space.data(end-8,8));
    const uint32_t encoded=rd32(footer)&0xffffffu,extra=rd32(footer+4);
    if (encoded<8 || encoded>end || uint64_t(encoded)+extra>4*1024*1024)
        throw std::runtime_error("Invalid native BLZ image extent.");
    // The footer describes the compressed tail. Its untouched prefix need not
    // be copied or guessed from an ARM code address.
    const auto *tail=static_cast<const uint8_t *>(space.data(end-encoded,encoded));
    auto bytes=unpack_overlay({tail,tail+encoded},encoded+extra,true);
    std::memcpy(space.data(end-encoded,bytes.size()),bytes.data(),bytes.size());
}
}

void bind_packed_fs(packed::Space &space) {
    space.bind(0x0205d518u,&file_open_path);
    space.bind(0x0205d568u,&file_open_id);
    space.bind(0x0205d5e8u,&file_open_direct);
    space.bind(0x0205d688u,&file_read);
    space.bind(0x0205d4ccu,&file_close);
    space.bind(0x0203c29cu,&heap_allocate);
    space.bind(0x0203c280u,&heap_deallocate);
    space.bind(0x0203c324u,&heap_set_default);
    // Symbol addresses from the original main/ITCM symbol maps.
    space.bind(0x02059d1cu,&irq_disable);
    space.bind(0x02059d30u,&irq_restore);
    space.bind(0x02058c40u,&cache_flush);
    space.bind(0x02058c08u,&cache_flush);
    space.bind(0x02058c68u,&cache_flush);
    space.bind(0x02070788u,&cartridge_strlen);
    space.bind(0x02018b64u,&overlay_load_image);
    space.bind(0x020048d8u,&overlay_decompress);
    space.bind(0x02019740u,&pk_Crash);
    space.bind(0x0205a61cu,&copy_bytes);
}

void initialize_packed_fs(packed::Space &space) {
    packed::Space::Scope scope(space);
    ntr::RomView rom{};
    if (!ntr::fs_rom_view(rom) || rom.size<0x160)
        throw std::runtime_error("Native stage filesystem has no mounted cartridge.");
    space.map(RomBase,const_cast<uint8_t *>(rom.bytes),rom.size);
    // NitroSDK's uncached overlay-table lookup reads the real card header
    // copied by the NTR boot environment. Normal DS mode has console type 0.
    space.map(0x027ffe00u,const_cast<uint8_t *>(rom.bytes),0x160);
    space.map_owned(0x027ffc40u,2);
    const uint32_t heap=space.allocate(16);
    at<uint32_t>(heap)=NativeHeapTag;
    at<uint32_t>(heap+4)=0x10000000u;
    at<uint32_t>(heap+8)=Resources::active().heap().capacity();
    at<uint32_t>(0x020a0ea0u)=at<uint32_t>(0x020a0eacu)=heap;
    // func_0205e088 and SDK overlay file IDs use the original inline ROM
    // archive at data_020a8074, not an independently allocated substitute.
    const uint32_t archive=0x020a8074u,name=space.allocate(4);
    std::memcpy(space.data(name,4),"rom",4);
    Ptr<void(uint32_t)>(0x0205cd34u)(archive);
    if (!Ptr<int(uint32_t,uint32_t,int)>(0x0205cc80u)(archive,name,3))
        throw std::runtime_error("Could not register the native ROM archive.");
    if (!Ptr<int(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t)>(0x0205cb68u)(
            archive,RomBase,rd32(rom.bytes+0x48),rd32(rom.bytes+0x4c),
            rd32(rom.bytes+0x40),rd32(rom.bytes+0x44),0,0))
        throw std::runtime_error("Could not mount the native ROM archive.");
    Ptr<void()>(0x02017e60u)(); // original overlay-region initialization
}
}
