#include "runtime/game_input.h"
#include "PlayerInput.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
extern "C" uint16_t data_020a0e58[], data_020a0e5a[];
int main() {
    sm64ds::GameInput input;
    assert(&data_020a0e58[1] == data_020a0e5a);
    assert(gTouchHeld + 1 == gTouchEdge && gTouchHeld + 2 == gTouchX && gTouchHeld + 3 == gTouchY);
    input.update(1,0,0,255,191);
    assert(input.held()==1 && input.pressed()==1 && input.released()==0);
    assert(gTouchHeld[0]==1 && gTouchEdge[0]==1 && gTouchX[0]==255 && gTouchY[0]==191);
    input.update(1,0,0,42,31);
    assert(input.pressed()==0 && gTouchEdge[0]==0 && gTouchX[0]==42);
    input.update(2,0,0,-1,-1);
    assert(input.held()==2 && input.pressed()==2 && input.released()==1);
    assert(gTouchHeld[0]==0 && gTouchEdge[0]==1);
    input.update(0xf0,0,0,256,192);
    assert(input.held()==0 && gTouchHeld[0]==0); // original opposite-direction filter
    input.update(0,1,1,-1,-1);
    assert(input.held()==0x50 && input.pressed()==0x50);
    input.update(0,std::numeric_limits<float>::infinity(),NAN,-1,-1);
    assert(input.held()==0 && input.x()==0 && input.y()==0);
    for(unsigned slot=1;slot<4;++slot) assert(input.held(slot)==0 && gTouchHeld[slot*4]==0);
    input.clear();
    assert(input.held()==0 && input.pressed()==0 && input.released()==0 && !gTouchEdge[0]);
    std::puts("Recovered input: key edges, opposite directions, stylus byte lanes and reset passed.");
}
