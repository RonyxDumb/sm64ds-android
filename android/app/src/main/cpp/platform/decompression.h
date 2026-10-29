#pragma once
#include "arm9_services.h"
namespace sm64ds::platform {
struct DecompressionStream{
 native_abi::StoredPointer<int(const uint8_t*,void*,uint32_t)> open;
 native_abi::StoredPointer<int(const uint8_t*)> close;
 native_abi::StoredPointer<uint8_t(const uint8_t*)> byte;
};static_assert(sizeof(DecompressionStream)==12);
struct CompressedReader{
 uintptr_t cursor;const DecompressionStream *callbacks;
 uint8_t byte(){auto *p=reinterpret_cast<const uint8_t*>(cursor++);return callbacks?static_cast<uint8_t(*)(const uint8_t*)>(callbacks->byte)(p):*p;}
 uint32_t word(){uint32_t value=0;for(unsigned i=0;i<4;++i)value|=uint32_t(byte())<<(i*8);return value;}
};
inline void write_decoded(void *destination,const std::vector<uint8_t> &out,unsigned width){
 auto *dst=static_cast<uint8_t*>(destination);for(size_t i=0;i+width<=out.size();i+=width){uint32_t value=0;std::memcpy(&value,out.data()+i,width);auto address=reinterpret_cast<uintptr_t>(dst+i);if(address>=ntr::IO_BASE&&address<ntr::IO_BASE+ntr::IO_SIZE)ntr::io_write(uint32_t(address),value,width);else std::memcpy(dst+i,&value,width);}
}
enum class Compression{Lz77,Rle,Huffman};
inline int decompress(const void *source,void *destination,Compression kind,unsigned width,uint32_t argument=0,const DecompressionStream *callbacks=nullptr){
 CompressedReader reader{reinterpret_cast<uintptr_t>(source),callbacks};
 const int header=callbacks?static_cast<int(*)(const uint8_t*,void*,uint32_t)>(callbacks->open)(static_cast<const uint8_t*>(source),destination,argument):int(reader.word());if(callbacks)reader.cursor+=4;
 std::vector<uint8_t> out;
 if(header>=0){uint32_t size=uint32_t(header)>>8;out.reserve(size);
  if(kind==Compression::Lz77){
   while(out.size()<size){auto flags=reader.byte();for(unsigned bit=0;bit<8&&out.size()<size;++bit){if(!(flags&(0x80>>bit))){out.push_back(reader.byte());continue;}unsigned first=reader.byte(),second=reader.byte();unsigned count=(first>>4)+3,distance=((first&15)<<8)+second+1;if(distance>out.size())throw std::runtime_error("Invalid LZ77 back-reference");for(unsigned i=0;i<count&&out.size()<size;++i)out.push_back(out[out.size()-distance]);}}
  }else if(kind==Compression::Rle){
   while(out.size()<size){auto control=reader.byte();unsigned count=(control&127)+((control&128)?3:1);if(control&128){auto value=reader.byte();for(unsigned i=0;i<count&&out.size()<size;++i)out.push_back(value);}else for(unsigned i=0;i<count&&out.size()<size;++i)out.push_back(reader.byte());}
  }else{
   unsigned bits=unsigned(header)&15;if(bits!=4&&bits!=8)throw std::runtime_error("Invalid Huffman symbol width");unsigned count=(unsigned(reader.byte())+1)*2;
   std::vector<uint8_t> tree(count);tree[0]=uint8_t(count/2-1);for(unsigned i=1;i<count;++i)tree[i]=reader.byte();
   unsigned node=1,available=0,used=0;uint32_t buffer=0,byte=0;
   while(out.size()<size){if(!available){buffer=reader.word();available=32;}unsigned bit=buffer>>31;buffer<<=1;--available;auto branch=tree.at(node);unsigned child=(node&~1u)+2*((branch&63)+1)+bit;if(branch&(bit?0x40:0x80)){byte|=uint32_t(tree.at(child)&((1u<<bits)-1))<<used;used+=bits;if(used==8){out.push_back(uint8_t(byte));used=0;byte=0;}node=1;}else node=child;}
  }
  write_decoded(destination,out,width);
 }
 if(callbacks&&static_cast<int(*)(const uint8_t*)>(callbacks->close)){int result=static_cast<int(*)(const uint8_t*)>(callbacks->close)(reinterpret_cast<const uint8_t*>(reader.cursor));if(result<0)return result;}return header;
}
inline void decompress_backwards(uint8_t *end){
 if(!end)return;uint32_t packed,extra;std::memcpy(&packed,end-8,4);std::memcpy(&extra,end-4,4);
 uint32_t encoded=packed&0x00ffffffu,header=packed>>24;if(header<8||encoded<header)throw std::runtime_error("Invalid backward LZ header");
 auto *begin=end-encoded,*source=end-header,*destination=end+extra,*outputEnd=destination;
 while(source>begin){auto flags=*--source;for(unsigned bit=0;bit<8&&source>begin;++bit){if(!(flags&(0x80>>bit))){*--destination=*--source;continue;}if(source-begin<2)throw std::runtime_error("Truncated backward LZ match");unsigned first=*--source,second=*--source;unsigned distance=(((first<<8)|second)&0xfff)+2,count=(first>>4)+3;for(unsigned i=0;i<count;++i){if(destination<=begin||destination+distance>=outputEnd)throw std::runtime_error("Invalid backward LZ back-reference");auto value=destination[distance];*--destination=value;}}}
 __builtin___clear_cache(reinterpret_cast<char*>(begin),reinterpret_cast<char*>(outputEnd));cache_fence();
}
}
