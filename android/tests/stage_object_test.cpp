#include "runtime/stage_assets.h"

#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {
constexpr uint32_t base = 0x02110000u;

void put16(std::vector<uint8_t> &data, size_t at, uint16_t value) {
    assert(at + 2 <= data.size());
    data[at] = static_cast<uint8_t>(value);
    data[at + 1] = static_cast<uint8_t>(value >> 8);
}
void put32(std::vector<uint8_t> &data, size_t at, uint32_t value) {
    assert(at + 4 <= data.size());
    for (unsigned i = 0; i < 4; ++i)
        data[at + i] = static_cast<uint8_t>(value >> (8 * i));
}
template <typename F> void must_reject(F action) {
    bool rejected = false;
    try { action(); }
    catch (const std::runtime_error &) { rejected = true; }
    assert(rejected);
}
}

int main() {
    sm64ds::Cartridge::DataImage overlay{base, std::vector<uint8_t>(0x200)};
    auto &bytes = overlay.bytes;
    sm64ds::StageAssets::Descriptor desc{};
    desc.objectsAddress = base + 0x20;
    desc.subTablesAddress = base + 0x30;
    desc.subTableCount = 2;

    // ObjTable: u16 count @ +0; u32 entries @ +4.
    put16(bytes, 0x20, 3);
    put32(bytes, 0x24, base + 0x80);
    bytes[0x80] = 0x02; // type 2, every star
    bytes[0x88] = 0x65; // type 5, star 3
    bytes[0x90] = 0x23; // type 3, star 1
    bytes[0x81] = 0xb4; // unmodified actor parameter bytes

    // The first area's 12-byte record points to another ObjTable.
    put32(bytes, 0x30, base + 0x50);
    put32(bytes, 0x3c, 0); // absent area 1, valid
    put16(bytes, 0x50, 2);
    put32(bytes, 0x54, base + 0xa0);
    bytes[0xa0] = 0xe7; // type 7, star 7
    bytes[0xa8] = 0x1f; // type 31, every star

    auto objects = sm64ds::StageAssets::decode_object_catalog(overlay, desc);
    assert(objects.size() == 5);
    assert(objects[0].area == -1 && objects[0].guestAddress == base + 0x80);
    assert(objects[0].type == 2 && objects[0].starFilter == 0);
    assert(objects[0].data[1] == 0xb4);
    assert(objects[1].type == 5 && objects[1].starFilter == 3);
    assert(objects[1].selected_for(3) && !objects[1].selected_for(1));
    assert(objects[2].type == 3 && objects[2].selected_for(1));
    assert(objects[3].area == 0 && objects[3].guestAddress == base + 0xa0);
    assert(objects[3].type == 7 && objects[3].selected_for(7));
    assert(objects[4].area == 0 && objects[4].type == 31);
    assert(objects[4].selected_for(1) && objects[4].selected_for(7));

    // The native pointer width never changes the 32-bit ROM offsets. Invalid
    // tables and truncated payloads must fail before an actor can be spawned.
    put32(bytes, 0x24, base + 0x1f8);
    must_reject([&] { (void)sm64ds::StageAssets::decode_object_catalog(overlay, desc); });
    put32(bytes, 0x24, base + 0x80);
    put32(bytes, 0x54, base - 8);
    must_reject([&] { (void)sm64ds::StageAssets::decode_object_catalog(overlay, desc); });
    put32(bytes, 0x54, base + 0xa0);
    desc.subTableCount = 128;
    must_reject([&] { (void)sm64ds::StageAssets::decode_object_catalog(overlay, desc); });
    desc.subTableCount = 2;
    desc.subTablesAddress = base + 0x1fc;
    must_reject([&] { (void)sm64ds::StageAssets::decode_object_catalog(overlay, desc); });
}
