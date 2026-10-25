package io.github.ronyxdumb.sm64ds;
import java.util.LinkedHashMap;
import java.util.Map;
/** Pure input model: pointer IDs survive Android's pointer-index reordering. */
public final class ControllerState {
    public static final int A = 1, B = 2, SELECT = 4, START = 8, RIGHT = 16, LEFT = 32, UP = 64,
                            DOWN = 128, R = 256, L = 512, X = 1024, Y = 2048;
    public static final int CAM_LEFT = 1 << 16, CAM_RIGHT = 1 << 17, CAM_UP = 1 << 18,
                            CAM_DOWN = 1 << 19;
    public static final int[] BITS = {A,     B,      X,        Y,         L,      R,
                                      START, SELECT, CAM_LEFT, CAM_RIGHT, CAM_UP, CAM_DOWN};
    public static final String[] LABELS = {"A",     "B",      "X", "Y", "L", "R",
                                           "START", "SELECT", "◀", "▶", "▲", "▼"};
    public static final float[] CX = {.90f, .80f, .80f, .70f, .12f, .88f,
                                      .56f, .44f, .63f, .75f, .69f, .69f};
    public static final float[] CY = {.70f, .82f, .58f, .70f, .16f, .16f,
                                      .89f, .89f, .29f, .29f, .18f, .40f};
    private static final class Pointer {
        float x, y;
        Pointer(float x, float y) {
            this.x = x;
            this.y = y;
        }
    }
    private final Map<Integer, Pointer> pointers = new LinkedHashMap<>();
    private int joystick = -1;
    private int hardwareButtons;
    private float hardwareX, hardwareY, lastAspect = 1;
    public int buttons, touchX = -1, touchY = -1;
    public float x, y;
    public float deadzone = .15f, sensitivity = 1f;
    public void hardware(int keys, float axisX, float axisY) {
        hardwareButtons = keys;
        hardwareX = Float.isFinite(axisX) ? Math.max(-1, Math.min(1, axisX)) : 0;
        hardwareY = Float.isFinite(axisY) ? Math.max(-1, Math.min(1, axisY)) : 0;
        rebuild(lastAspect);
    }
    public void down(int id, float px, float py, float aspect) {
        pointers.put(id, new Pointer(px, py));
        if (joystick < 0 && Math.hypot((px - .16f) * aspect, py - .70f) < .23f)
            joystick = id;
        rebuild(aspect);
    }
    public void move(int id, float px, float py, float aspect) {
        Pointer p = pointers.get(id);
        if (p != null) {
            p.x = px;
            p.y = py;
            rebuild(aspect);
        }
    }
    public void up(int id, float aspect) {
        pointers.remove(id);
        if (joystick == id)
            joystick = -1;
        rebuild(aspect);
    }
    public void clear() {
        hardwareButtons = 0;
        hardwareX = hardwareY = 0;
        cancelTouch();
    }
    public void cancelTouch() {
        pointers.clear();
        joystick = -1;
        rebuild(lastAspect);
    }
    private void rebuild(float aspect) {
        lastAspect = aspect;
        buttons = 0;
        x = y = 0;
        touchX = touchY = -1;
        for (Map.Entry<Integer, Pointer> entry : pointers.entrySet()) {
            Pointer p = entry.getValue();
            if (entry.getKey() == joystick) {
                float dx = (p.x - .16f) * aspect / .18f, dy = -(p.y - .70f) / .18f;
                float length = (float)Math.hypot(dx, dy);
                if (length > deadzone) {
                    float magnitude =
                        Math.min(1, (length - deadzone) / (1 - deadzone) * sensitivity);
                    x = dx / length * magnitude;
                    y = dy / length * magnitude;
                }
            } else {
                boolean button = false;
                for (int i = 0; i < BITS.length; i++)
                    if (Math.hypot((p.x - CX[i]) * aspect, p.y - CY[i]) < .067f) {
                        buttons |= BITS[i];
                        button = true;
                    }
                if (!button && touchX < 0 && p.x >= 0 && p.x <= 1 && p.y >= 0 && p.y <= 1) {
                    touchX = Math.min(255, (int)(p.x * 256));
                    touchY = Math.min(191, (int)(p.y * 192));
                }
            }
        }
        buttons |= hardwareButtons;
        if (Math.hypot(hardwareX, hardwareY) > Math.hypot(x, y)) {
            x = hardwareX;
            y = hardwareY;
        }
        if (x > .35f)
            buttons |= RIGHT;
        if (x < -.35f)
            buttons |= LEFT;
        if (y > .35f)
            buttons |= UP;
        if (y < -.35f)
            buttons |= DOWN;
    }
}
