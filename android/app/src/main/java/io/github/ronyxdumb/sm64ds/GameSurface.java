package io.github.ronyxdumb.sm64ds;
import android.content.Context;
import android.opengl.GLSurfaceView;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;
final class GameSurface extends GLSurfaceView implements GLSurfaceView.Renderer {
    interface Failure {
        void report(String text);
    }
    private final String rom;
    private final Failure failure;
    private long handle, lastFrame;
    private int width, height, buttons, tx = -1, ty = -1;
    private float x, y;
    private boolean failed;
    private volatile boolean audioEnabled;
    void audioEnabled(boolean enabled) {
        audioEnabled = enabled;
    }
    GameSurface(Context context, String rom, Failure failure) {
        super(context);
        this.rom = rom;
        this.failure = failure;
        setEGLContextClientVersion(3);
        setEGLConfigChooser(8, 8, 8, 8, 24, 0);
        setPreserveEGLContextOnPause(false);
        setRenderer(this);
        setRenderMode(RENDERMODE_CONTINUOUSLY);
    }
    synchronized void input(ControllerState state) {
        buttons = state.buttons;
        x = state.x;
        y = state.y;
        tx = state.touchX;
        ty = state.touchY;
    }
    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        if (handle != 0)
            NativeBridge.destroyRenderer(handle, false);
        handle = 0;
        lastFrame = 0;
        failed = false;
    }
    @Override
    public void onSurfaceChanged(GL10 gl, int width, int height) {
        this.width = width;
        this.height = height;
    }
    @Override
    public void onDrawFrame(GL10 gl) {
        if (failed)
            return;
        try {
            if (handle == 0)
                handle = NativeBridge.createRenderer(rom);
            long now = System.nanoTime();
            float dt = lastFrame == 0 ? 0 : (now - lastFrame) * 1e-9f;
            lastFrame = now;
            final int keys, inputTx, inputTy;
            final float axisX, axisY;
            synchronized (this) {
                keys = buttons;
                inputTx = tx;
                inputTy = ty;
                axisX = x;
                axisY = y;
            }
            NativeBridge.drawRenderer(handle, width, height, dt, keys, axisX, axisY, inputTx,
                                      inputTy, audioEnabled);
        } catch (Exception | LinkageError e) {
            failed = true;
            if (handle != 0) {
                NativeBridge.destroyRenderer(handle, true);
                handle = 0;
            }
            post(() -> failure.report(e.getMessage()));
        }
    }
    void suspend() {
        queueEvent(() -> {
            if (handle != 0) {
                NativeBridge.destroyRenderer(handle, true);
                handle = 0;
            }
            lastFrame = 0;
        });
        onPause();
    }
}
