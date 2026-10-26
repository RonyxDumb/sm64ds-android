#include "runtime/frontend_core.h"
#include "runtime/packed_ptr.h"
#include <cassert>
#include <cstdio>
#include <stdexcept>

using namespace sm64ds;
template<class T> T at(const FrontendCore &c, uint32_t a) {
    T v; std::memcpy(&v,c.data_at(a,sizeof v),sizeof v); return v;
}
int main() {
    GameHeap heap;
    size_t available=heap.memory_left();
    for (int life=0; life<3; ++life) {
        Cartridge::DataImage image{0x020d0000,std::vector<uint8_t>(0x35000)};
        int one=4096;
        std::memcpy(image.bytes.data()+0x7db4,&one,4);
        FrontendCore c(heap,std::move(image),4);
        assert(c.allocations()==23 && !c.has_items());
        uint32_t root=c.root_address();
        assert(c.word_at(0x0210342c)==root);
        assert(reinterpret_cast<uintptr_t>(c.data_at(root,0x188))>UINT32_MAX);
        assert(reinterpret_cast<uintptr_t>(c.data_at(root,0x188))!=root);
        assert(at<uint8_t>(c,c.word_at(root+0x28)+0x3a)==5);
        assert(c.word_at(c.word_at(root+0x44))==c.word_at(root+0x4c)+4);
        assert(c.word_at(c.word_at(root+0x48))==c.word_at(root+0x44)+4);
        for (unsigned i=0;i<3;++i) {
            auto state=c.word_at(root+4+i*4);
            assert(at<int16_t>(c,state)==(i==0?0:i==1?-1:1));
            c.step_state(i);
            assert(at<uint32_t>(c,state+12)==0);
            c.step_state(i);
            assert(at<uint32_t>(c,state+12)==1);
        }
        for (unsigned i=0;i<2;++i) {
            auto cam=c.word_at(root+0x30+i*4);
            assert(at<int>(c,cam+0x10)==4096);
            assert(at<int>(c,cam+0x24)==4096);
            assert(at<int64_t>(c,cam+0xbc)==0x155555555LL);
            for(unsigned j=0;j<12;++j) assert(at<int>(c,cam+0x44+j*4)==(j==0||j==4||j==8?4096:0));
            for(unsigned j=0;j<16;++j) assert(at<int>(c,cam+0x74+j*4)==(j==0||j==5||j==10||j==15?4096:0));
            auto light=c.word_at(root+0x38+i*4);
            assert(at<int16_t>(c,light+0x20)==-4096);
            auto tween=c.word_at(root+0x6c+i*4);
            assert(c.word_at(tween+0x10)==root+0x64+i*4);
        }
        auto pad=c.word_at(root+0x54), touch=c.word_at(root+0x50);
        c.sample(0x41,10,20);
        assert(at<uint16_t>(c,pad)==0x41);
        assert(at<uint32_t>(c,pad+4)==1);
        c.sample(0x41,14,25);
        assert(at<uint32_t>(c,pad+4)==2);
        assert(at<uint16_t>(c,touch)==14);
        c.sample(0,-1,-1);
        assert(at<uint16_t>(c,pad)==0);
        assert(at<uint32_t>(c,pad+0x34)==1);
        FrontendCore::Scope scope(c);
        auto p=packed::Ptr<int>(root);
        assert((p+1).address()==root+4);
        assert(packed::Ptr<int>(p.get()).address()==root);
        // An odd-address byte alias must not bind a misaligned u16[] reference
        // before the source casts it to its real byte view.
        auto alias=packed::ArrayRef<uint16_t>(root+1);
        assert(packed::Ptr<uint8_t>(alias).address()==root+1);
        bool bad=false;
        try { packed::Ptr<void()>(0x020b7138)(); } catch (const std::runtime_error &) { bad=true; }
        assert(bad);
        bad=false;
        try { (void)*packed::Ptr<int>(0xfffffff0); } catch (const std::runtime_error &) { bad=true; }
        assert(bad);
    }
    assert(heap.allocations()==0 && heap.memory_left()==available);
    std::puts("Native frontend: original allocators, root, states, cameras, lights, tweens, input and teardown passed.");
}
