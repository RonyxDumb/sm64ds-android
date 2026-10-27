#include "stage_actor_catalog.h"
#include <stdexcept>
namespace sm64ds {
namespace {
uint16_t h16(const uint8_t* p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
uint32_t h32(const uint8_t* p){return uint32_t(h16(p))|(uint32_t(h16(p+2))<<16);}
int16_t s16(const uint8_t* p){return static_cast<int16_t>(h16(p));}
// European ov002 symbols: see src/game/stages/LevelObjects.cpp.
constexpr uint32_t LoaderTable=0x0210cbb8u,ActorIndexTable=0x0210cbf4u;
constexpr uint32_t EntranceLoader=0x020fe6c8u;
constexpr uint32_t StandardLoader=0x020fe8acu;
constexpr uint32_t SimpleLoader=0x020fe960u;
const uint8_t* owned(const Cartridge::DataImage& ov,uint32_t ptr,size_t n){
    if(ptr<ov.base||size_t(ptr-ov.base)>ov.bytes.size()||
       n>ov.bytes.size()-size_t(ptr-ov.base))
        throw std::runtime_error("Unmapped native object-loader table");
    return ov.bytes.data()+(ptr-ov.base);
}
}
bool StageActorCatalog::read(const StageAssets& level,const Cartridge::DataImage& objectOv,
                             unsigned star,unsigned entrance){
    actors_.clear();categories_=0;entranceFound_=false;entrance_={};
    if(star>7||entrance>4096)return false;
    try{
        const auto& desc=level.descriptor();
        // The loader table is 15 four-byte pointers and ends at 0x0210cbf4.
        const uint8_t* code=owned(objectOv,LoaderTable,15*4);
        const uint8_t* actorIds=owned(objectOv,ActorIndexTable,512*2);
        unsigned entranceCat=32,standardCat=32,simpleCat=32;
        for(unsigned i=0;i<15;++i){
            uint32_t addr=h32(code+i*4);
            if(addr==EntranceLoader)entranceCat=i;
            if(addr==StandardLoader)standardCat=i;
            if(addr==SimpleLoader)simpleCat=i;
        }
        if(entranceCat==32||standardCat==32||simpleCat==32)return false;
        auto at=[&](uint32_t address,size_t n)->const uint8_t*{
            return level.overlay_at(address,n);
        };
        auto parse=[&](uint32_t address,int area){
            if(!address)return;
            const uint8_t* head=at(address,8);
            const unsigned count=h16(head);
            if(count>4096)return;
            const uint32_t records=h32(head+4);
            const uint8_t* rows=at(records,size_t(count)*8);
            for(unsigned i=0;i<count;++i){
                const uint8_t* e=rows+size_t(i)*8;
                const unsigned kind=e[0]&31,filter=e[0]>>5;
                if(filter!=0&&filter!=star)continue;
                ++categories_;
                const unsigned n=e[1];
                const uint32_t entryPtr=h32(e+4);
                if(kind==entranceCat){
                    if(entrance>=n||!n)continue;
                    const uint8_t* data=at(entryPtr,size_t(n)*16);
                    const uint8_t* row=data+size_t(entrance)*16;
                    const unsigned index=h16(row);
                    if(index>=512)continue;
                    entrance_={h16(actorIds+index*2),h16(row+14),
                               s16(row+2),s16(row+4),s16(row+6),s16(row+10),area};
                    entranceFound_=true;
                }else if(kind==standardCat||kind==simpleCat){
                    const unsigned stride=kind==standardCat?16:8;
                    const uint8_t* data=at(entryPtr,size_t(n)*stride);
                    for(unsigned j=0;j<n&&actors_.size()<8192;++j){
                        const uint8_t* row=data+size_t(j)*stride;
                        const unsigned raw=h16(row);
                        const unsigned id=kind==simpleCat?(raw&0x1ffu):raw;
                        if(id>=512||id==0x1ff)continue; // minimap sentinel
                        actors_.push_back({h16(actorIds+id*2),
                            kind==standardCat?h16(row+14):uint16_t(raw>>9),
                            s16(row+2),s16(row+4),s16(row+6),
                            kind==standardCat?s16(row+10):int16_t(0),area});
                    }
                }
            }
        };
        parse(desc.objectsAddress,-1);
        if(desc.subTableCount>64)return false;
        if(desc.subTableCount){
            const uint8_t* sub=at(desc.subTablesAddress,size_t(desc.subTableCount)*12);
            for(unsigned a=0;a<desc.subTableCount;++a)
                parse(h32(sub+size_t(a)*12),int(a));
        }
        return entranceFound_;
    }catch(const std::runtime_error&){
        entranceFound_=false;entrance_={};actors_.clear();categories_=0;
        return false;
    }
}
}
