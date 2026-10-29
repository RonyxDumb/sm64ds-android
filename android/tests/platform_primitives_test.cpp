#include "platform/decompression.h"
#include <cassert>
#include <cstdio>
#include <array>
#include <thread>
extern "C"{
void Copy32Bytes(const void*,void*);void Copy36Bytes(const void*,void*);void Copy48Bytes(const void*,void*);void Copy48BytesFixed(const void*,void*);void CpuCopy8(const void*,void*,uint32_t);
void CpuSet(const void*,void*,uint32_t);void CpuFastSet(const void*,void*,uint32_t);void BitUnPack(const void*,void*,const sm64ds::platform::BitUnpackInfo*);
uint16_t GetCRC16(uint16_t,const void*,uint32_t);uint32_t Sqrt(uint32_t);
void DecompressLZ16(const void*,void*);void LZ77UnCompReadNormalWrite8bit(const void*,void*);void RLUnCompReadNormalWrite8bit(const void*,void*);
int LZ77UnCompReadByCallbackWrite16bit(const void*,void*,uint32_t,const sm64ds::platform::DecompressionStream*);int RLUnCompReadByCallbackWrite16bit(const void*,void*,uint32_t,const sm64ds::platform::DecompressionStream*);int HuffUnCompReadByCallback(const void*,void*,uint32_t,const sm64ds::platform::DecompressionStream*);
void Matrix3x3_LoadIdentity(int32_t*);void func_02052514(const int32_t*,int32_t*);void Matrix3x3_SetRotationZ(int32_t*,int32_t,int32_t);void func_020527e8(int32_t*,int32_t,int32_t,int32_t);
uint32_t func_0205a74c(uint32_t,uint32_t*);int func_02057020();void func_02057078(unsigned);
}
namespace{
unsigned opened=0,closed=0,reads=0;bool rejectClose=false;
int open(const uint8_t *source,void*,uint32_t argument){assert(argument==93);++opened;uint32_t v;std::memcpy(&v,source,4);return int(v);}
int close(const uint8_t*){++closed;return rejectClose?-7:0;}
uint8_t byte(const uint8_t *p){++reads;return *p;}
void waits(){using namespace sm64ds::platform;signal_interrupt(ntr::IRQ_VBLANK);wait_interrupt(false,ntr::IRQ_VBLANK);assert(ntr::rt_frame()==0);wait_interrupt(true,ntr::IRQ_VBLANK);assert(ntr::rt_frame()==1);wait_interrupt(true,ntr::IRQ_VBLANK);assert(ntr::rt_frame()==2);}
}
int main(){
 using namespace sm64ds;using namespace sm64ds::platform;assert(ntr::io_init());GameHeap heap;packed::Space space(heap);packed::Space::Scope scope(space);
 std::array<uint8_t,96> source{},dst{};for(unsigned i=0;i<source.size();++i)source[i]=uint8_t(i);
 for(unsigned size:{32,36,48}){dst.fill(0xcc);if(size==32)Copy32Bytes(source.data(),dst.data());if(size==36)Copy36Bytes(source.data(),dst.data());if(size==48)Copy48Bytes(source.data(),dst.data());assert(!std::memcmp(source.data(),dst.data(),size)&&dst[size]==0xcc);}
 dst.fill(0xcc);Copy48BytesFixed(source.data(),dst.data());assert(!std::memcmp(source.data()+36,dst.data(),12)&&dst[12]==0xcc);
 for(unsigned a=0;a<4;++a)for(unsigned b=0;b<4;++b)for(unsigned size=0;size<57;++size){dst.fill(0xcc);CpuCopy8(source.data()+a,dst.data()+b,size);assert(!std::memcmp(source.data()+a,dst.data()+b,size));if(b)assert(dst[b-1]==0xcc);assert(dst[b+size]==0xcc);}
 dst.fill(0);CpuSet(source.data(),dst.data(),3);assert(!std::memcmp(source.data(),dst.data(),6)&&dst[6]==0);dst.fill(0);CpuFastSet(source.data(),dst.data(),1);assert(!std::memcmp(source.data(),dst.data(),32)&&dst[32]==0);
 uint32_t fill=0x12345678;CpuSet(&fill,dst.data(),(1u<<24)|(1u<<26)|5);for(unsigned i=0;i<5;++i)assert(!std::memcmp(dst.data()+4*i,&fill,4));
 BitUnpackInfo info{1,1,4,0};uint8_t packed=0xa5;uint32_t unpacked=0;BitUnPack(&packed,&unpacked,&info);assert(unpacked==0x10100101);info.offset=0x80000002;BitUnPack(&packed,&unpacked,&info);assert(unpacked==0x32322323);
 assert(GetCRC16(0,"123456789",9)==0xbb3d);assert(Sqrt(UINT32_MAX)==65535);for(uint32_t i=0;i<100000;++i){auto root=Sqrt(i);assert(uint64_t(root)*root<=i&&uint64_t(root+1)*(root+1)>i);}
 int32_t m[16];Matrix3x3_LoadIdentity(m);func_02052514(m,m);assert(m[0]==4096&&m[5]==4096&&m[10]==4096&&m[15]==4096);Matrix3x3_SetRotationZ(m,4096,0);assert(m[1]==4096&&m[3]==-4096&&m[8]==4096);func_020527e8(m,2,3,4);assert(m[0]==2&&m[4]==3&&m[8]==4&&m[11]==0);
 const uint8_t lz[]={0x10,12,0,0,0x10,'A','B','C',0x60,2},rle[]={0x30,8,0,0,1,'A','B',0x83,'C'};uint8_t decoded[16]{};
 LZ77UnCompReadNormalWrite8bit(lz,decoded);assert(!std::memcmp(decoded,"ABCABCABCABC",12));std::memset(decoded,0xcc,sizeof decoded);DecompressLZ16(lz,decoded);assert(!std::memcmp(decoded,"ABCABCABCABC",12)&&decoded[12]==0xcc);RLUnCompReadNormalWrite8bit(rle,decoded);assert(!std::memcmp(decoded,"ABCCCCCC",8));
 DecompressionStream stream{&open,&close,&byte};assert(sizeof stream==12);assert(LZ77UnCompReadByCallbackWrite16bit(lz,decoded,93,&stream)==0xc10);assert(RLUnCompReadByCallbackWrite16bit(rle,decoded,93,&stream)==0x830);
 const uint8_t h8[]={0x28,4,0,0,1,0xc0,'A','B',0,0,0,0x60},h4[]={0x24,4,0,0,1,0xc0,1,2,0,0,0,0x66};
 assert(HuffUnCompReadByCallback(h8,decoded,93,&stream)==0x428);assert(!std::memcmp(decoded,"ABBA",4));assert(HuffUnCompReadByCallback(h4,decoded,93,&stream)==0x424);for(int i=0;i<4;++i)assert(decoded[i]==(i%2?0x12:0x21));rejectClose=true;assert(HuffUnCompReadByCallback(h8,decoded,93,&stream)==-7);assert(opened==closed&&reads>0);
 bool invalid=false;const uint8_t bad[]={0x10,4,0,0,0x80,0,0};try{LZ77UnCompReadNormalWrite8bit(bad,decoded);}catch(const std::runtime_error&){invalid=true;}assert(invalid);
 uint32_t lockWords[2]={0xc0000000,0x80000000};space.map(0x027fffb0,lockWords,sizeof lockWords);assert(func_02057020()==0x40&&func_02057020()==0x41&&func_02057020()==0x60&&func_02057020()==-3);func_02057078(0x41);assert(func_02057020()==0x41);
 uint32_t shared=0;std::thread a([&]{for(int i=0;i<10000;++i)func_0205a74c(1,&shared);}),b([&]{for(int i=0;i<10000;++i)func_0205a74c(2,&shared);});a.join();b.join();assert(shared==1||shared==2);
 assert(mask_update(0xc0,0xc0)==0&&processor_status()==0xdf&&mask_update(0xc0,0)==0xc0);assert(ntr::rt_run(&waits,nullptr,0)==2);
 std::puts("Native ARM9 memory, matrices, codecs/callbacks, CRC, locks and IRQ waits passed.");
}
