#include "runtime/wipe_runtime.h"
#include "runtime/boot_seams.h"
#include "ntr/ppu.h"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>

namespace {
void configure_screen(unsigned base,unsigned vram,unsigned palette) {
    ntr::io_write(base,(1u<<16)|(1u<<8),4);
    ntr::io_write(base+8,(1u<<8)|(1u<<2),2);
    ntr::io_write(base+0x50,0,2); ntr::io_write(base+0x6c,0,2);
    auto *map=static_cast<uint16_t *>(ntr::host_ptr(vram+0x800));
    std::fill(map,map+1024,1);
    std::memset(ntr::host_ptr(vram+0x4000+32),0x11,32);
    auto *colors=static_cast<uint16_t *>(ntr::host_ptr(palette));
    colors[0]=0; colors[1]=0x03e0;
}
void setup() {
    assert(ntr::io_init());
    sm64ds::reset_wipe();
    std::memset(ntr::host_ptr(ntr::IO_BASE),0,ntr::IO_SIZE);
    ntr::io_write(0x04000240,0x81,1);
    ntr::io_write(0x04000241,0x82,1);
    ntr::io_write(0x04000242,0x84,1);
    configure_screen(0x04000000,0x06000000,0x05000000);
    configure_screen(0x04001000,0x06200000,0x05000400);
}
}

