package io.github.ronyxdumb.sm64ds;
import android.app.Activity;
import android.app.AlertDialog;
import android.media.AudioManager;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.os.Bundle;
import android.view.View;
import android.view.WindowManager;
import android.widget.FrameLayout;
public final class RuntimeActivity extends Activity {
    private GameSurface surface;
    private ControlsView controls;
    private AudioManager audioManager;
    private AudioFocusRequest focusRequest;
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
                    .setOnCancelListener(d -> finish())
                    .show();
            });
        controls = new ControlsView(this, surface);
        FrameLayout root = new FrameLayout(this);
        root.addView(surface);
        root.addView(controls);
        setContentView(root);
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
        if (audioManager != null)
            surface.audioEnabled(audioManager.requestAudioFocus(focusRequest) ==
                                 AudioManager.AUDIOFOCUS_REQUEST_GRANTED);
        immersive();
    }
    @Override
    protected void onPause() {
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
        if (!focus && controls != null)
            controls.clear();
        if (focus)
            immersive();
    }
}
