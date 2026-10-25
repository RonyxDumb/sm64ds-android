package io.github.ronyxdumb.sm64ds;
import java.io.IOException;
final class NativeBridge {
    static {
        System.loadLibrary("sm64ds_android");
    }
    static native String probe(String path) throws IOException;
    static native long createRenderer(String path) throws IOException;
    static native void drawRenderer(long handle, int width, int height, float dt, int buttons,
                                    float x, float y, int touchX, int touchY, boolean audioEnabled);
    static native void destroyRenderer(long handle, boolean contextValid);
    private NativeBridge() {}
}
