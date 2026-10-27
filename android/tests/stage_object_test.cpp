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
    // The original actor callback table lives in overlay 2, separate from
    // the level overlay. Its pointer is a guest ARM address, NOT callable on
    // AArch64. The u16 actor ID table follows it at 0x0210cbf4.
    sm64ds::Cartridge::DataImage actorOverlay{
        0x0210c000u, std::vector<uint8_t>(0x2000)};
    put32(actorOverlay.bytes, 0xcbb8 - 0xc000 + 5 * 4, 0x020fe960u);
    put16(actorOverlay.bytes, 0xcbf4 - 0xc000 + 0x12 * 2, 0x015au);
    assert(sm64ds::StageAssets::original_loader(actorOverlay, 5) == 0x020fe960u);
    must_reject([&] { (void)sm64ds::StageAssets::original_loader(actorOverlay, 15); });
    // Source-defined ObjSubTable: count at +1, entry pointer at +4.
    objects[1].entryCount = 2;
    objects[1].entriesAddress = base + 0xc0;
    objects[1].loaderAddress = 0x020fe960u;
    put16(bytes, 0xc0, uint16_t(0x12 | (3 << 9)));
    put16(bytes, 0xc2, uint16_t(-2));
    put16(bytes, 0xc4, 4);
    put16(bytes, 0xc6, uint16_t(-7));
    put16(bytes, 0xc8, uint16_t(0x1ff | (0x1b << 9)));
    auto spawns = sm64ds::StageAssets::decode_simple_spawns(overlay, actorOverlay, objects[1]);
    assert(spawns.size() == 2);
    assert(spawns[0].actorIndex == 0x12 && spawns[0].actorId == 0x015a);
    assert(spawns[0].param == 3 && spawns[0].x == -8192 &&
           spawns[0].y == 16384 && spawns[0].z == -28672);
    assert(spawns[0].area == -1 && !spawns[0].minimapChange);
    assert(spawns[0].starFilter == 3 && spawns[0].selected_for(3) &&
           !spawns[0].selected_for(1));
    assert(spawns[1].minimapChange && spawns[1].actorIndex == 0x1ff &&
           spawns[1].param == 0xb);
    // Recover a real 0x10-byte StandardEntry, also used for the Player
    // entrance table. Its rotation and parameter are not part of the
    // eight-byte outer category descriptor.
    auto standard = objects[1];
    standard.loaderAddress = 0x020fe6c8u;
    standard.entryCount = 1;
    standard.entriesAddress = base + 0x100;
    put16(bytes, 0x100, 0x21);
    put16(bytes, 0x102, uint16_t(-1200));
    put16(bytes, 0x104, 254);
    put16(bytes, 0x106, 6800);
    put16(bytes, 0x108, 10);
    put16(bytes, 0x10a, uint16_t(-32768));
    put16(bytes, 0x10c, 4);
    put16(bytes, 0x10e, 0x0387);
    put16(actorOverlay.bytes, 0xcbf4 - 0xc000 + 0x21 * 2, 0x00bfu);
    auto entrance = sm64ds::StageAssets::decode_standard_spawns(overlay, actorOverlay, standard);
    assert(entrance.size() == 1 && entrance[0].entrance);
    assert(entrance[0].actorIndex == 0x21 && entrance[0].actorId == 0x00bf);
    assert(entrance[0].x == -1200 * 4096 && entrance[0].y == 254 * 4096 &&
           entrance[0].z == 6800 * 4096);
    assert(entrance[0].rotX == 10 && entrance[0].rotY == -32768 &&
           entrance[0].rotZ == 4 && entrance[0].param == 0x0387);
    assert(entrance[0].selected_for(3) && !entrance[0].selected_for(2));
    standard.loaderAddress = 0x020fe8acu;
    auto normal = sm64ds::StageAssets::decode_standard_spawns(overlay, actorOverlay, standard);
    assert(normal.size() == 1 && !normal[0].entrance);
    assert(entrance[0].categoryAddress == standard.guestAddress);
    put16(bytes, 0x100, 326);
    must_reject([&] {
        (void)sm64ds::StageAssets::decode_standard_spawns(overlay, actorOverlay, standard);
    });
    put16(bytes, 0x100, 0x21);

    auto bad = objects[1];
    bad.loaderAddress = 0;
    must_reject([&] {
        (void)sm64ds::StageAssets::decode_simple_spawns(overlay, actorOverlay, bad);
    });
    bad = objects[1];
    bad.entriesAddress = base + 0x1fc;
    must_reject([&] {
        (void)sm64ds::StageAssets::decode_simple_spawns(overlay, actorOverlay, bad);
    });
    // Index 326 is still inside the nine-bit field, but outside the
    // recovered 326-element u16 table. Do not accept adjacent ROM data as IDs.
    put16(bytes, 0xc0, 326);
    must_reject([&] {
        (void)sm64ds::StageAssets::decode_simple_spawns(overlay, actorOverlay, objects[1]);
    });
    put16(bytes, 0xc0, uint16_t(0x12 | (3 << 9)));


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
