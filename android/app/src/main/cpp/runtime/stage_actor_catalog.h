#pragma once
#include "stage_assets.h"
#include <cstdint>
#include <vector>
namespace sm64ds {
// Cartridge-defined spawn placement, not native dActor_c instances.
struct StageActorPlacement {
    uint16_t actorId=0,parameter=0;
    int16_t x=0,y=0,z=0,yaw=0;
    int area=-1;
};
class StageActorCatalog {
public:
    // Overlay 2 owns the original loader-dispatch and actor-ID tables.
    bool read(const StageAssets& level,const Cartridge::DataImage& objectOv,
              unsigned star,unsigned entrance);
    bool has_entrance() const {return entranceFound_;}
    const StageActorPlacement& entrance() const {return entrance_;}
    const std::vector<StageActorPlacement>& actors() const {return actors_;}
    unsigned category_count() const {return categories_;}
private:
    bool entranceFound_=false;
    StageActorPlacement entrance_{};
    unsigned categories_=0;
    std::vector<StageActorPlacement> actors_;
};
}
