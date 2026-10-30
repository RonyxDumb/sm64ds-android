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
        // The game's physical bottom/right 4:3 viewport is the sole
        // touchscreen. The left screen and black letterbox must be inert.
        s.down(128, .25f, .5f, aspect);
        check(s.touchX == -1 && s.touchY == -1);
        s.move(128, .75f, .5f, aspect);
        check(s.touchX == 128 && s.touchY == 96);
        s.move(128, .51f, .125f, aspect);
        check(s.touchX >= 4 && s.touchX <= 6 && s.touchY == 0);
        s.move(128, .75f, .05f, aspect);
        check(s.touchX == -1 && s.touchY == -1);
        s.clear();
        check(s.buttons == 0 && s.touchX == -1 && s.y == 0);
        s.down(4, .90f, .70f, aspect);
        s.move(4, .80f, .82f, aspect);
        check((s.buttons & ControllerState.A) == 0 && (s.buttons & ControllerState.B) != 0);
        s.clear();
        // Intro/title menus expose the actual stylus surface. Hidden A,
        // camera and stick controls must not intercept a menu selection.
        s.gameplayControls(false);
        for (int i = 0; i < ControllerState.BITS.length; ++i)
            check(s.visibleButton(i) == (ControllerState.BITS[i] == ControllerState.START));
        s.down(1, .90f, .70f, aspect);
        check(s.buttons == 0 && s.touchX >= 203 && s.touchX <= 205 && s.touchY >= 146);
        s.up(1, aspect);
        s.down(2, .69f, .18f, aspect);
        check(s.buttons == 0 && s.touchX >= 96 && s.touchX <= 98 && s.touchY >= 13);
        s.up(2, aspect);
        s.down(3, .16f, .70f, aspect);
        s.move(3, .25f, .70f, aspect);
        check(s.x == 0 && s.y == 0 && s.buttons == 0 && s.touchX == -1);
        s.up(3, aspect);
        s.down(4, .5f, .95f, aspect);
        check(s.buttons == ControllerState.START && s.touchX == -1);
        s.up(4, aspect);

        // A landscape cutout and navigation inset affect control positions,
        // while the two GL viewports still occupy the full surface.
        s.surface(2401, 1081, 80, 0, 24, 32);
        float safeAspect = 2297f / 1049f;
        s.down(10, (1800.5f - 80) / 2297f, 540.5f / 1049f, safeAspect);
        check(s.buttons == 0 && s.touchX == 128 && s.touchY == 96);
        s.move(10, (1200.5f - 80) / 2297f, 90.5f / 1049f, safeAspect);
        check(s.touchX == 0 && s.touchY == 0);
        s.move(10, (2399.5f - 80) / 2297f, 989.5f / 1049f, safeAspect);
        check(s.touchX == 255 && s.touchY == 191);
        s.move(10, (1199f - 80) / 2297f, 540f / 1049f, safeAspect);
        check(s.touchX == -1 && s.touchY == -1);
        s.move(10, (1800f - 80) / 2297f, 89f / 1049f, safeAspect);
        check(s.touchX == -1 && s.touchY == -1);
        s.clear();
        s.surface(1536, 710, 0, 0, 0, 0);
        s.hardware(ControllerState.A, .8f, 0);
        s.gameplayControls(true);
        check((s.buttons & ControllerState.A) != 0 && s.x == .8f);
        s.clear();
        s.down(11, .90f, .70f, 1536f / 710f);
        check((s.buttons & ControllerState.A) != 0);
        s.gameplayControls(false);
        check(s.buttons == 0 && s.touchX == -1 && s.x == 0);
        System.out.println("Controller multitouch, menu routing and cutout viewport tests passed");
    }
}
