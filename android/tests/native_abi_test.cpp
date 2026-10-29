#include "runtime/native_abi.h"
#include "runtime/stored_member.h"
#include "runtime/packed_memory.h"
#include <cassert>
#include <cstdio>

namespace {
int callback(int value) { return value * 3 + 1; }
struct Left { int a; };
struct Right { int b; };
struct Both : Left, Right {};
struct MemberLeft { int left=5; virtual int value(){return left;} };
struct MemberRight { int right=13; virtual int value(){return right;} };
struct MemberBoth : MemberLeft, MemberRight { int total(){return left+right;} };
struct PackedMember {};
int packed_member(uint32_t object){return *static_cast<int*>(sm64ds::packed::Space::current().data(object+4,4));}
}
namespace sm64ds::migration::fixture {
int load(char *, int);
int arithmetic(int, int);
int member_call();
}

int main() {
    using namespace sm64ds;
    GameHeap heap;
    packed::Space space(heap);
    packed::Space::Scope scope(space);
    const auto handle = space.allocate(64);
    auto *data = static_cast<int *>(space.data(handle, 64));
    assert(reinterpret_cast<uintptr_t>(data) > UINT32_MAX);
    data[0] = 42;
    assert(native_abi::uword(data) == handle);
    assert(native_abi::pointer<int *>(native_abi::word(data)) == data);
    assert(native_abi::pointer<int *>(handle + sizeof(int)) == data + 1);
    native_abi::StoredPointer<int> stored(data);
    assert(static_cast<uint32_t>(stored) == handle && *stored == 42);
    assert(static_cast<int *>(stored) == data);
    assert(stored[0] == 42);
    stored = nullptr;
    assert(static_cast<int *>(stored) == nullptr);
    assert(native_abi::pointer<void *>(0) == nullptr);
    assert(native_abi::word(-5) == -5 && native_abi::uword(-1) == UINT32_MAX);

    using Function = int (*)(int);
    const auto code = native_abi::uword(&callback);
    assert(native_abi::pointer<Function>(code)(7) == 22);
    bool refused = false;
    try { (void)native_abi::pointer<Function>(0x02000000u); }
    catch (const std::runtime_error &) { refused = true; }
    assert(refused);
    refused = false;
    try { (void)native_abi::pointer<int *>(0x01000000u); }
    catch (const std::runtime_error &) { refused = true; }
    assert(refused);

    Both object{};
    assert(native_abi::pointer<Right *>(&object) == static_cast<Right *>(&object));
    short lanes[6] = {10, 0, 20, 0, 30, 0};
    assert(migration::fixture::load(reinterpret_cast<char *>(lanes), 2) == 30);
    assert(migration::fixture::arithmetic(-5, 3) == 7);
    assert(migration::fixture::member_call() == 1);
    native_abi::StoredMember<MemberBoth,int()> method(&MemberBoth::total);
    MemberBoth nativeObject;
    assert(((&nativeObject)->*method)()==18);
    native_abi::StoredMember<MemberBoth,int()> adjusted(&MemberRight::value);
    assert(adjusted.invoke(&nativeObject)==13);
    native_abi::StoredMember<MemberBoth,int()> virtualMethod(&MemberLeft::value);
    assert(virtualMethod.invoke(&nativeObject)==5);
    native_abi::StoredMember<MemberBoth,int()> nullMethod(nullptr);
    assert(nullMethod==nullptr);
    const uint32_t packedObject=space.allocate(8),vtable=space.allocate(4);
    const auto codeHandle=native_abi::uword(&packed_member);
    *static_cast<uint32_t*>(space.data(vtable,4))=codeHandle;
    *static_cast<uint32_t*>(space.data(packedObject,4))=vtable;
    *static_cast<int*>(space.data(packedObject+4,4))=61;
    native_abi::StoredMember<PackedMember,int()> serialized;
    packed::MemberCall descriptor{0,1};
    std::memcpy(static_cast<void*>(&serialized),&descriptor,sizeof descriptor);
    assert(serialized!=nullptr);
    assert(serialized.invoke(static_cast<PackedMember*>(space.data(packedObject,8)))==61);
    descriptor={codeHandle,0};
    std::memcpy(static_cast<void*>(&serialized),&descriptor,sizeof descriptor);
    assert(serialized.invoke(static_cast<PackedMember*>(space.data(packedObject,8)))==61);
    space.release(vtable);space.release(packedObject);
    space.release(handle);
    std::puts("ARM64 address migration: data, callback, null and base adjustment passed.");
}
