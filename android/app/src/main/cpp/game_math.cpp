// Compile and exercise recovered game math without changing the matching source.
#include "common.h"
#include <stdexcept>
extern "C" Fix12i DotVec3(const Vector3 *, const Vector3 *);
extern "C" void CrossVec3(const Vector3 *, const Vector3 *, Vector3 *);
void verify_game_math() {
    const Vector3 x{4096, 0, 0}, y{0, 4096, 0};
    Vector3 out{};
    CrossVec3(&x, &y, &out);
    if (DotVec3(&x, &x) != 4096 || DotVec3(&x, &y) != 0 ||
        out.x != 0 || out.y != 0 || out.z != 4096)
        throw std::runtime_error("Native fixed-point game math self-test failed.");
}
