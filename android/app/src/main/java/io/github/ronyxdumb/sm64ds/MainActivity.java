package io.github.ronyxdumb.sm64ds;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.system.Os;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.*;
import java.io.*;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class MainActivity extends Activity {
    private static final int PICK_ROM = 10;
    private static final ExecutorService worker = Executors.newSingleThreadExecutor();
    private TextView status;
    private ProgressBar progress;
    private Button select, verify, render;
    private volatile boolean closed;
    private File installed;
    private boolean romVerified;

    @Override
    public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
                             WindowManager.LayoutParams.FLAG_FULLSCREEN);
        getWindow().getDecorView().setSystemUiVisibility(
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN |
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        installed = new File(getFilesDir(), "game.nds");
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER_VERTICAL);
        int pad = (int)(32 * getResources().getDisplayMetrics().density);
        root.setPadding(pad, pad / 2, pad, pad / 2);
        root.setBackgroundColor(Color.BLACK);
        root.setOnApplyWindowInsetsListener((view, insets) -> {
            view.setPadding(pad + insets.getSystemWindowInsetLeft(),
                            pad / 2 + insets.getSystemWindowInsetTop(),
                            pad + insets.getSystemWindowInsetRight(),
                            pad / 2 + insets.getSystemWindowInsetBottom());
            return insets;
        });
        TextView title = new TextView(this);
        title.setText("Super Mario 64 DS Android");
        title.setTextSize(26);
        title.setTextColor(Color.WHITE);
        root.addView(title);
        TextView phase = new TextView(this);
        phase.setText("Native port • development build 0.4\nBy Francesco Pio Pipino");
        phase.setTextSize(14);
        phase.setTextColor(0xff8db5ff);
        root.addView(phase);
        status = new TextView(this);
        status.setTextSize(16);
        status.setTextColor(0xffcccccc);
        status.setPadding(0, 16, 0, 8);
        root.addView(status);
        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setIndeterminate(true);
        root.addView(progress);
        LinearLayout buttons = new LinearLayout(this);
        select = new Button(this);
        select.setText("Select ROM");
        select.setOnClickListener(v -> pickRom());
        buttons.addView(select);
        verify = new Button(this);
        verify.setText("Verifica ROM");
        verify.setOnClickListener(v -> checkInstalled());
        buttons.addView(verify);
        root.addView(buttons);
        render = new Button(this);
        render.setText("Avvia boot originale");
        render.setOnClickListener(v -> startActivity(new Intent(this, RuntimeActivity.class)));
        root.addView(render);
        LinearLayout diagnostics = new LinearLayout(this);
        Button inspect = new Button(this);
        inspect.setText("Console / Log");
        inspect.setOnClickListener(v -> new android.app.AlertDialog.Builder(this)
            .setTitle("Console diagnostica")
            .setMessage(CrashReport.preview())
            .setPositiveButton("Chiudi", null)
            .setNeutralButton("Esporta TXT", (dialog, which) -> CrashReport.requestExport(this))
            .show());
        diagnostics.addView(inspect);
        Button export = new Button(this);
        export.setText("Esporta log TXT");
        export.setOnClickListener(v -> CrashReport.requestExport(this));
        diagnostics.addView(export);
        root.addView(diagnostics);
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.addView(root);
        setContentView(scroll);
        root.requestApplyInsets();
        if (installed.isFile())
            checkInstalled();
        else {
            status.setText(
                "Select a European ASMP revision 0 .nds ROM or ZIP.\n\nIl boot originale è disponibile; titolo e gameplay sono ancora in sviluppo.");
            busy(false);
        }
    }
    private void pickRom() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                        Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(intent, PICK_ROM);
    }
    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == CrashReport.EXPORT_REQUEST) {
            if (result == RESULT_OK && data != null && data.getData() != null) {
                try {
                    CrashReport.exportTo(this, data.getData());
                    Toast.makeText(this, "Log TXT salvato", Toast.LENGTH_LONG).show();
                } catch (IOException e) {
                    CrashReport.failure("LOG-EXPORT", e);
                    Toast.makeText(this, "Esportazione fallita: " + e.getMessage(),
                                   Toast.LENGTH_LONG).show();
                }
            }
            return;
        }
        if (request != PICK_ROM || result != RESULT_OK || data == null || data.getData() == null)
            return;
        Uri uri = data.getData();
        // The imported private copy is authoritative, so revoked URI access is harmless.
        boolean retained = false;
        try {
            getContentResolver().takePersistableUriPermission(
                uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
            retained = true;
        } catch (SecurityException ignored) {
        }
        final boolean releasePermission = retained;
        busy(true);
        status.setText("Importing ROM…");
        worker.execute(() -> {
            File pending = new File(getFilesDir(), "import.pending");
            try {
                pending.delete();
                InputStream input = getContentResolver().openInputStream(uri);
                if (input == null)
                    throw new IOException("The document provider could not open this file.");
                RomImport.copy(
                    input, pending,
                    bytes -> ui(() -> status.setText("Importing: " + bytes / 1024 + " KiB")));
                ui(() -> status.setText("Validating filesystem and decompressing game assets…"));
                RomImport.verifySupportedRom(pending);
                String report = NativeBridge.probe(pending.getAbsolutePath());
                // POSIX rename on the same private volume replaces atomically. A failed
                // copy/validation leaves the previously working ROM untouched.
                Os.rename(pending.getAbsolutePath(), installed.getAbsolutePath());
                ui(() -> {
                    romVerified = true;
                    status.setText(report);
                    select.setText("Change ROM");
                });
            } catch (Exception | LinkageError e) {
                ui(()
                       -> status.setText("Import failed: " + e.getMessage() +
                                         (installed.isFile() ? "\nPrevious ROM preserved." : "")));
            } finally {
                pending.delete();
                if (releasePermission) {
                    try {
                        getContentResolver().releasePersistableUriPermission(
                            uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
                    } catch (SecurityException ignored) {
                    }
                }
                ui(() -> busy(false));
            }
        });
    }
    private void checkInstalled() {
        romVerified = false;
        busy(true);
        status.setText("Checking saved ROM…");
        worker.execute(() -> {
            try {
                new File(getFilesDir(), "import.pending").delete();
                RomImport.verifySupportedRom(installed);
                String report = NativeBridge.probe(installed.getAbsolutePath());
                ui(() -> {
                    romVerified = true;
                    status.setText(report);
                    select.setText("Change ROM");
                });
            } catch (Exception | LinkageError e) {
                ui(()
                       -> status.setText("Saved ROM is unavailable: " + e.getMessage() +
                                         "\nSelect the ROM again."));
            } finally {
                ui(() -> busy(false));
            }
        });
    }
    private void busy(boolean value) {
        progress.setVisibility(value ? View.VISIBLE : View.GONE);
        select.setEnabled(!value);
        verify.setEnabled(!value && installed.isFile());
        render.setEnabled(!value && installed.isFile() && romVerified);
    }
    private void ui(Runnable action) {
        runOnUiThread(() -> {
            if (!closed)
                action.run();
        });
    }
    @Override
    protected void onDestroy() {
        closed = true;
        super.onDestroy();
    }
}
