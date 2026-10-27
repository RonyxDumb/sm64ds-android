#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace sm64ds {
// Checked native KCL reader, not a cast of the retail dBgW_Kc vtable.
class StageCollision {
public:
    struct GroundHit { double y=0; uint16_t surface=0, prism=0; };
    bool load(const std::vector<uint8_t>& file);
    bool ground_below(double x,double y,double z,GroundHit& hit) const;
    size_t prism_count() const {return prisms_;}
    bool ready() const {return !file_.empty();}
    double center_x() const {return (double(origin_[0])/64.0)+double(~xmask_)/2.0;}
    double center_z() const {return (double(origin_[2])/64.0)+double(~zmask_)/2.0;}
    double upper_y() const {return (double(origin_[1])/64.0)+double(~ymask_);}
private:
    std::vector<uint8_t> file_;
    uint32_t positions_=0,normals_=0,prismStart_=0,octree_=0;
    uint32_t xmask_=0,ymask_=0,zmask_=0,shift_=0,yshift_=0,zshift_=0;
    int32_t origin_[3]{};
    size_t vertices_=0,normalsCount_=0,prisms_=0;
    uint16_t u16(size_t off) const;
    int16_t s16(size_t off) const;
    uint32_t u32(size_t off) const;
    int32_t s32(size_t off) const;
    void search_leaf(size_t node,uint32_t word,double x,double y,double z,
                     double& best,GroundHit& hit) const;
};
// Provisional ARM64-owned Player/camera motion records. These are not the
// retail Player::Behavior / dCamera_c::Behavior state machines.
struct StagePilot {
    double x=0,y=0,z=0,vy=0,heading=0;
    bool grounded=false;
    void place(double x,double y,double z,const StageCollision& world);
    void update(const StageCollision& world,float stickX,float stickY,bool jump);
};
struct StageFollowCamera {
    double x=0,y=0,z=0,targetX=0,targetY=0,targetZ=0,yaw=0;
    void follow(const StagePilot& player);
};
}
