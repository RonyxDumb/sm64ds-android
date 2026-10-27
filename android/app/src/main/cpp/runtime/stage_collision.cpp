#include "stage_collision.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace sm64ds {
namespace {
uint16_t rd16(const std::vector<uint8_t>& b,size_t i){
    return uint16_t(b[i])|(uint16_t(b[i+1])<<8);
}
uint32_t rd32(const std::vector<uint8_t>& b,size_t i){
    return uint32_t(rd16(b,i))|(uint32_t(rd16(b,i+2))<<16);
}
bool span(size_t at,size_t n,size_t end){return at<=end&&n<=end-at;}
}
uint16_t StageCollision::u16(size_t i) const{return rd16(file_,i);}
int16_t StageCollision::s16(size_t i) const{return static_cast<int16_t>(u16(i));}
uint32_t StageCollision::u32(size_t i) const{return rd32(file_,i);}
int32_t StageCollision::s32(size_t i) const{return static_cast<int32_t>(u32(i));}
bool StageCollision::load(const std::vector<uint8_t>& b){
    file_.clear();positions_=normals_=prismStart_=octree_=0;
    vertices_=normalsCount_=prisms_=0;
    if(b.size()<0x38||b.size()>16u*1024u*1024u)return false;
    const uint32_t p=rd32(b,0),n=rd32(b,4),t=rd32(b,8),o=rd32(b,12);
    if(p<0x38||p>=n||n>=t||t>=o||o>=b.size()||
       (n-p)%12||(t-n)%6||(o-t)%16)return false;
    const size_t vc=(n-p)/12,nc=(t-n)/6,tc=(o-t)/16;
    if(!vc||!nc||!tc||vc>65536||nc>65536||tc>65536)return false;
    uint32_t sh=rd32(b,0x2c),ys=rd32(b,0x30),zs=rd32(b,0x34);
    if(sh>29||ys>29||zs>29)return false;
    uint32_t xm=rd32(b,0x20),ym=rd32(b,0x24),zm=rd32(b,0x28);
    if(uint64_t(~xm)+1>=(1ull<<30)||uint64_t(~ym)+1>=(1ull<<30)||
       uint64_t(~zm)+1>=(1ull<<30))return false;
    file_=b;positions_=p;normals_=n;prismStart_=t;octree_=o;
    vertices_=vc;normalsCount_=nc;prisms_=tc;
    xmask_=xm;ymask_=ym;zmask_=zm;shift_=sh;yshift_=ys;zshift_=zs;
    for(unsigned i=0;i<3;++i)origin_[i]=s32(0x14+4*i);
    return true;
}
void StageCollision::search_leaf(size_t node,uint32_t word,double x,double y,double z,
                                  double& best,GroundHit& out) const {
    if(!(word&0x80000000u))return;
    const size_t off=node+size_t(word&0x7fffffffu);
    if(!span(off,4,file_.size()))return;
    // Retail KCL leaf starts with ignored u16, followed by prism IDs and 0.
    for(size_t at=off+2,steps=0;steps<prisms_&&span(at,2,file_.size());at+=2,++steps){
        const uint16_t id=u16(at);
        if(!id)break;
        if(id>=prisms_)return;
        const size_t tr=prismStart_+size_t(id)*16;
        const uint16_t pi=u16(tr+4),ni=u16(tr+6),e1=u16(tr+8),e2=u16(tr+10),e3=u16(tr+12);
        if(pi>=vertices_||ni>=normalsCount_||e1>=normalsCount_||
           e2>=normalsCount_||e3>=normalsCount_)return;
        const size_t v=positions_+size_t(pi)*12,n=normals_+size_t(ni)*6;
        double nx=s16(n),ny=s16(n+2),nz=s16(n+4);
        if(ny<=0)continue;
        const double dx=x-s32(v),dz=z-s32(v+8);
        const double dy=-(dx*nx+dz*nz)/ny;
        if(!std::isfinite(dy))continue;
        auto dot=[&](uint16_t i){
            size_t off=normals_+size_t(i)*6;
            return dx*s16(off)+dy*s16(off+2)+dz*s16(off+4);
        };
        if(dot(e1)>0x20000||dot(e2)>0x20000)continue;
        const double third=dot(e3);
        if(third< -0x20000||third>double(s32(tr))+0x20000)continue;
        const double floorY=dy+s32(v+4);
        if(dx*nx+(y-s32(v+4))*ny+dz*nz<0)continue;
        if(floorY<origin_[1]||floorY>=y||floorY<=best)continue;
        best=floorY;out={floorY/64.,u16(tr+14),id};
    }
}
bool StageCollision::ground_below(double wx,double wy,double wz,GroundHit& hit) const {
    if(file_.empty()||!std::isfinite(wx)||!std::isfinite(wy)||!std::isfinite(wz))return false;
    // Original collision format stores positions at 1/64th of a world unit.
    const double x=wx*64.,y=wy*64.,z=wz*64.;
    const double xx=(x-origin_[0])/64.,zz=(z-origin_[2])/64.;
    if(xx<0||zz<0||xx>double(~xmask_)||zz>double(~zmask_))return false;
    double yy=(y-origin_[1])/64.;
    if(yy<0)return false;
    uint32_t cx=uint32_t(xx),cz=uint32_t(zz);
    uint32_t cy=uint32_t(std::min(yy,double(~ymask_)));
    double best=-std::numeric_limits<double>::infinity();
    for(unsigned attempts=0;attempts<1024;++attempts){
        uint32_t sh=shift_;
        const uint64_t idx=(uint64_t(cz>>sh)<<zshift_)|
                           (uint64_t(cy>>sh)<<yshift_)|(cx>>sh);
        if(idx>file_.size()/4||!span(octree_,size_t(idx)*4+4,file_.size()))break;
        size_t node=octree_;uint32_t word=u32(node+size_t(idx)*4);
        for(unsigned depth=0;!(word&0x80000000u)&&depth<31;++depth){
            if(!sh)return std::isfinite(best);
            const size_t child=node+size_t(word);
            if(!span(child,32,file_.size()))return std::isfinite(best);
            --sh;
            const unsigned oct=(((cz>>sh)&1u)<<2)|(((cy>>sh)&1u)<<1)|((cx>>sh)&1u);
            node=child;word=u32(node+oct*4);
        }
        if(!(word&0x80000000u))break;
        search_leaf(node,word,x,y,z,best,hit);
        const uint32_t step=1u<<sh;
        const uint32_t next=cy&~(step-1u);
        if(next==0)break;
        cy=next-1;
    }
    return std::isfinite(best);
}
void StagePilot::place(double px,double py,double pz,const StageCollision& world){
    x=px;y=py;z=pz;vy=0;grounded=false;heading=0;
    StageCollision::GroundHit hit;
    if(world.ground_below(x,y+300,z,hit)){y=hit.y;grounded=true;}
}
void StagePilot::update(const StageCollision& world,float stickX,float stickY,bool jump){
    if(!std::isfinite(stickX)||!std::isfinite(stickY))return;
    const double sx=std::clamp(double(stickX),-1.,1.);
    const double sz=std::clamp(double(stickY),-1.,1.);
    const double len=std::max(1.,std::hypot(sx,sz));
    if(sx!=0||sz!=0)heading=std::atan2(sx,sz);
    // Temporary host motion integrator; NOT the retail Player state machine.
    x+=(sx/len)*3.5;z+=(sz/len)*3.5;
    if(jump&&grounded){vy=13;grounded=false;}
    vy=std::max(-36.,vy-0.8);
    const double previousY=y;
    y+=vy;
    if(vy<=0){
        StageCollision::GroundHit floor;
        if(world.ground_below(x,previousY+4,z,floor)&&
           y<=floor.y+4&&y>=floor.y-36){
            y=floor.y;vy=0;grounded=true;
        }else grounded=false;
    }
}
void StageFollowCamera::follow(const StagePilot& p){
    targetX+=.20*(p.x-targetX);targetY+=.20*(p.y+75-targetY);
    targetZ+=.20*(p.z-targetZ);
    const double wantX=targetX-std::sin(yaw)*320;
    const double wantZ=targetZ-std::cos(yaw)*320;
    x+=.10*(wantX-x);y+=.10*(targetY+140-y);z+=.10*(wantZ-z);
}
}
