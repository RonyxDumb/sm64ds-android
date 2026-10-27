#include "runtime/stage_collision.h"
#include <cassert>
#include <cmath>
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
    std::vector<uint8_t> b(0x88);
    // Synthetic KCL: original 0x38-byte header, one point, four normals,
    // one unused prism + one ground prism and a single terminal octree node.
    h32(b,0,0x38);h32(b,4,0x44);h32(b,8,0x5c);h32(b,12,0x7c);
    for(unsigned i=0;i<3;++i)h32(b,0x14+i*4,0);
    for(unsigned i:{0x20,0x24,0x28})h32(b,i,~63u);
    h32(b,0x2c,6);h32(b,0x30,0);h32(b,0x34,0);
    h16(b,0x44+2,0x400);       // floor face normal
    h16(b,0x4a,0xfc00);        // -X edge
    h16(b,0x50+4,0xfc00);      // -Z edge
    h16(b,0x56,0x400);h16(b,0x56+4,0x400);
    const size_t t=0x5c+16;
    h32(b,t,0x100000);
    h16(b,t+8,1);h16(b,t+10,2);h16(b,t+12,3);
    h32(b,0x7c,0x80000004u);
    h16(b,0x80,0);h16(b,0x82,1);h16(b,0x84,0);
    StageCollision collision;
    assert(collision.load(b));
    assert(collision.prism_count()==2);
    StageCollision::GroundHit hit;
    assert(collision.ground_below(2,10,2,hit));
    assert(std::abs(hit.y)<1e-3 && hit.prism==1);
    assert(!collision.ground_below(-1,10,2,hit));
    StagePilot player;
    player.place(2,10,2,collision);
    assert(player.grounded && player.y==0);
    player.update(collision,0,0,true);
    assert(!player.grounded && player.y>0);
    for(int i=0;i<120;++i)player.update(collision,0,0,false);
    assert(player.grounded && std::abs(player.y)<1e-3);
    StageFollowCamera camera;
    for(int i=0;i<100;++i)camera.follow(player);
    assert(std::abs(camera.targetY-75)<1);
    h32(b,0,0xfffffff0u);
    assert(!collision.load(b));
    assert(!collision.ready());
    std::cout<<"Original KCL layout, octree floor, native pilot and follow-camera passed.\n";
}
