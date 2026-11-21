// Meta Quest: the game on a large screen in the headset, through OpenXR.
//
// Built only for the Quest flavour (PORT_QUEST). The immersive activity starts a thread that owns the OpenXR session
// and an OpenGL ES context of its own (no window: the headset's compositor shows what is submitted to it). Each
// headset frame it draws the game's two screens with gl/present.c into one swapchain image, laid out side by side
// (the top screen in widescreen, the touch screen and a status line to its right), and submits it as a quad layer
// that floats in front of the player. The Touch controllers are the DS buttons; pointing a controller at the touch
// screen and pulling its trigger is the stylus, and on the status line it opens the settings panel.
#ifdef PORT_QUEST
#include "port.h"
#include "frame.h"
#include "present.h"
#include "guest_if.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <jni.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

void android_set_input(int buttons, bool touching, int x, int y); // jni.c

// DS buttons (KEYINPUT bits).
enum { K_A = 0x001, K_B = 0x002, K_SELECT = 0x004, K_START = 0x008, K_RIGHT = 0x010, K_LEFT = 0x020, K_UP = 0x040,
       K_DOWN = 0x080, K_R = 0x100, K_L = 0x200, K_X = 0x400, K_Y = 0x800 };

// The picture: canvas pixels, y down. The top screen 16:9, the touch screen 4:3 and the status line to its right.
#define CANVAS_W 2880
#define CANVAS_H 1080
static const int k_layout[8] = {0, 0, 1920, 1080, 1960, 130, 880, 660};
static const int k_status[4] = {1960, 830, 880, 120};
#define STATUS_W 880
#define STATUS_H 120
// The quad in the room (metres): 2.4 m away, the top screen straight ahead.
#define QUAD_W 4.0f
#define QUAD_H (QUAD_W * CANVAS_H / CANVAS_W)
#define QUAD_DISTANCE 2.4f
#define QUAD_X (QUAD_W * (CANVAS_W / 2 - 960) / CANVAS_W)
#define RENDER_SCALE 5 // 3D at 960 lines

static JavaVM *s_vm;
static jobject s_activity;
static pthread_t s_thread;
static volatile bool s_running, s_stop;
static volatile int s_state; // 0 not started, 1 starting, 2 showing, -1 failed
static char s_error[512];
static volatile int s_mic_toggles, s_settings_requests;

// The status line, drawn by the activity (ARGB pixels).
static pthread_mutex_t s_status_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t s_status_pixels[STATUS_W * STATUS_H];
static bool s_status_dirty;

// OpenGL: kept from one session to the next (the presenter's textures live in the context).
static EGLDisplay s_dpy = EGL_NO_DISPLAY;
static EGLConfig s_cfg;
static EGLContext s_ctx = EGL_NO_CONTEXT;
static EGLSurface s_pbuffer = EGL_NO_SURFACE;
static bool s_gl_ready;
static GLuint s_status_tex, s_status_fbo;

static void fail(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_error, sizeof(s_error), fmt, ap);
    va_end(ap);
    port_log("xr: %s", s_error);
    s_state = -1;
}

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ull + (uint64_t) ts.tv_nsec;
}

