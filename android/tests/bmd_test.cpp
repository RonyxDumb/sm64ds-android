#include "ntr/bmd.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>
static void word(std::vector<uint8_t>&b,size_t o,uint32_t v){for(int i=0;i<4;i++)b[o+i]=v>>(i*8);}
static void half(std::vector<uint8_t>&b,size_t o,uint16_t v){b[o]=v;b[o+1]=v>>8;}
int main(){try{
    ntr::BmdModel model;model.num_bones=3;model.data.resize(0x100);word(model.data,8,0x20);
    // File order 1,2,0; parent links are relative to ID, not file record position.
    int ids[]={1,2,0};for(int i=0;i<3;i++){size_t o=0x20+i*0x40;half(model.data,o,ids[i]);half(model.data,o+8,ids[i]?uint16_t(-1):0);}
    std::vector<uint8_t> bca(0x180);word(bca,8,0x120);word(bca,12,0x120);word(bca,16,0x130);word(bca,20,0x20);
    for(int i=0;i<3;i++){half(bca,0x20+i*0x24+0x1a,i+1);word(bca,0x130+(i+1)*4,(i+1)*4096);}
    std::vector<std::array<float,16>> out;ntr::bmd_bone_matrices(model,bca,out);
    if(out.size()!=3 || std::fabs(out[0][12]-1)>.0001f || std::fabs(out[1][12]-3)>.0001f || std::fabs(out[2][12]-6)>.0001f)throw std::runtime_error("bone ID hierarchy regression");
    half(model.data,0x20+8,1); // ID1 -> ID2 -> ID1
    try{ntr::bmd_bone_matrices(model,bca,out);throw std::logic_error("cycle accepted");}catch(const std::runtime_error&){}
    puts("BMD record order/parent-ID hierarchy tests passed");
}catch(const std::exception&e){puts(e.what());return 1;}}
