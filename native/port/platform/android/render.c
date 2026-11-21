// Android presentation thread: an OpenGL ES 3 context on the activity's surface, drawing each new frame with
// gl/present.c at the display's refresh rate.
#include "port.h"
#include "frame.h"
#include "present.h"
#include "guest_if.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/native_window.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_cond = PTHREAD_COND_INITIALIZER;
static ANativeWindow *s_window; // requested window
static bool s_window_changed;
static int s_layout[8] = {0, 0, 256, 192, 256, 0, 256, 192};
static pthread_t s_thread;
static bool s_thread_running;

static void *render_thread(void *arg) {
    (void) arg;
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, NULL, NULL);
    const EGLint cfg_attr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                               EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
    EGLConfig cfg;
    EGLint n;
    eglChooseConfig(dpy, cfg_attr, &cfg, 1, &n);
    const EGLint ctx_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    EGLSurface surf = EGL_NO_SURFACE;
    ANativeWindow *window = NULL;
    bool gl_ready = false;
    uint64_t snap_time = 0, last_swap = 0;
    double swap_interval = 1.0 / 60; // measured display period (smoothed)
    const double game_period = 1.0 / 59.8261;

    for (;;) {
        pthread_mutex_lock(&s_lock);
        while (!s_window && !s_window_changed) {
            pthread_cond_wait(&s_cond, &s_lock);
        }
        if (s_window_changed) {
            s_window_changed = false;
            if (surf != EGL_NO_SURFACE) {
                eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
                eglDestroySurface(dpy, surf);
                surf = EGL_NO_SURFACE;
            }
            if (window) {
                ANativeWindow_release(window);
            }
            window = s_window;
            if (window) {
                ANativeWindow_acquire(window);
                surf = eglCreateWindowSurface(dpy, cfg, window, NULL);
                eglMakeCurrent(dpy, surf, surf, ctx);
                eglSwapInterval(dpy, 1);
                if (!gl_ready) {
                    gl_ready = true;
                    present_init();
                }
            }
        }
        bool have_window = window != NULL;
        int layout[8];
        memcpy(layout, s_layout, sizeof(layout));
        pthread_mutex_unlock(&s_lock);
        if (!have_window) {
            continue;
        }
        int w = ANativeWindow_getWidth(window), h = ANativeWindow_getHeight(window);
        const FrameSnapshot *f = guest_frame_acquire();
        if (f) {
            // Internal 3D resolution: enough for the larger on-screen rectangle.
            int rect_h = layout[3] > layout[7] ? layout[3] : layout[7];
            int scale = (rect_h + 191) / 192;
            if (scale < 1) scale = 1;
            if (scale > 6) scale = 6;
            present_update(f, scale);
            snap_time = f->time_ns;
        }
        // Above 60 Hz the 3D is drawn between the last two game frames, one game frame behind (the snapshot taken
        // at snap_time is reached one period later), so that motion is smooth at 90, 120 or 144 Hz.
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t now = (uint64_t) ts.tv_sec * 1000000000ull + (uint64_t) ts.tv_nsec;
        float t = 1.0f;
        if (swap_interval < 1.0 / 75 && snap_time) {
            double since = (double) (int64_t) (now - snap_time) / 1e9;
            t = (float) (since / game_period);
            if (t < 0) t = 0;
            if (t > 1) t = 1;
        }
        present_draw(layout, w, h, t);
        eglSwapBuffers(dpy, surf);
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t after = (uint64_t) ts.tv_sec * 1000000000ull + (uint64_t) ts.tv_nsec;
        if (last_swap) {
            double d = (double) (after - last_swap) / 1e9;
            if (d < 0.1) swap_interval = swap_interval * 0.95 + d * 0.05;
        }
        last_swap = after;
    }
    return NULL;
}

void android_render_set_window(ANativeWindow *window) {
    pthread_mutex_lock(&s_lock);
    if (s_window) {
        ANativeWindow_release(s_window);
    }
    s_window = window;
    s_window_changed = true;
    if (!s_thread_running) {
        s_thread_running = true;
        pthread_create(&s_thread, NULL, render_thread, NULL);
    }
    pthread_cond_signal(&s_cond);
    pthread_mutex_unlock(&s_lock);
    // A destroyed surface must not be used after surfaceDestroyed returns: wait for the render thread to let go.
    while (!window) {
        pthread_mutex_lock(&s_lock);
        bool pending = s_window_changed;
        pthread_mutex_unlock(&s_lock);
        if (!pending) break;
        usleep(1000);
    }
}

void android_render_set_layout(const int *rects) {
    pthread_mutex_lock(&s_lock);
    memcpy(s_layout, rects, sizeof(s_layout));
    pthread_mutex_unlock(&s_lock);
}
