package io.github.ronyxdumb.sm64ds;

import android.app.Application;

/** Installs diagnostics before either launcher or GLES render thread starts. */
public final class CrashApplication extends Application {
    @Override public void onCreate() {
        super.onCreate();
        CrashReport.initialize(this);
    }
}