void wipe_runtime_tests() {
    setup();
    ntr::Framebuffer main{},sub{};
    auto *oam=static_cast<uint16_t *>(ntr::host_ptr(0x07000000));
    for (unsigned i=0;i<128;++i) oam[i*4]=0x200;
    oam[0]=8; oam[1]=8; oam[2]=1;
    std::memset(ntr::host_ptr(0x06400020),0x11,32);
    *static_cast<uint16_t *>(ntr::host_ptr(0x05000202))=0x001f;
    ntr::io_write(0x04000000,(1u<<16)|(1u<<8)|(1u<<12)|(3u<<13),4);
    ntr::io_write(0x04000040,(10u<<8)|20,2);
    ntr::io_write(0x04000042,30,2);
    ntr::io_write(0x04000044,(10u<<8)|20,2);
    ntr::io_write(0x04000046,30,2);
    ntr::io_write(0x04000048,1|(63u<<8),2);
    ntr::io_write(0x0400004a,0,2);
    ntr::ppu_scanout(ntr::ENGINE_A,main); ntr::ppu_scanout_obj(ntr::ENGINE_A,main);
    assert(main.px[10][10]==0xff00ff00); // WIN0 beats WIN1, hides OBJ
    assert(main.px[9][9]==0xffff0000);   // WIN1 allows OBJ
    assert(main.px[40][40]==0xff000000); // outside hides every layer
    ntr::io_write(0x04000050,0xff,2); ntr::io_write(0x04000054,16,2);
    ntr::ppu_scanout(ntr::ENGINE_A,main); ntr::ppu_scanout_obj(ntr::ENGINE_A,main);
    assert(main.px[10][10]==0xff00ff00); // WIN0 disables color effects
    assert(main.px[9][9]==0xff000000);   // WIN1 darkens OBJ as well as BG
    ntr::io_write(0x04000050,0xbf,2);
    ntr::ppu_scanout(ntr::ENGINE_A,main); ntr::ppu_scanout_obj(ntr::ENGINE_A,main);
    assert(main.px[10][10]==0xff00ff00 && main.px[9][9]==0xffffffff);
    ntr::io_write(0x04000050,0,2);
    ntr::io_write(0x04000000,(1u<<16)|(1u<<8)|(1u<<13),4);
    ntr::io_write(0x04000040,(250u<<8)|5,2);
    ntr::io_write(0x04000044,(190u<<8)|2,2);
    ntr::ppu_scanout(ntr::ENGINE_A,main);
    assert(main.px[0][0]==0xff00ff00 && main.px[0][250]==0xff00ff00);
    assert(main.px[0][5]==0xff000000 && main.px[2][0]==0xff000000);

    // Engine-specific line tables override horizontal bounds, and are also
    // applied when the OBJ pass follows the BG pass.
    ntr::io_write(0x04000044,192,2);
    ntr::io_write(0x04001000,(1u<<16)|(1u<<8)|(1u<<13),4);
    ntr::io_write(0x04001044,192,2); ntr::io_write(0x04001048,1,2);
    ntr::io_write(0x0400104a,0,2);
    std::array<uint32_t,192> left{},right{};
    left.fill(128); right.fill((128u<<8)|255);
    ntr::ppu_set_window_lines(ntr::ENGINE_A,left.data());
    ntr::ppu_set_window_lines(ntr::ENGINE_B,right.data());
    ntr::ppu_scanout(ntr::ENGINE_A,main); ntr::ppu_scanout(ntr::ENGINE_B,sub);
    assert(main.px[96][64]==0xff00ff00 && main.px[96][192]==0xff000000);
    assert(sub.px[96][64]==0xff000000 && sub.px[96][192]==0xff00ff00);
    ntr::ppu_clear_window_lines();

    // Both original table builders, original IRQ table flips, forward and
    // backward lifecycle, endpoint disarm and mid-transition type requests.
    for (int type:{0,2}) {
        setup();
        data_0209f61c.func_0202ec9c(type);
        assert(data_0209f61c.SetForwardTime(30)==0);
        assert(sm64ds::wipe_hblank_active() && data_0209f61c.needsCleanup==1);
        for (unsigned frame=0;frame<100 && data_0209f61c.state!=4;++frame) {
            data_0209f61c.AdvanceFade();
            sm64ds::prepare_wipe_scanlines();
            if (frame==40) {
                assert(sm64ds::wipe_hblank_active());
                assert(data_0209f608<2 && data_0209f60c<2);
                ntr::ppu_scanout(ntr::ENGINE_A,main); ntr::ppu_scanout(ntr::ENGINE_B,sub);
                assert(std::memcmp(main.px,sub.px,sizeof main.px)==0);
                if (type==2) {
                    const auto *table=data_0209f648+data_0209f608*0x300;
                    for (unsigned y=0;y<192;++y) {
                        assert(table[y*4]==table[(191-y)*4]);
                        assert(table[y*4+3]==table[(191-y)*4+3]);
                    }
                    assert(table[96*4]!=table[0]);
                }
            }
        }
        assert(data_0209f61c.state==4 && data_0209f61c.IsAtEnd());
        assert(!sm64ds::wipe_hblank_active() && !data_0209f61c.needsCleanup);
        assert((ntr::io_read(0x04000000,4)&0xe000)==0);
        data_0209f61c.SetBackwardTime(30);
        assert(sm64ds::wipe_hblank_active());
        for (unsigned frame=0;frame<100 && data_0209f61c.state!=2;++frame) {
            data_0209f61c.AdvanceFade(); sm64ds::prepare_wipe_scanlines();
        }
        assert(data_0209f61c.state==2 && data_0209f61c.IsAtStart());
        assert(!sm64ds::wipe_hblank_active());
        data_0209f61c.SetForwardTime(30); data_0209f61c.AdvanceFade();
        assert(data_0209f61c.func_0202ec9c(type==2?0:2)==1);
        assert(data_0209f61c.typePending==1);
        for (unsigned frame=0;frame<100 && data_0209f61c.state!=4;++frame)
            data_0209f61c.AdvanceFade();
        assert(data_0209f61c.type==(type==2?0:2) && !data_0209f61c.typePending);
        data_0209f61c.SetBackwardTime(30);
        assert(sm64ds::wipe_hblank_active());
        sm64ds::reset_wipe();
        assert(!sm64ds::wipe_hblank_active() && data_0209f61c.state==0);
    }
    // Type 1 delegates to the real native color interpolator, without IRQs.
    setup(); data_0209f61c.func_0202ec9c(1); data_0209f61c.SetToStart();
    data_0209f61c.SetForwardTime(2);
    for (int i=0;i<3;++i) data_0209f61c.AdvanceFade();
    assert(data_0209f61c.IsAtEnd() && !sm64ds::wipe_hblank_active());
    sm64ds::reset_wipe();
    std::cout << "Original minigame wipe: both scanline tables, window priority/wrap/effects, two engines, fade lifecycle and IRQ teardown passed.\n";
}