// ------------------------------------------------------------------------------------------------ OpenGL
static bool gl_start(void) {
    if (s_ctx == EGL_NO_CONTEXT) {
        s_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (s_dpy == EGL_NO_DISPLAY || !eglInitialize(s_dpy, NULL, NULL)) return false;
        const EGLint cfg_attr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                   EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
        EGLint n = 0;
        if (!eglChooseConfig(s_dpy, cfg_attr, &s_cfg, 1, &n) || n < 1) return false;
        const EGLint ctx_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        s_ctx = eglCreateContext(s_dpy, s_cfg, EGL_NO_CONTEXT, ctx_attr);
        const EGLint pb_attr[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
        s_pbuffer = eglCreatePbufferSurface(s_dpy, s_cfg, pb_attr);
        if (s_ctx == EGL_NO_CONTEXT || s_pbuffer == EGL_NO_SURFACE) return false;
    }
    if (!eglMakeCurrent(s_dpy, s_pbuffer, s_pbuffer, s_ctx)) return false;
    if (!s_gl_ready) {
        s_gl_ready = true;
        present_init();
        glGenTextures(1, &s_status_tex);
        glBindTexture(GL_TEXTURE_2D, s_status_tex);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, STATUS_W, STATUS_H);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glGenFramebuffers(1, &s_status_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, s_status_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_status_tex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        pthread_mutex_lock(&s_status_lock);
        s_status_dirty = true;
        pthread_mutex_unlock(&s_status_lock);
    }
    return true;
}

static void status_upload(void) {
    static uint8_t rgba[STATUS_W * STATUS_H * 4];
    pthread_mutex_lock(&s_status_lock);
    bool dirty = s_status_dirty;
    s_status_dirty = false;
    if (dirty) {
        for (int i = 0; i < STATUS_W * STATUS_H; i++) {
            uint32_t p = s_status_pixels[i];
            rgba[i * 4] = (uint8_t) (p >> 16), rgba[i * 4 + 1] = (uint8_t) (p >> 8), rgba[i * 4 + 2] = (uint8_t) p;
            rgba[i * 4 + 3] = 255;
        }
    }
    pthread_mutex_unlock(&s_status_lock);
    if (!dirty) return;
    glBindTexture(GL_TEXTURE_2D, s_status_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, STATUS_W, STATUS_H, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

// ------------------------------------------------------------------------------------------------ input
typedef struct Actions {
    XrActionSet set;
    XrAction a, b, x, y, menu, stick_click_l, stick_click_r;
    XrAction trigger_l, trigger_r, grip_l, grip_r;
    XrAction stick_l, stick_r;
    XrAction aim_l, aim_r;
    XrSpace aim_space[2];
} Actions;

static XrAction make_action(XrActionSet set, XrActionType type, const char *name, const char *label) {
    XrActionCreateInfo info = {XR_TYPE_ACTION_CREATE_INFO};
    info.actionType = type;
    snprintf(info.actionName, sizeof(info.actionName), "%s", name);
    snprintf(info.localizedActionName, sizeof(info.localizedActionName), "%s", label);
    XrAction action = XR_NULL_HANDLE;
    xrCreateAction(set, &info, &action);
    return action;
}

static bool actions_create(XrInstance instance, XrSession session, Actions *a) {
    XrActionSetCreateInfo set_info = {XR_TYPE_ACTION_SET_CREATE_INFO};
    snprintf(set_info.actionSetName, sizeof(set_info.actionSetName), "game");
    snprintf(set_info.localizedActionSetName, sizeof(set_info.localizedActionSetName), "Game");
    if (XR_FAILED(xrCreateActionSet(instance, &set_info, &a->set))) return false;
    a->a = make_action(a->set, XR_ACTION_TYPE_BOOLEAN_INPUT, "a", "A: accelerate");
    a->b = make_action(a->set, XR_ACTION_TYPE_BOOLEAN_INPUT, "b", "B: brake");
    a->x = make_action(a->set, XR_ACTION_TYPE_BOOLEAN_INPUT, "x", "X");
    a->y = make_action(a->set, XR_ACTION_TYPE_BOOLEAN_INPUT, "y", "Y");
    a->menu = make_action(a->set, XR_ACTION_TYPE_BOOLEAN_INPUT, "start", "START");
    a->stick_click_l = make_action(a->set, XR_ACTION_TYPE_BOOLEAN_INPUT, "mic", "Microphone on / off");
    a->stick_click_r = make_action(a->set, XR_ACTION_TYPE_BOOLEAN_INPUT, "select", "SELECT");
    a->trigger_l = make_action(a->set, XR_ACTION_TYPE_FLOAT_INPUT, "trigger_left", "L: item / stylus");
    a->trigger_r = make_action(a->set, XR_ACTION_TYPE_FLOAT_INPUT, "trigger_right", "R: drift / stylus");
    a->grip_l = make_action(a->set, XR_ACTION_TYPE_FLOAT_INPUT, "grip_left", "L: item");
    a->grip_r = make_action(a->set, XR_ACTION_TYPE_FLOAT_INPUT, "grip_right", "R: drift");
    a->stick_l = make_action(a->set, XR_ACTION_TYPE_VECTOR2F_INPUT, "stick_left", "Steer");
    a->stick_r = make_action(a->set, XR_ACTION_TYPE_VECTOR2F_INPUT, "stick_right", "Steer (right stick)");
    a->aim_l = make_action(a->set, XR_ACTION_TYPE_POSE_INPUT, "aim_left", "Left pointer");
    a->aim_r = make_action(a->set, XR_ACTION_TYPE_POSE_INPUT, "aim_right", "Right pointer");

    const struct {
        XrAction action;
        const char *path;
    } map[] = {
        {a->a, "/user/hand/right/input/a/click"},
        {a->b, "/user/hand/right/input/b/click"},
        {a->x, "/user/hand/left/input/x/click"},
        {a->y, "/user/hand/left/input/y/click"},
        {a->menu, "/user/hand/left/input/menu/click"},
        {a->stick_click_l, "/user/hand/left/input/thumbstick/click"},
        {a->stick_click_r, "/user/hand/right/input/thumbstick/click"},
        {a->trigger_l, "/user/hand/left/input/trigger/value"},
        {a->trigger_r, "/user/hand/right/input/trigger/value"},
        {a->grip_l, "/user/hand/left/input/squeeze/value"},
        {a->grip_r, "/user/hand/right/input/squeeze/value"},
        {a->stick_l, "/user/hand/left/input/thumbstick"},
        {a->stick_r, "/user/hand/right/input/thumbstick"},
        {a->aim_l, "/user/hand/left/input/aim/pose"},
        {a->aim_r, "/user/hand/right/input/aim/pose"},
    };
    enum { COUNT = sizeof(map) / sizeof(map[0]) };
    XrActionSuggestedBinding bindings[COUNT];
    for (int i = 0; i < COUNT; i++) {
        bindings[i].action = map[i].action;
        xrStringToPath(instance, map[i].path, &bindings[i].binding);
    }
    XrInteractionProfileSuggestedBinding suggested = {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    xrStringToPath(instance, "/interaction_profiles/oculus/touch_controller", &suggested.interactionProfile);
    suggested.suggestedBindings = bindings;
    suggested.countSuggestedBindings = COUNT;
    if (XR_FAILED(xrSuggestInteractionProfileBindings(instance, &suggested))) return false;

    XrSessionActionSetsAttachInfo attach = {XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1;
    attach.actionSets = &a->set;
    if (XR_FAILED(xrAttachSessionActionSets(session, &attach))) return false;

    const XrAction aims[2] = {a->aim_l, a->aim_r};
    for (int i = 0; i < 2; i++) {
        XrActionSpaceCreateInfo space = {XR_TYPE_ACTION_SPACE_CREATE_INFO};
        space.action = aims[i];
        space.poseInActionSpace.orientation.w = 1.0f;
        if (XR_FAILED(xrCreateActionSpace(session, &space, &a->aim_space[i]))) return false;
    }
    return true;
}

static bool get_bool(XrSession session, XrAction action) {
    XrActionStateGetInfo get = {XR_TYPE_ACTION_STATE_GET_INFO};
    get.action = action;
    XrActionStateBoolean state = {XR_TYPE_ACTION_STATE_BOOLEAN};
    return XR_SUCCEEDED(xrGetActionStateBoolean(session, &get, &state)) && state.isActive && state.currentState;
}

static float get_float(XrSession session, XrAction action) {
    XrActionStateGetInfo get = {XR_TYPE_ACTION_STATE_GET_INFO};
    get.action = action;
    XrActionStateFloat state = {XR_TYPE_ACTION_STATE_FLOAT};
    if (XR_FAILED(xrGetActionStateFloat(session, &get, &state)) || !state.isActive) return 0.0f;
    return state.currentState;
}

static XrVector2f get_stick(XrSession session, XrAction action) {
    XrActionStateGetInfo get = {XR_TYPE_ACTION_STATE_GET_INFO};
    get.action = action;
    XrActionStateVector2f state = {XR_TYPE_ACTION_STATE_VECTOR2F};
    XrVector2f zero = {0, 0};
    if (XR_FAILED(xrGetActionStateVector2f(session, &get, &state)) || !state.isActive) return zero;
    return state.currentState;
}

// Where the controller's pointer meets the picture, in canvas pixels; false when it points elsewhere.
static bool pointer_on_canvas(XrSpace aim, XrSpace local, XrTime time, float *px, float *py) {
    XrSpaceLocation loc = {XR_TYPE_SPACE_LOCATION};
    if (XR_FAILED(xrLocateSpace(aim, local, time, &loc))) return false;
    const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    if ((loc.locationFlags & need) != need) return false;
    // The pointer looks along -z of the aim pose.
    XrQuaternionf q = loc.pose.orientation;
    float dx = -2.0f * (q.x * q.z + q.w * q.y);
    float dy = -2.0f * (q.y * q.z - q.w * q.x);
    float dz = -(1.0f - 2.0f * (q.x * q.x + q.y * q.y));
    if (dz > -0.05f) return false; // not towards the picture
    float t = (-QUAD_DISTANCE - loc.pose.position.z) / dz;
    if (t <= 0) return false;
    float hx = loc.pose.position.x + dx * t, hy = loc.pose.position.y + dy * t;
    float u = (hx - (QUAD_X - QUAD_W / 2)) / QUAD_W, v = ((QUAD_H / 2) - hy) / QUAD_H;
    if (u < 0 || u > 1 || v < 0 || v > 1) return false;
    *px = u * CANVAS_W;
    *py = v * CANVAS_H;
    return true;
}

// Reads the controllers and gives the game its buttons and stylus. cursor: where a pointer is on the picture.
static void input_update(XrSession session, const Actions *a, XrSpace local, XrTime time, bool focused,
                         int *cursor_count, float cursor[2][2]) {
    *cursor_count = 0;
    if (!focused) {
        android_set_input(0, false, 0, 0);
        return;
    }
    XrActiveActionSet active = {a->set, XR_NULL_PATH};
    XrActionsSyncInfo sync = {XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    if (xrSyncActions(session, &sync) != XR_SUCCESS) {
        android_set_input(0, false, 0, 0);
        return;
    }
    int buttons = 0;
    if (get_bool(session, a->a)) buttons |= K_A;
    if (get_bool(session, a->b)) buttons |= K_B;
    if (get_bool(session, a->x)) buttons |= K_X;
    if (get_bool(session, a->y)) buttons |= K_Y;
    if (get_bool(session, a->menu)) buttons |= K_START;
    if (get_bool(session, a->stick_click_r)) buttons |= K_SELECT;
    if (get_float(session, a->grip_l) > 0.6f) buttons |= K_L;
    if (get_float(session, a->grip_r) > 0.6f) buttons |= K_R;
    const XrAction sticks[2] = {a->stick_l, a->stick_r};
    for (int i = 0; i < 2; i++) {
        XrVector2f s = get_stick(session, sticks[i]);
        if (s.x < -0.4f) buttons |= K_LEFT;
        if (s.x > 0.4f) buttons |= K_RIGHT;
        if (s.y > 0.5f) buttons |= K_UP;
        if (s.y < -0.5f) buttons |= K_DOWN;
    }
    static bool s_mic_was;
    bool mic = get_bool(session, a->stick_click_l);
    if (mic && !s_mic_was) s_mic_toggles++;
    s_mic_was = mic;

    // A trigger is L / R, unless its controller points at the touch screen: then it is the stylus. On the status
    // line it asks for the settings panel.
    static bool s_pulled_was[2];
    bool touching = false;
    int touch_x = 0, touch_y = 0;
    const XrAction triggers[2] = {a->trigger_l, a->trigger_r};
    const int trigger_key[2] = {K_L, K_R};
    for (int hand = 0; hand < 2; hand++) {
        bool pulled = get_float(session, triggers[hand]) > 0.5f;
        float px, py;
        bool on_canvas = pointer_on_canvas(a->aim_space[hand], local, time, &px, &py);
        const int *r = &k_layout[4];
        bool on_touch = on_canvas && px >= r[0] && px < r[0] + r[2] && py >= r[1] && py < r[1] + r[3];
        const int *st = k_status;
        bool on_status = on_canvas && px >= st[0] && px < st[0] + st[2] && py >= st[1] && py < st[1] + st[3];
        if (on_status) {
            cursor[*cursor_count][0] = px;
            cursor[*cursor_count][1] = py;
            (*cursor_count)++;
            if (pulled && !s_pulled_was[hand]) s_settings_requests++;
        } else if (on_touch) {
            cursor[*cursor_count][0] = px;
            cursor[*cursor_count][1] = py;
            (*cursor_count)++;
            if (pulled && !touching) {
                touching = true;
                touch_x = (int) ((px - r[0]) * 256 / r[2]);
                touch_y = (int) ((py - r[1]) * 192 / r[3]);
            }
        } else if (pulled) {
            buttons |= trigger_key[hand];
        }
        s_pulled_was[hand] = pulled;
    }
    android_set_input(buttons, touching, touch_x, touch_y);
}

// ------------------------------------------------------------------------------------------------ the session
static void *xr_thread(void *arg) {
    (void) arg;
    JNIEnv *env = NULL;
    (*s_vm)->AttachCurrentThread(s_vm, &env, NULL);
    XrInstance instance = XR_NULL_HANDLE;
    XrSession session = XR_NULL_HANDLE;
    XrSpace local = XR_NULL_HANDLE;
    XrSwapchain swapchain = XR_NULL_HANDLE;
    GLuint fbos[8] = {0};
    uint32_t image_count = 0;
    Actions actions;
    memset(&actions, 0, sizeof(actions));

    port_log("xr: starting");
    // The loader needs the Java VM and the activity before anything else.
    PFN_xrInitializeLoaderKHR init_loader = NULL;
    xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction *) &init_loader);
    if (!init_loader) {
        fail("The OpenXR loader cannot be initialised.");
        goto done;
    }
    XrLoaderInitInfoAndroidKHR loader_info = {XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    loader_info.applicationVM = s_vm;
    loader_info.applicationContext = s_activity;
    XrResult r = init_loader((const XrLoaderInitInfoBaseHeaderKHR *) &loader_info);
    if (XR_FAILED(r)) {
        fail("The OpenXR loader failed to start (%d).", (int) r);
        goto done;
    }

    port_log("xr: loader ready");
    const char *extensions[] = {XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME, XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME};
    XrInstanceCreateInfoAndroidKHR android_info = {XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    android_info.applicationVM = s_vm;
    android_info.applicationActivity = s_activity;
    XrInstanceCreateInfo instance_info = {XR_TYPE_INSTANCE_CREATE_INFO};
    instance_info.next = &android_info;
    snprintf(instance_info.applicationInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE, "Mario Kart DS");
    instance_info.applicationInfo.applicationVersion = 1;
    instance_info.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    instance_info.enabledExtensionCount = 2;
    instance_info.enabledExtensionNames = extensions;
    r = xrCreateInstance(&instance_info, &instance);
    if (XR_FAILED(r)) {
        fail("OpenXR is not available (xrCreateInstance %d).", (int) r);
        goto done;
    }
    port_log("xr: instance created");
    XrSystemGetInfo system_info = {XR_TYPE_SYSTEM_GET_INFO};
    system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    r = xrGetSystem(instance, &system_info, &system);
    if (XR_FAILED(r)) {
        fail("No headset (xrGetSystem %d).", (int) r);
        goto done;
    }
    XrGraphicsRequirementsOpenGLESKHR requirements = {XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
    PFN_xrGetOpenGLESGraphicsRequirementsKHR get_requirements = NULL;
    xrGetInstanceProcAddr(instance, "xrGetOpenGLESGraphicsRequirementsKHR", (PFN_xrVoidFunction *) &get_requirements);
    if (get_requirements) get_requirements(instance, system, &requirements);

    port_log("xr: system found, starting OpenGL");
    if (!gl_start()) {
        fail("OpenGL ES 3 could not be started (EGL error 0x%x).", (unsigned) eglGetError());
        goto done;
    }
    port_log("xr: OpenGL ready (%s)", (const char *) glGetString(GL_RENDERER));
    XrGraphicsBindingOpenGLESAndroidKHR binding = {XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    binding.display = s_dpy;
    binding.config = s_cfg;
    binding.context = s_ctx;
    XrSessionCreateInfo session_info = {XR_TYPE_SESSION_CREATE_INFO};
    session_info.next = &binding;
    session_info.systemId = system;
    r = xrCreateSession(instance, &session_info, &session);
    if (XR_FAILED(r)) {
        fail("The headset session could not be created (%d).", (int) r);
        goto done;
    }
    port_log("xr: session object created");
    XrReferenceSpaceCreateInfo space_info = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    space_info.poseInReferenceSpace.orientation.w = 1.0f;
    r = xrCreateReferenceSpace(session, &space_info, &local);
    if (XR_FAILED(r)) {
        fail("No reference space (%d).", (int) r);
        goto done;
    }
    port_log("xr: reference space created");
    if (!actions_create(instance, session, &actions)) {
        fail("The controllers could not be set up.");
        goto done;
    }

    port_log("xr: controllers set up");
    // One swapchain for the whole picture. sRGB when there is one: the game's colours are sRGB values, written as
    // they are (the conversion on writing is turned off below).
    // The formats the headset offers, in its order of preference (asked for twice: how many, then which).
    static int64_t formats[512];
    uint32_t format_count = 0;
    xrEnumerateSwapchainFormats(session, 0, &format_count, NULL);
    if (format_count > 512) format_count = 512;
    r = xrEnumerateSwapchainFormats(session, format_count, &format_count, formats);
    if (XR_FAILED(r)) format_count = 0;
    port_log("xr: %u picture formats (%d), first 0x%x", format_count, (int) r,
             format_count ? (unsigned) formats[0] : 0);
    int64_t format = 0;
    const int64_t wanted[] = {GL_SRGB8_ALPHA8, GL_RGBA8, GL_RGB10_A2, GL_RGBA16F};
    for (int w = 0; w < 4 && !format; w++) {
        for (uint32_t i = 0; i < format_count; i++) {
            if (formats[i] == wanted[w]) format = formats[i];
        }
    }
    // None of the known ones listed: the one the headset prefers, or sRGB unasked when it lists nothing.
    if (!format) format = format_count ? formats[0] : GL_SRGB8_ALPHA8;
    XrSwapchainCreateInfo swapchain_info = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    swapchain_info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    swapchain_info.format = format;
    swapchain_info.sampleCount = 1;
    swapchain_info.width = CANVAS_W;
    swapchain_info.height = CANVAS_H;
    swapchain_info.faceCount = 1;
    swapchain_info.arraySize = 1;
    swapchain_info.mipCount = 1;
    r = xrCreateSwapchain(session, &swapchain_info, &swapchain);
    if (XR_FAILED(r)) {
        fail("The picture for the headset could not be created (%d).", (int) r);
        goto done;
    }
    port_log("xr: swapchain created");
    XrSwapchainImageOpenGLESKHR images[8];
    for (int i = 0; i < 8; i++) {
        images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
        images[i].next = NULL;
    }
    xrEnumerateSwapchainImages(swapchain, 0, &image_count, NULL);
    if (image_count < 1 || image_count > 8 ||
        XR_FAILED(xrEnumerateSwapchainImages(swapchain, image_count, &image_count,
                                             (XrSwapchainImageBaseHeader *) images))) {
        fail("The picture for the headset has no images.");
        goto done;
    }
    glGenFramebuffers((GLsizei) image_count, fbos);
    for (uint32_t i = 0; i < image_count; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbos[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, images[i].image, 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    present_set_wide(16.0f / 9.0f);
    guest_set_aspect((int32_t) (16.0f / 9.0f * 4096.0f + 0.5f));
    port_log("xr: session created, %u images, format 0x%x", image_count, (unsigned) format);
    s_state = 2;

    bool session_running = false, focused = false, quit = false;
    uint64_t snap_time = 0;
    const double game_period = 1.0 / 59.8261;
    while (!quit) {
        // Events: the session's life.
        XrEventDataBuffer event = {XR_TYPE_EVENT_DATA_BUFFER};
        while (xrPollEvent(instance, &event) == XR_SUCCESS) {
            if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                const XrEventDataSessionStateChanged *changed = (const XrEventDataSessionStateChanged *) &event;
                focused = changed->state == XR_SESSION_STATE_FOCUSED;
                port_log("xr: session state %d", (int) changed->state);
                if (changed->state == XR_SESSION_STATE_READY) {
                    XrSessionBeginInfo begin = {XR_TYPE_SESSION_BEGIN_INFO};
                    begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    session_running = XR_SUCCEEDED(xrBeginSession(session, &begin));
                } else if (changed->state == XR_SESSION_STATE_STOPPING) {
                    xrEndSession(session);
                    session_running = false;
                } else if (changed->state == XR_SESSION_STATE_EXITING ||
                           changed->state == XR_SESSION_STATE_LOSS_PENDING) {
                    quit = true;
                }
            } else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
                quit = true;
            }
            event.type = XR_TYPE_EVENT_DATA_BUFFER;
            event.next = NULL;
        }
        if (quit) break;
        if (s_stop) {
            // The activity is going away: leave the session in order (STOPPING and EXITING follow), or at once.
            if (!session_running || XR_FAILED(xrRequestExitSession(session))) break;
            s_stop = false;
        }
        if (!session_running) {
            usleep(20000);
            continue;
        }

        XrFrameState frame_state = {XR_TYPE_FRAME_STATE};
        XrFrameWaitInfo wait_info = {XR_TYPE_FRAME_WAIT_INFO};
        if (XR_FAILED(xrWaitFrame(session, &wait_info, &frame_state))) {
            usleep(10000);
            continue;
        }
        XrFrameBeginInfo begin_info = {XR_TYPE_FRAME_BEGIN_INFO};
        xrBeginFrame(session, &begin_info);

        int cursor_count = 0;
        float cursor[2][2];
        input_update(session, &actions, local, frame_state.predictedDisplayTime, focused, &cursor_count, cursor);

        XrCompositionLayerQuad quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
        const XrCompositionLayerBaseHeader *layers[1] = {(const XrCompositionLayerBaseHeader *) &quad};
        uint32_t layer_count = 0;
        if (frame_state.shouldRender) {
            uint32_t index = 0;
            XrSwapchainImageAcquireInfo acquire = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            XrSwapchainImageWaitInfo wait_image = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            wait_image.timeout = XR_INFINITE_DURATION;
            if (XR_SUCCEEDED(xrAcquireSwapchainImage(swapchain, &acquire, &index)) &&
                XR_SUCCEEDED(xrWaitSwapchainImage(swapchain, &wait_image)) && index < image_count) {
                const FrameSnapshot *f = guest_frame_acquire();
                if (f) {
                    present_update(f, RENDER_SCALE);
                    snap_time = f->time_ns;
                }
                // Above 60 Hz the 3D is drawn between the last two game frames (as android/render.c does).
                float t = 1.0f;
                if (frame_state.predictedDisplayPeriod > 0 && frame_state.predictedDisplayPeriod < 15000000 &&
                    snap_time) {
                    t = (float) ((double) (int64_t) (now_ns() - snap_time) / 1e9 / game_period);
                    if (t < 0) t = 0;
                    if (t > 1) t = 1;
                }
                glDisable(0x8DB9); // GL_FRAMEBUFFER_SRGB_EXT: store the game's colour values unchanged
                while (glGetError() != GL_NO_ERROR) {
                }
                static int s_frames_logged;
                if (s_frames_logged < 3) port_log("xr: drawing frame %d (image %u)", s_frames_logged, index);
                present_set_target(fbos[index]);
                present_draw(k_layout, CANVAS_W, CANVAS_H, t);
                if (s_frames_logged < 3) port_log("xr: frame %d drawn, GL error 0x%x", s_frames_logged++, (unsigned) glGetError());
                // The status line under the touch screen (its rows run top down: flipped on the way).
                status_upload();
                glBindFramebuffer(GL_READ_FRAMEBUFFER, s_status_fbo);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbos[index]);
                glBlitFramebuffer(0, 0, STATUS_W, STATUS_H, k_status[0], CANVAS_H - k_status[1],
                                  k_status[0] + k_status[2], CANVAS_H - (k_status[1] + k_status[3]),
                                  GL_COLOR_BUFFER_BIT, GL_LINEAR);
                // Where the pointers are on the touch screen.
                glBindFramebuffer(GL_FRAMEBUFFER, fbos[index]);
                glEnable(GL_SCISSOR_TEST);
                for (int i = 0; i < cursor_count; i++) {
                    glScissor((int) cursor[i][0] - 7, CANVAS_H - (int) cursor[i][1] - 7, 14, 14);
                    glClearColor(0, 0, 0, 1);
                    glClear(GL_COLOR_BUFFER_BIT);
                    glScissor((int) cursor[i][0] - 5, CANVAS_H - (int) cursor[i][1] - 5, 10, 10);
                    glClearColor(1, 0.9f, 0.2f, 1);
                    glClear(GL_COLOR_BUFFER_BIT);
                }
                glDisable(GL_SCISSOR_TEST);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                XrSwapchainImageReleaseInfo release = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                xrReleaseSwapchainImage(swapchain, &release);

                quad.space = local;
                quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                quad.subImage.swapchain = swapchain;
                quad.subImage.imageRect.offset.x = 0;
                quad.subImage.imageRect.offset.y = 0;
                quad.subImage.imageRect.extent.width = CANVAS_W;
                quad.subImage.imageRect.extent.height = CANVAS_H;
                quad.subImage.imageArrayIndex = 0;
                quad.pose.orientation.w = 1.0f;
                quad.pose.position.x = QUAD_X;
                quad.pose.position.y = 0.0f;
                quad.pose.position.z = -QUAD_DISTANCE;
                quad.size.width = QUAD_W;
                quad.size.height = QUAD_H;
                layer_count = 1;
            }
        }
        XrFrameEndInfo end_info = {XR_TYPE_FRAME_END_INFO};
        end_info.displayTime = frame_state.predictedDisplayTime;
        end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        end_info.layerCount = layer_count;
        end_info.layers = layers;
        xrEndFrame(session, &end_info);
    }

done:
    port_log("xr: ending (state %d)", (int) s_state);
    android_set_input(0, false, 0, 0);
    if (image_count) glDeleteFramebuffers((GLsizei) image_count, fbos);
    present_set_target(0);
    for (int i = 0; i < 2; i++) {
        if (actions.aim_space[i] != XR_NULL_HANDLE) xrDestroySpace(actions.aim_space[i]);
    }
    if (swapchain != XR_NULL_HANDLE) xrDestroySwapchain(swapchain);
    if (local != XR_NULL_HANDLE) xrDestroySpace(local);
    if (session != XR_NULL_HANDLE) xrDestroySession(session);
    if (instance != XR_NULL_HANDLE) xrDestroyInstance(instance);
    if (s_dpy != EGL_NO_DISPLAY) eglMakeCurrent(s_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (s_state != -1) s_state = 0;
    s_running = false;
    (*s_vm)->DetachCurrentThread(s_vm);
    return NULL;
}

// ------------------------------------------------------------------------------------------------ Java
JNIEXPORT void JNICALL Java_com_mkds_port_Native_xrStart(JNIEnv *env, jclass cls, jobject activity) {
    (void) cls;
    if (s_running) return;
    if (s_thread) pthread_join(s_thread, NULL);
    (*env)->GetJavaVM(env, &s_vm);
    if (s_activity) (*env)->DeleteGlobalRef(env, s_activity);
    s_activity = (*env)->NewGlobalRef(env, activity);
    s_stop = false;
    s_error[0] = 0;
    s_state = 1;
    s_running = true;
    pthread_create(&s_thread, NULL, xr_thread, NULL);
}

JNIEXPORT void JNICALL Java_com_mkds_port_Native_xrStop(JNIEnv *env, jclass cls) {
    (void) env, (void) cls;
    if (s_running) s_stop = true;
}

// 0 not running, 1 starting, 2 showing the game, -1 failed (xrError says why).
JNIEXPORT jint JNICALL Java_com_mkds_port_Native_xrState(JNIEnv *env, jclass cls) {
    (void) env, (void) cls;
    return s_state;
}

JNIEXPORT jstring JNICALL Java_com_mkds_port_Native_xrError(JNIEnv *env, jclass cls) {
    (void) cls;
    return (*env)->NewStringUTF(env, s_error);
}

// How often the microphone button (left thumbstick click) has been pressed.
JNIEXPORT jint JNICALL Java_com_mkds_port_Native_xrMicToggles(JNIEnv *env, jclass cls) {
    (void) env, (void) cls;
    return s_mic_toggles;
}

// How often the settings were asked for (a trigger pulled on the status line).
JNIEXPORT jint JNICALL Java_com_mkds_port_Native_xrSettingsRequests(JNIEnv *env, jclass cls) {
    (void) env, (void) cls;
    return s_settings_requests;
}

// The status line: STATUS_W x STATUS_H ARGB pixels, rows top down.
JNIEXPORT void JNICALL Java_com_mkds_port_Native_xrSetStatus(JNIEnv *env, jclass cls, jintArray pixels) {
    (void) cls;
    if ((*env)->GetArrayLength(env, pixels) != STATUS_W * STATUS_H) return;
    pthread_mutex_lock(&s_status_lock);
    (*env)->GetIntArrayRegion(env, pixels, 0, STATUS_W * STATUS_H, (jint *) s_status_pixels);
    s_status_dirty = true;
    pthread_mutex_unlock(&s_status_lock);
}
#else
typedef int port_quest_unused; // not the Quest flavour
#endif
