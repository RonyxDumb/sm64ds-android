import io.github.ronyxdumb.sm64ds.ControllerState;
import io.github.ronyxdumb.sm64ds.GamepadState;
public class GamepadStateTest {
    static void check(boolean condition) { if (!condition) throw new AssertionError(); }
    public static void main(String[] args) {
        GamepadState pads = new GamepadState();
        ControllerState touch = new ControllerState();
        pads.key(1,ControllerState.A,true);
        pads.axes(1,1,0,0,0,0,0,0,0,.15f);
        check((pads.buttons & ControllerState.A)!=0 && pads.x>.99f);
        touch.hardware(pads.buttons,pads.x,pads.y);
        touch.down(3,.80f,.82f,2);
        check((touch.buttons & 3)==3 && touch.x>.99f);
        touch.up(3,2);
        check((touch.buttons & ControllerState.A)!=0 && (touch.buttons & ControllerState.B)==0);
        pads.key(1,ControllerState.A,false);
        pads.axes(1,.04f,0,1,-1,-1,0,1,0,.15f);
        check(pads.x==0 && (pads.buttons & (ControllerState.CAM_RIGHT|ControllerState.CAM_UP|ControllerState.L|ControllerState.LEFT))!=0);
        touch.hardware(pads.buttons,pads.x,pads.y);
        touch.cancelTouch();
        check((touch.buttons & ControllerState.L)!=0);
        pads.key(2,ControllerState.B,true);
        pads.remove(1);
        check(pads.buttons==ControllerState.B && pads.x==0);
        pads.axes(2,Float.NaN,Float.POSITIVE_INFINITY,0,0,0,0,0,0,.2f);
        check(pads.x==0 && pads.y==0);
        pads.clear();
        touch.hardware(pads.buttons,pads.x,pads.y);
        check(touch.buttons==0 && touch.x==0);
        touch.hardware(1,1,1);
        touch.clear();
        check(touch.buttons==0 && touch.x==0 && touch.y==0);
        System.out.println("Gamepad/touch merge, deadzone, device removal and focus reset passed");
    }
}
