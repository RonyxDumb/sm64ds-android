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
    void showControls(boolean show) {
        if (state.showControls() != show) {
            state.showControls(show);
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
    private void disk(Canvas canvas, float x, float y, float radius, int fill, int outline) {
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(fill);
        canvas.drawCircle(x, y, radius, paint);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(Math.max(1f, radius * .035f));
        paint.setColor(outline);
        canvas.drawCircle(x, y, radius, paint);
        paint.setStyle(Paint.Style.FILL);
    }
    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (!state.showControls())
            return;
        float w = areaWidth(), h = areaHeight();
        canvas.save();
        canvas.translate(safe.left, safe.top);
        if (state.gameplayControls()) {
            float baseX = w * .16f, baseY = h * .70f;
            disk(canvas, baseX, baseY, h * .18f, 0x66202c3e, 0x888aa7c4);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(Math.max(1f, h * .002f));
            paint.setColor(0x668aa7c4);
            canvas.drawCircle(baseX, baseY, h * .105f, paint);
            paint.setStyle(Paint.Style.FILL);
            float knobX = baseX + state.x * h * .14f;
            float knobY = baseY - state.y * h * .14f;
            if (Math.abs(state.x) + Math.abs(state.y) > .01f) {
                paint.setColor(0xaa38bdf8);
                paint.setStrokeWidth(Math.max(2f, h * .009f));
                canvas.drawLine(baseX, baseY, knobX, knobY, paint);
            }
            disk(canvas, knobX, knobY, h * .065f, 0xbb435a70, 0xff38bdf8);
            disk(canvas, knobX, knobY, h * .018f, 0xffc6ecfc, 0x8838bdf8);
        }
        for (int i = 0; i < ControllerState.BITS.length; i++) {
            if (!state.visibleButton(i))
                continue;
            final int tint;
            switch (i) {
            case 0: tint = 0xff38bdf8; break; // Jump
            case 1: tint = 0xfffbbf24; break;
            case 2: tint = 0xff34d399; break;
            case 3: tint = 0xffa78bfa; break;
            default: tint = 0xff94a3b8; break;
            }
            boolean pressed = (state.buttons & ControllerState.BITS[i]) != 0;
            float cx = w * state.buttonX(i), cy = h * state.buttonY(i);
            float radius = h * state.buttonRadius();
            disk(canvas, cx, cy, radius, pressed ? 0xbb426078 : 0x66313e50,
                 pressed ? 0xffffffff : tint);
            paint.setColor(pressed ? 0xffffffff : 0xffe2e8f0);
            paint.setStyle(Paint.Style.FILL);
            paint.setTextAlign(Paint.Align.CENTER);
            paint.setFakeBoldText(true);
            paint.setTextSize(h * (state.gameplayControls() ?
                (i == 6 || i == 7 ? .027f : .043f) : .018f));
            canvas.drawText(ControllerState.LABELS[i], cx,
                            cy + h * (state.gameplayControls() ? .014f : .006f), paint);
            paint.setFakeBoldText(false);
        }
        canvas.restore();
    }
}
