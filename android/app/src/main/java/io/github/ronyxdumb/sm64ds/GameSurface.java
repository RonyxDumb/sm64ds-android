package io.github.ronyxdumb.sm64ds;
import android.content.Context;
import android.opengl.GLSurfaceView;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;
final class GameSurface extends GLSurfaceView implements GLSurfaceView.Renderer {
    interface Failure {
        void report(String text);
    }
    interface Presentation {
        void gameplayControls(boolean visible);
    }
    private volatile Presentation presentation;
    private int lastPresentation = -1;
    void presentation(Presentation listener) {
        presentation = listener;
    }
    private final String rom;
    private final Failure failure;
    private long handle, lastFrame, lastHeartbeat;
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
        lastHeartbeat = 0;
        failed = false;
        CrashReport.note("GRAPHICS", "EGL context created; entering native renderer");
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
            if (handle == 0) {
                CrashReport.note("NATIVE", "Creating original game session");
                handle = NativeBridge.createRenderer(rom);
                CrashReport.note("NATIVE", "Original game session created");
            }
            long now = System.nanoTime();
            float dt = lastFrame == 0 ? 0 : (now - lastFrame) * 1e-9f;
            lastFrame = now;
            if (lastHeartbeat == 0 || now - lastHeartbeat > 5_000_000_000L) {
                lastHeartbeat = now;
                CrashReport.note("FRAME", "renderer alive, viewport=" + width + "x" + height +
                                 ", delta_ms=" + Math.round(dt * 1000));
            }
            final int keys, inputTx, inputTy;
            final float axisX, axisY;
            synchronized (this) {
                keys = buttons;
                inputTx = tx;
                inputTy = ty;
                axisX = x;
                axisY = y;
            }
            boolean gameplay = NativeBridge.drawRenderer(
                handle, width, height, dt, keys, axisX, axisY, inputTx, inputTy, audioEnabled);
            int next = gameplay ? 1 : 0;
            if (next != lastPresentation) {
                lastPresentation = next;
                post(() -> {
                    Presentation listener = presentation;
                    if (listener != null)
                        listener.gameplayControls(gameplay);
                });
            }
        } catch (Throwable e) {
            // Save the complete Java/JNI exception BEFORE cleanup can fail.
            CrashReport.failure("RENDER-ERROR", e);
            failed = true;
            if (handle != 0) {
                try {
                    NativeBridge.destroyRenderer(handle, true);
                } catch (Throwable cleanup) {
                    CrashReport.failure("RENDER-CLEANUP", cleanup);
                }
                handle = 0;
            }
            String detail = e.getMessage() == null ? e.toString() : e.getMessage();
            post(() -> failure.report(detail));
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
