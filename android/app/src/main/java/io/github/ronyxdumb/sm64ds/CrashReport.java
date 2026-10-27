package io.github.ronyxdumb.sm64ds;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Process;
import java.io.File;
import java.io.IOException;
import java.io.OutputStream;
import java.io.PrintWriter;
import java.io.StringWriter;

final class CrashReport {
    static final int EXPORT_REQUEST = 42;
    private static CrashLogStore store;

    static synchronized void initialize(Context context) {
        if (store != null) return;
        File directory = context.getExternalFilesDir(null);
        if (directory == null) directory = context.getFilesDir();
        store = new CrashLogStore(new File(new File(directory, "logs"), "sm64ds-crash-report.txt"));
        note("SESSION", "App startup; Android " + Build.VERSION.RELEASE +
             " (SDK " + Build.VERSION.SDK_INT + "), " +
             Build.MANUFACTURER + " " + Build.MODEL + ", ABI " +
             (Build.SUPPORTED_ABIS.length == 0 ? "unknown" : Build.SUPPORTED_ABIS[0]));
        try {
            if (!NativeBridge.configureCrashLog(store.file().getAbsolutePath()))
                note("NATIVE", "Failed to install native signal diagnostics.");
            else
                note("NATIVE", "Crash signal diagnostics installed.");
        } catch (Throwable error) {
            failure("NATIVE-INIT", error);
        }
        Thread.UncaughtExceptionHandler old = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((thread, error) -> {
            failure("UNCAUGHT thread=" + thread.getName(), error);
            if (old != null) old.uncaughtException(thread, error);
            else {
                Process.killProcess(Process.myPid());
                System.exit(10);
            }
        });
    }

    static synchronized void note(String kind, String text) {
        if (store != null) store.append(kind, text);
    }

    static void failure(String kind, Throwable error) {
        StringWriter buffer = new StringWriter();
        if (error != null) error.printStackTrace(new PrintWriter(buffer));
        note(kind, error == null ? "Unknown error" : buffer.toString());
    }

    static String preview() {
        if (store == null) return "Registro diagnostico non inizializzato.";
        try {
            return store.tail(16000);
        } catch (IOException e) {
            return "Impossibile leggere il log: " + e.getMessage();
        }
    }

    static void requestExport(Activity activity) {
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("text/plain");
        intent.putExtra(Intent.EXTRA_TITLE, "sm64ds-crash-report.txt");
        activity.startActivityForResult(intent, EXPORT_REQUEST);
    }

    static void exportTo(Context context, Uri uri) throws IOException {
        if (store == null) throw new IOException("Diagnostic logging has not been initialized.");
        try (OutputStream output = context.getContentResolver().openOutputStream(uri, "wt")) {
            if (output == null) throw new IOException("Cannot open the selected destination.");
            store.copyTo(output);
        }
    }

    private CrashReport() {}
}
