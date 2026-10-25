package io.github.ronyxdumb.sm64ds;

import java.util.LinkedHashMap;
import java.util.Map;

/** Device state stays independent from touchscreen pointers and button holds. */
public final class GamepadState {
    private static final class Pad {
        int keys, axes;
        float x, y;
    }
    private final Map<Integer, Pad> devices = new LinkedHashMap<>();
    public int buttons;
    public float x, y;
    public void key(int device, int mask, boolean down) {
        Pad pad = devices.computeIfAbsent(device, id -> new Pad());
        if (down) pad.keys |= mask;
        else pad.keys &= ~mask;
        rebuild();
    }
    private static float finite(float value) {
        return Float.isFinite(value) ? Math.max(-1, Math.min(1, value)) : 0;
    }
    public void axes(int device, float leftX, float leftY, float rightX, float rightY,
                     float hatX, float hatY, float leftTrigger, float rightTrigger, float flat) {
        Pad pad = devices.computeIfAbsent(device, id -> new Pad());
        leftX = finite(leftX);
        leftY = finite(leftY);
        float length = (float)Math.hypot(leftX, leftY);
        float deadzone = Math.max(.12f, Math.min(.5f, finite(flat)));
        float amount = length > deadzone ? Math.min(1, (length - deadzone) / (1 - deadzone)) : 0;
        pad.x = length > 0 ? leftX / length * amount : 0;
        pad.y = length > 0 ? -leftY / length * amount : 0;
        pad.axes = 0;
        if (finite(hatX) > .5f) pad.axes |= ControllerState.RIGHT;
        if (finite(hatX) < -.5f) pad.axes |= ControllerState.LEFT;
        if (finite(hatY) < -.5f) pad.axes |= ControllerState.UP;
        if (finite(hatY) > .5f) pad.axes |= ControllerState.DOWN;
        if (finite(rightX) < -.4f) pad.axes |= ControllerState.CAM_LEFT;
        if (finite(rightX) > .4f) pad.axes |= ControllerState.CAM_RIGHT;
        if (finite(rightY) < -.4f) pad.axes |= ControllerState.CAM_UP;
        if (finite(rightY) > .4f) pad.axes |= ControllerState.CAM_DOWN;
        if (finite(leftTrigger) > .5f) pad.axes |= ControllerState.L;
        if (finite(rightTrigger) > .5f) pad.axes |= ControllerState.R;
        rebuild();
    }
    public void remove(int device) { devices.remove(device); rebuild(); }
    public void clear() { devices.clear(); rebuild(); }
    private void rebuild() {
        buttons = 0;
        x = y = 0;
        for (Pad pad : devices.values()) {
            buttons |= pad.keys | pad.axes;
            if (Math.hypot(pad.x, pad.y) > Math.hypot(x, y)) {
                x = pad.x;
                y = pad.y;
            }
        }
    }
}
