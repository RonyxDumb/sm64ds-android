#include "packed_audio.h"
#include "packed_ptr.h"
#include "pc_audio/archive.h"
#include <array>
#include <stdexcept>
#include <string>

namespace sm64ds {
namespace {
using packed::Ptr;
// Playback owns the real, fully resident native SDAT/PCM cache. SDK sound-heap
// save/restore tracks group leases in that owner, rather than freeing memory
// still read by the audio thread or inventing a DS heap pointer.
struct SoundCache {
    uint32_t tag,level;
    uint64_t active;
    std::array<uint64_t,32> saved;
};
constexpr uint32_t CacheTag=0x53444154;
SoundCache &cache(uint32_t address) {
    if (!address) throw std::runtime_error("Missing native sound-cache owner.");
    auto &value=*static_cast<SoundCache *>(packed::Space::current().data(address,sizeof(SoundCache)));
    if (value.tag!=CacheTag || value.level>=value.saved.size())
        throw std::runtime_error("Invalid native sound-cache owner.");
    return value;
}
int load_group(uint32_t group,uint32_t owner) {
    auto &value=cache(owner);
    if (group>=64 || !pc_group_ready(group))
        throw std::runtime_error("Native SDAT group is not ready for playback: "+std::to_string(group));
    value.active|=uint64_t(1)<<group;
    return 1;
}
int save_sound_cache(uint32_t owner) {
    auto &value=cache(owner);
    if (value.level+1>=value.saved.size())
        throw std::runtime_error("Native sound-cache snapshot capacity exceeded.");
    value.saved[++value.level]=value.active;
    return static_cast<int>(value.level);
}
int restore_sound_cache(uint32_t owner,int level) {
    auto &value=cache(owner);
    if (level<0 || static_cast<unsigned>(level)>value.level) return 0;
    value.active=value.saved[static_cast<unsigned>(level)];
    value.level=static_cast<uint32_t>(level);
    return 1;
}
}
void bind_packed_audio(packed::Space &space) {
    space.bind(0x020134d8u,&load_group);
    space.bind(0x0205117cu,&save_sound_cache);
    space.bind(0x020510a4u,&restore_sound_cache);
}
void initialize_packed_audio(packed::Space &space) {
    packed::Space::Scope scope(space);
    const uint32_t owner=space.allocate(sizeof(SoundCache),8);
    auto &value=*static_cast<SoundCache *>(space.data(owner,sizeof(SoundCache)));
    value.tag=CacheTag;
    *Ptr<uint32_t>(0x0209b498u)=owner;
    // The original group loader establishes its common-group save point and
    // game bank state using the same ready SDAT cache used by the native mixer.
    Ptr<void(int,int)>(0x0201200cu)(0,0x20);
}
}
