package io.github.ronyxdumb.sm64ds;
import android.app.Activity;
import android.app.AlertDialog;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.widget.LinearLayout;
import android.widget.Switch;
import android.widget.TextView;
import android.media.AudioManager;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.os.Bundle;
import android.view.View;
import android.view.WindowManager;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.hardware.input.InputManager;
import android.widget.FrameLayout;
public final class RuntimeActivity extends Activity implements InputManager.InputDeviceListener {
    private static final String UI_PREFS = "controller_ui";
    private static final String SHOW_CONTROLS = "show_controls";
    private GameSurface surface;
    private ControlsView controls;
    private AudioManager audioManager;
    private AudioFocusRequest focusRequest;
    private InputManager inputManager;
    private final GamepadState gamepads = new GamepadState();
    @Override
    public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        immersive();
        surface = new GameSurface(
            this, new java.io.File(getFilesDir(), "game.nds").getAbsolutePath(), text -> {
                if (isFinishing() || isDestroyed())
                    return;
                new AlertDialog.Builder(this)
                    .setTitle("Avvio nativo non riuscito")
                    .setMessage(text)
                    .setPositiveButton("Launcher", (d, w) -> finish())
                    .setNeutralButton("Esporta log TXT", (d, w) -> CrashReport.requestExport(this))
                    .setOnCancelListener(d -> finish())
                    .show();
            });
        controls = new ControlsView(this, surface);
        controls.showControls(getSharedPreferences(UI_PREFS, MODE_PRIVATE)
                                  .getBoolean(SHOW_CONTROLS, true));
        surface.presentation(controls::gameplayControls);
        FrameLayout root = new FrameLayout(this);
        root.addView(surface);
        root.addView(controls);
        TextView settings = new TextView(this);
        settings.setText("\u2699");
        settings.setTextSize(23);
        settings.setTextColor(0xffe2e8f0);
        settings.setGravity(Gravity.CENTER);
        settings.setContentDescription("Impostazioni controller");
        GradientDrawable background = new GradientDrawable();
        background.setColor(0xaa1d293b);
        background.setCornerRadius(dp(13));
        background.setStroke(dp(1), 0x885c8ca5);
        settings.setBackground(background);
        settings.setElevation(dp(5));
        FrameLayout.LayoutParams settingsPosition =
            new FrameLayout.LayoutParams(dp(42), dp(42), Gravity.TOP | Gravity.START);
        settingsPosition.leftMargin = dp(12);
        settingsPosition.topMargin = dp(12);
        root.addView(settings, settingsPosition);
        settings.setOnClickListener(v -> showControllerSettings());
        settings.setOnApplyWindowInsetsListener((v, insets) -> {
            FrameLayout.LayoutParams position = (FrameLayout.LayoutParams)v.getLayoutParams();
            position.leftMargin = insets.getSystemWindowInsetLeft() + dp(12);
            position.topMargin = insets.getSystemWindowInsetTop() + dp(12);
            v.setLayoutParams(position);
            return insets;
        });
        setContentView(root);
        settings.requestApplyInsets();
        inputManager = (InputManager)getSystemService(INPUT_SERVICE);
        controls.requestApplyInsets();
        audioManager = (AudioManager)getSystemService(AUDIO_SERVICE);
        focusRequest =
            new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                .setAudioAttributes(new AudioAttributes.Builder()
                                        .setUsage(AudioAttributes.USAGE_GAME)
                                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                                        .build())
                .setOnAudioFocusChangeListener(
                    change -> surface.audioEnabled(change == AudioManager.AUDIOFOCUS_GAIN))
                .build();
    }
    @Override
    protected void onActivityResult(int request, int result, android.content.Intent data) {
        super.onActivityResult(request, result, data);
        if (request != CrashReport.EXPORT_REQUEST) return;
        if (result == RESULT_OK && data != null && data.getData() != null) {
            try {
                CrashReport.exportTo(this, data.getData());
                android.widget.Toast.makeText(this, "Log TXT salvato",
                    android.widget.Toast.LENGTH_LONG).show();
            } catch (java.io.IOException error) {
                CrashReport.failure("LOG-EXPORT", error);
                android.widget.Toast.makeText(this, "Esportazione fallita: " + error.getMessage(),
                    android.widget.Toast.LENGTH_LONG).show();
            }
        }
    }
    private int dp(float value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
    private void showControllerSettings() {
        controls.clear();
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(22), dp(8), dp(22), dp(16));
        Switch show = new Switch(this);
        show.setChecked(getSharedPreferences(UI_PREFS, MODE_PRIVATE)
                            .getBoolean(SHOW_CONTROLS, true));
        show.setText(show.isChecked() ? "MOSTRA: ON" : "MOSTRA: OFF");
        show.setTextSize(17);
        show.setMinHeight(dp(56));
        panel.addView(show, new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, dp(56)));
        TextView description = new TextView(this);
        description.setText("Nasconde i pulsanti virtuali, non lo schermo touch DS. " +
                            "I controller Bluetooth e USB restano attivi.");
        description.setTextSize(13);
        panel.addView(description);
        show.setOnCheckedChangeListener((v, enabled) -> {
            controls.showControls(enabled);
            getSharedPreferences(UI_PREFS, MODE_PRIVATE).edit()
                .putBoolean(SHOW_CONTROLS, enabled).apply();
            show.setText(enabled ? "MOSTRA: ON" : "MOSTRA: OFF");
        });
        AlertDialog dialog = new AlertDialog.Builder(this)
            .setTitle("Controller visivo")
            .setView(panel)
            .setPositiveButton("Chiudi", null)
            .create();
        dialog.setOnDismissListener(d -> immersive());
        dialog.show();
    }
    private void immersive() {
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                                                         View.SYSTEM_UI_FLAG_FULLSCREEN |
                                                         View.SYSTEM_UI_FLAG_HIDE_NAVIGATION);
    }
    @Override
    protected void onResume() {
        super.onResume();
        if (surface != null)
            surface.onResume();
        if (inputManager != null)
            inputManager.registerInputDeviceListener(this, null);
        if (audioManager != null)
            surface.audioEnabled(audioManager.requestAudioFocus(focusRequest) ==
                                 AudioManager.AUDIOFOCUS_REQUEST_GRANTED);
        immersive();
    }
    @Override
    protected void onPause() {
        if (inputManager != null)
            inputManager.unregisterInputDeviceListener(this);
        gamepads.clear();
        controls.clear();
        surface.audioEnabled(false);
        surface.suspend();
        if (audioManager != null)
            audioManager.abandonAudioFocusRequest(focusRequest);
        super.onPause();
    }
    @Override
    public void onWindowFocusChanged(boolean focus) {
        super.onWindowFocusChanged(focus);
        if (!focus && controls != null) {
            gamepads.clear();
            controls.clear();
        }
        if (focus)
            immersive();
    }
    private static int button(int key) {
        switch (key) {
        case KeyEvent.KEYCODE_BUTTON_A: return ControllerState.A;
        case KeyEvent.KEYCODE_BUTTON_B: return ControllerState.B;
        case KeyEvent.KEYCODE_BUTTON_X: return ControllerState.X;
        case KeyEvent.KEYCODE_BUTTON_Y: return ControllerState.Y;
        case KeyEvent.KEYCODE_BUTTON_L1: return ControllerState.L;
        case KeyEvent.KEYCODE_BUTTON_R1: return ControllerState.R;
        case KeyEvent.KEYCODE_BUTTON_START: return ControllerState.START;
        case KeyEvent.KEYCODE_BUTTON_SELECT: return ControllerState.SELECT;
        case KeyEvent.KEYCODE_DPAD_LEFT: return ControllerState.LEFT;
        case KeyEvent.KEYCODE_DPAD_RIGHT: return ControllerState.RIGHT;
        case KeyEvent.KEYCODE_DPAD_UP: return ControllerState.UP;
        case KeyEvent.KEYCODE_DPAD_DOWN: return ControllerState.DOWN;
        default: return 0;
        }
    }
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        final int source = event.getSource();
        final boolean controller = (source & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD ||
            (source & InputDevice.SOURCE_DPAD) == InputDevice.SOURCE_DPAD ||
            (source & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
        final int mask = controller ? button(event.getKeyCode()) : 0;
        if (controls != null && mask != 0 &&
            (event.getAction() == KeyEvent.ACTION_DOWN || event.getAction() == KeyEvent.ACTION_UP)) {
            gamepads.key(event.getDeviceId(), mask, event.getAction() == KeyEvent.ACTION_DOWN);
            controls.hardware(gamepads);
            return true;
        }
        return super.dispatchKeyEvent(event);
    }
    @Override
    public boolean onGenericMotionEvent(MotionEvent event) {
        if (controls != null && event.isFromSource(InputDevice.SOURCE_JOYSTICK) &&
            event.getActionMasked() == MotionEvent.ACTION_MOVE) {
            InputDevice device = event.getDevice();
            InputDevice.MotionRange range = device == null ? null :
                device.getMotionRange(MotionEvent.AXIS_X, event.getSource());
            float rightX = event.getAxisValue(MotionEvent.AXIS_Z);
            float rightY = event.getAxisValue(MotionEvent.AXIS_RZ);
            if (device != null && device.getMotionRange(MotionEvent.AXIS_Z, event.getSource()) == null) {
                rightX = event.getAxisValue(MotionEvent.AXIS_RX);
                rightY = event.getAxisValue(MotionEvent.AXIS_RY);
            }
            gamepads.axes(event.getDeviceId(), event.getAxisValue(MotionEvent.AXIS_X),
                event.getAxisValue(MotionEvent.AXIS_Y), rightX, rightY,
                event.getAxisValue(MotionEvent.AXIS_HAT_X), event.getAxisValue(MotionEvent.AXIS_HAT_Y),
                Math.max(event.getAxisValue(MotionEvent.AXIS_LTRIGGER), event.getAxisValue(MotionEvent.AXIS_BRAKE)),
                Math.max(event.getAxisValue(MotionEvent.AXIS_RTRIGGER), event.getAxisValue(MotionEvent.AXIS_GAS)),
                range == null ? .15f : range.getFlat());
            controls.hardware(gamepads);
            return true;
        }
        return super.onGenericMotionEvent(event);
    }
    @Override public void onInputDeviceAdded(int deviceId) {}
    @Override public void onInputDeviceChanged(int deviceId) { onInputDeviceRemoved(deviceId); }
    @Override public void onInputDeviceRemoved(int deviceId) {
        gamepads.remove(deviceId);
        if (controls != null) controls.hardware(gamepads);
    }
}
