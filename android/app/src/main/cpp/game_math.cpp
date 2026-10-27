// Compile and exercise recovered game math without changing the matching source.
#include "common.h"
#include <stdexcept>
extern "C" Fix12i DotVec3(const Vector3 *, const Vector3 *);
extern "C" void CrossVec3(const Vector3 *, const Vector3 *, Vector3 *);
extern "C" void Vec3_Add(Vector3 *, const Vector3 *, const Vector3 *);
extern "C" void Vec3_Sub(Vector3 *, const Vector3 *, const Vector3 *);
extern "C" void Vec3_MulScalarInPlace(int *, int);
extern "C" void MulVec3Mat3x3(const int *, const int *, int *);
void verify_game_math() {
    const Vector3 x{4096, 0, 0}, y{0, 4096, 0};
    Vector3 out{};
    CrossVec3(&x, &y, &out);
    if (DotVec3(&x, &x) != 4096 || DotVec3(&x, &y) != 0 ||
        out.x != 0 || out.y != 0 || out.z != 4096)
        throw std::runtime_error("Native fixed-point game math self-test failed.");

    // Exercise original gameplay vector functions in the same native binary.
    Vector3 added{}, subtracted{};
    Vec3_Add(&added, &x, &y);
    Vec3_Sub(&subtracted, &added, &y);
    int scaled[3] = {added.x, added.y, added.z};
    Vec3_MulScalarInPlace(scaled, 2048);
    const int identity[9] = {4096, 0, 0, 0, 4096, 0, 0, 0, 4096};
    int rotated[3] = {};
    MulVec3Mat3x3(scaled, identity, rotated);
    if (subtracted.x != x.x || subtracted.y != x.y || subtracted.z != x.z ||
        scaled[0] != 2048 || scaled[1] != 2048 || scaled[2] != 0 ||
        rotated[0] != scaled[0] || rotated[1] != scaled[1] || rotated[2] != scaled[2])
        throw std::runtime_error("Native recovered vector operations failed.");
}
