#include "ntr/rt.h"
#include "ntr/mmio.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
// Scheduler contract test uses a register-file seam, so it runs under ASan
// without mapping over the sanitizer's low-address shadow region.
namespace ntr {
static std::array<uint8_t,IO_SIZE> registers{};
bool io_init(){return true;}
uint64_t io_read(uint32_t a,unsigned n){uint64_t value=0;std::memcpy(&value,registers.data()+a-IO_BASE,n);return value;}
void io_write(uint32_t a,uint64_t v,unsigned n){std::memcpy(registers.data()+a-IO_BASE,&v,n);}
}
static int steps=0,hooks=0,cleanup=0;
static void check(bool ok){if(!ok)throw std::runtime_error("runtime assertion failed");}
static void finite(){for(int i=0;i<10;++i){++steps;ntr::rt_vblank_wait();}}
static void forever(){struct Guard{~Guard(){++cleanup;}}guard;for(;;){++steps;ntr::rt_vblank_wait();}}
static bool hook(uint64_t frame){++hooks;check(steps==int(frame));check(ntr::rt_frame()==frame);return true;}
static bool stop_hook(uint64_t frame){return frame<3;}
static bool bad_hook(uint64_t){throw std::runtime_error("hook failed");}
static void bad_game(){throw std::runtime_error("game failed");}
int main(){try{
    check(ntr::rt_run(finite,hook)==10 && hooks==10 && steps==10);
    steps=0;check(ntr::rt_run(forever,nullptr,5)==5 && steps==5 && cleanup==1);
    steps=0;check(ntr::rt_run(forever,stop_hook)==3 && steps==3 && cleanup==2);
    try{ntr::rt_run(forever,bad_hook);check(false);}catch(const std::runtime_error&e){check(std::string(e.what())=="hook failed");}
    check(cleanup==3);
    try{ntr::rt_run(bad_game,nullptr);check(false);}catch(const std::runtime_error&e){check(std::string(e.what())=="game failed");}
    check(ntr::rt_irq_disable()==0);check(ntr::rt_irq_enable()==0x80);ntr::rt_vblank_wait();
    steps=0;check(ntr::rt_run(finite,nullptr)==10);
    std::puts("Portable VBlank scheduler tests passed");
}catch(const std::exception&e){puts(e.what());return 1;}}
