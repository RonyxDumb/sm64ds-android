// This file is compiled through the same transformation as the game TUs.
namespace sm64ds::migration::fixture {
#define LOAD(base, i) (*(short *)((char *)(base) + (i) * 4))
int load(char *base, int index) { return LOAD(base, index); }
int arithmetic(int value, int scale) { return (int)(value) + (scale) * 4; }
struct Object {
    int value;
    void bump() { ++value; }
};
using Method = void (Object::*)();
int member_call() {
    Object object{};
    Method method = &Object::bump;
    (object.*method)();
    return object.value;
}
}
