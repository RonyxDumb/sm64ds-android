package io.github.ronyxdumb.sm64ds;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.view.MotionEvent;
import android.view.View;
final class ControlsView extends View {
    private final ControllerState state = new ControllerState();
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Rect safe = new Rect();
    private final GameSurface surface;
    ControlsView(Context context, GameSurface surface) {
        super(context);
        this.surface = surface;
        state.gameplayControls(false);
        setFocusable(true);
        setOnApplyWindowInsetsListener((v, insets) -> {
            safe.set(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                     insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
            updateGeometry();
            clear();
            return insets;
        });
    }
    private void updateGeometry() {
        state.surface(getWidth(), getHeight(), safe.left, safe.top, safe.right, safe.bottom);
    }
    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        updateGeometry();
        clear();
    }
    void gameplayControls(boolean visible) {
        if (state.gameplayControls() != visible) {
            state.gameplayControls(visible);
            publish();
        }
    }
    private float areaWidth() {
        return Math.max(1, getWidth() - safe.left - safe.right);
    }
    private float areaHeight() {
        return Math.max(1, getHeight() - safe.top - safe.bottom);
    }
    private float aspect() {
        return areaWidth() / areaHeight();
    }
    private void publish() {
        surface.input(state);
        invalidate();
    }
    void clear() {
        state.clear();
        publish();
    }
    void hardware(GamepadState pad) {
        state.hardware(pad.buttons, pad.x, pad.y);
        publish();
    }
    @Override
    public boolean onTouchEvent(MotionEvent event) {
        int action = event.getActionMasked(), index = event.getActionIndex();
        if (action == MotionEvent.ACTION_CANCEL) {
            state.cancelTouch();
            publish();
            return true;
        }
        float w = areaWidth(), h = areaHeight();
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN)
            state.down(event.getPointerId(index), (event.getX(index) - safe.left) / w,
                       (event.getY(index) - safe.top) / h, aspect());
        else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
            state.up(event.getPointerId(index), aspect());
            if (action == MotionEvent.ACTION_UP)
                performClick();
        } else if (action == MotionEvent.ACTION_MOVE)
            for (int i = 0; i < event.getPointerCount(); i++)
                state.move(event.getPointerId(i), (event.getX(i) - safe.left) / w,
                           (event.getY(i) - safe.top) / h, aspect());
        publish();
        return true;
    }
    @Override
    public boolean performClick() {
        super.performClick();
        return true;
    }
    @Override
    protected void onDraw(Canvas canvas) {
        float w = areaWidth(), h = areaHeight();
        canvas.save();
        canvas.translate(safe.left, safe.top);
        paint.setStyle(Paint.Style.FILL);
        if (state.gameplayControls()) {
            paint.setColor(0x55486484);
            canvas.drawCircle(w * .16f, h * .70f, h * .18f, paint);
            paint.setColor(0xaa9abfff);
            canvas.drawCircle(w * .16f + state.x * h * .14f,
                              h * .70f - state.y * h * .14f, h * .065f, paint);
        }
        for (int i = 0; i < ControllerState.BITS.length; i++) {
            if (!state.visibleButton(i))
                continue;
            paint.setColor((state.buttons & ControllerState.BITS[i]) != 0 ? 0xcc7cafff
                                                                          : 0x55486484);
            canvas.drawCircle(w * state.buttonX(i), h * state.buttonY(i),
                              h * state.buttonRadius(), paint);
            paint.setColor(0xffeeeeff);
            paint.setTextAlign(Paint.Align.CENTER);
            paint.setTextSize(h * (state.gameplayControls() ?
                (i == 6 || i == 7 ? .027f : .043f) : .018f));
            canvas.drawText(ControllerState.LABELS[i], w * state.buttonX(i),
                            h * state.buttonY(i) + h * (state.gameplayControls() ? .014f : .006f), paint);
        }
        canvas.restore();
    }
}
