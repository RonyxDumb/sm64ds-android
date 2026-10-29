#pragma once
#include "packed_memory.h"
#include <map>
#include <functional>
#include <mutex>
#include <type_traits>
#include <utility>
namespace sm64ds::native_abi {
template<class C,class Signature>class StoredMember;
// Cartridge state rows reserve eight bytes. Native PMFs retain their complete
// ABI representation in a typed registry, independently of a live guest Space.
template<class C,class R,class... Args>class StoredMember<C,R(Args...)>{
public:
 using Native=R(C::*)(Args...);
private:
 uint32_t entry_;int32_t adjustment_;
 struct Method { Native identity; std::function<R(C*,Args...)> invoke; };
 struct Registry{std::mutex mutex;std::map<uint32_t,Method> entries;uint32_t next=0xe0000000u;};
 static Registry &registry(){static Registry table;return table;}
public:
 StoredMember()=default;
 StoredMember(std::nullptr_t):entry_(0),adjustment_(0){}
 template<class B,std::enable_if_t<std::is_base_of_v<B,C>,int> = 0>
 StoredMember(R(B::*value)(Args...)):entry_(0),adjustment_(0){
  if(!value)return;
  const Native identity=static_cast<Native>(value);
  auto &table=registry();std::lock_guard<std::mutex> lock(table.mutex);
  for(const auto &row:table.entries)if(row.second.identity==identity){entry_=row.first;return;}
  if(table.next==UINT32_MAX)throw std::overflow_error("Native member identity overflow");
  entry_=table.next++;
  // Retain the declaring class: C++ performs its base adjustment before
  // invoking the original PMF, including secondary virtual base tables.
  table.entries.emplace(entry_,Method{identity,[value](C *object,Args... args)->R{
   return (static_cast<B*>(object)->*value)(std::forward<Args>(args)...);
  }});
 }
 bool operator==(std::nullptr_t)const{return entry_==0&&!(adjustment_&1);}
 bool operator!=(std::nullptr_t)const{return entry_!=0||(adjustment_&1);}
 explicit operator bool()const{return entry_!=0||(adjustment_&1);}
 R invoke(C *object,Args... args)const{
  std::function<R(C*,Args...)> method;
  if(entry_>=0xe0000000u){auto &table=registry();std::lock_guard<std::mutex> lock(table.mutex);method=table.entries.at(entry_).invoke;}
  if(method)return method(object,std::forward<Args>(args)...);
  auto &space=packed::Space::current();const auto target=space.resolve_member(space.address(object),{entry_,adjustment_});
  using Bound=R(*)(uint32_t,Args...);Bound callback=nullptr;
  static_assert(sizeof callback==sizeof target.code);std::memcpy(&callback,&target.code,sizeof callback);
  return callback(target.object,std::forward<Args>(args)...);
 }
 struct Invocation{C *object;StoredMember member;R operator()(Args... args)const{return member.invoke(object,std::forward<Args>(args)...);}};
 friend Invocation operator->*(C *object,StoredMember member){return {object,member};}
};
struct MemberExtentProbe{};
static_assert(sizeof(StoredMember<MemberExtentProbe,int()>)==8);
static_assert(alignof(StoredMember<MemberExtentProbe,int()>)==4);
static_assert(std::is_trivially_copyable_v<StoredMember<MemberExtentProbe,int()>>);
}
