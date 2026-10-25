import io.github.ronyxdumb.sm64ds.ControllerState;
public class ControllerStateTest {
    static void check(boolean ok) {
        if (!ok)
            throw new AssertionError();
    }
    public static void main(String[] args) {
        ControllerState s = new ControllerState();
        float aspect = 2;
        s.down(7, .16f, .70f, aspect);
        check(s.x == 0 && s.y == 0);
        s.move(7, .25f, .70f, aspect);
        check(s.x > .99f);
        s.down(43, .90f, .70f, aspect);
        check((s.buttons & ControllerState.A) != 0 && s.x > .99f);
        s.down(2, .88f, .16f, aspect);
        check((s.buttons & ControllerState.R) != 0);
        s.up(43, aspect);
        check((s.buttons & ControllerState.A) == 0 && (s.buttons & ControllerState.R) != 0 &&
              s.x > .99f);
        s.move(7, .16f, .70f, aspect);
        check(s.x == 0);
        s.up(2, aspect);
        check(s.buttons == 0);
        s.up(7, aspect);
        check(s.x == 0);
        s.down(128, .5f, .5f, aspect);
        check(s.touchX == 128 && s.touchY == 96);
        s.clear();
        check(s.buttons == 0 && s.touchX == -1 && s.y == 0);
        s.down(4, .90f, .70f, aspect);
        s.move(4, .80f, .82f, aspect);
        check((s.buttons & ControllerState.A) == 0 && (s.buttons & ControllerState.B) != 0);
        s.clear();
        System.out.println("Controller multitouch tests passed");
    }
}
