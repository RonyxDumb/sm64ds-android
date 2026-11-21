// Shared presentation on OpenGL ES 3 (Android and the host test build): turns frame snapshots into the two DS
// screens and draws them into rectangles of the current framebuffer.
//
// For each new frame snapshot: the 3D layer is rasterised on the GPU at a resolution that matches the screen
// (gl3d.c), the 2D engines are drawn in software at the native 256x192 and split into what is above and below the
// 3D layer, and a shader composites them with the colour effects and the master brightness. Frames with a display
// capture (dual-screen 3D, menu blur) are drawn entirely in software, because the capture writes into VRAM that the
// next frames display (the displayed frame itself still uses the GPU path when it has 3D).
//
// Dual-screen 3D (the race intros of the Grand Prix): the game draws its 3D on the two screens in turn, swapping the
// screens every frame (POWCNT bit 15) and showing on the other one the capture of the frame before. Here each
// screen is drawn from its own last frame instead (the GPU path, high resolution, widescreen on the top screen), and
// its 3D is interpolated between that screen's frames, two VBlanks apart.
#include "port.h"
#include "frame.h"
#include "gpu2d.h"
#include "gl3d.h"
#include "present.h"

#include <GLES3/gl3.h>
#include <string.h>

static const char *VS = "#version 300 es\n"
                        "layout(location = 0) in vec2 pos;\n"
                        "layout(location = 1) in vec2 uv;\n"
                        "out vec2 v_uv;\n"
                        "void main() { v_uv = uv; gl_Position = vec4(pos, 0.0, 1.0); }\n";
// A plain 2D screen.
static const char *FS_PLAIN = "#version 300 es\n"
                              "precision mediump float;\n"
                              "uniform sampler2D tex;\n"
                              "in vec2 v_uv;\n"
                              "out vec4 color;\n"
                              "void main() { color = vec4(texture(tex, v_uv).rgb, 1.0); }\n";
// Engine A with the 3D layer: below / 3D / above, then the master brightness.
static const char *FS_COMPOSE =
    "#version 300 es\n"
    "precision highp float;\n"
    "uniform sampler2D above;\n"
    "uniform sampler2D below;\n"
    "uniform sampler2D layer3d;\n"
    "uniform int bright_mode;\n"
    "uniform float bright;\n"
    // The screen is k2 times as wide as the 2D layers and k3 times as wide as the 3D layer, both centred.
    "uniform float k2;\n"
    "uniform float k3;\n"
    "uniform vec3 backdrop;\n"
    "in vec2 v_uv;\n"
    "out vec4 color;\n"
    "void main() {\n"
    "  vec2 uv2 = vec2((v_uv.x - 0.5) * k2 + 0.5, v_uv.y);\n"
    "  bool inside = uv2.x >= 0.0 && uv2.x <= 1.0;\n"
    "  vec4 up = inside ? texture(above, uv2) : vec4(0.0);\n"
    "  vec4 dn = inside ? texture(below, uv2) : vec4(backdrop, 1.0);\n"
    "  float u3 = (v_uv.x - 0.5) * k3 + 0.5;\n"
    "  vec4 d3 = u3 >= 0.0 && u3 <= 1.0 ? texture(layer3d, vec2(u3, 1.0 - v_uv.y)) : vec4(0.0);\n"
    "  vec3 c;\n"
    "  if (d3.a <= 0.0) c = dn.rgb;\n"
    "  else if (dn.a <= 0.0) c = d3.rgb;\n"
    "  else if (d3.a < 0.99) c = mix(dn.rgb, d3.rgb, d3.a);\n"
    "  else c = mix(dn.rgb, d3.rgb, dn.a);\n"
    "  c = mix(c, up.rgb, up.a);\n"
    "  if (bright_mode == 1) c += (1.0 - c) * bright;\n"
    "  else if (bright_mode == 2) c -= c * bright;\n"
    "  color = vec4(c, 1.0);\n"
    "}\n";

static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        port_log("shader: %s", log);
    }
    return s;
}

static GLuint program(const char *fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, compile(GL_VERTEX_SHADER, VS));
    glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(p);
    return p;
}

static GLuint make_texture(void) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

// Uploads a 192-line picture `width` wide (widescreen 2D), reallocating the texture when the width changes.
static void upload(GLuint tex, const uint32_t *pixels, int width) {
    static GLuint names[8];
    static int widths[8];
    int i = 0;
    while (i < 7 && names[i] && names[i] != tex) i++;
    names[i] = tex;
    glBindTexture(GL_TEXTURE_2D, tex);
    if (widths[i] != width) {
        widths[i] = width;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, 192, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }
}

static void draw_quad(int x, int y, int w, int h, int surface_w, int surface_h) {
    float x0 = (float) x / surface_w * 2 - 1, x1 = (float) (x + w) / surface_w * 2 - 1;
    float y0 = 1 - (float) y / surface_h * 2, y1 = 1 - (float) (y + h) / surface_h * 2;
    const float v[] = {x0, y0, 0, 0, x1, y0, 1, 0, x0, y1, 0, 1, x1, y1, 1, 1};
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0); // client-side arrays
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, v);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, v + 2);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

typedef struct Presented {
    bool software; // both screens complete in s_tex_plain[0..1]
    bool a_on_top;
    bool race;       // a race frame
    int width_top, width_bottom; // of the 2D pictures of the top and bottom screens (top: widescreen)
    uint16_t bright; // engine A MASTER_BRIGHT
    float backdrop[3];
    int scale;
    const FrameSnapshot *snap; // for the textures; valid until the next snapshot is acquired
} Presented;

static GLuint s_prog_plain, s_prog_compose, s_tex_plain[2], s_tex_above, s_tex_below;

// Dual-screen 3D: engine A's picture of each screen (index 0 = top), from the last frame it was on that screen.
typedef struct Held {
    bool valid;
    uint64_t number;    // of its last frame
    int width;          // of its 2D (widescreen on the top screen)
    uint16_t bright;
    float backdrop[3];
    GLuint above, below;
    int cur;            // its last 3D list in s_alt3d[screen][cur], the one before in [cur ^ 1]
    bool have_prev;
} Held;
static bool s_alt; // dual-screen 3D
static bool s_prev_a_on_top;
static Held s_held[2];
static Gx3dFrame s_alt3d[2][2];
static GLint s_u_bright_mode, s_u_bright, s_u_k2, s_u_k3, s_u_backdrop;
static Presented s_shown = {true, true, false, 256, 256, 0, {0, 0, 0}, 1, NULL};
static float s_wide = 1.0f; // the display's aspect ratio / (4/3)
// The 3D lists of the last two frames (interpolation), only the used parts copied.
static Gx3dFrame s_frames3d[2];
static int s_cur3d;
static bool s_have_prev;
static uint64_t s_last_number;

static void copy_list(Gx3dFrame *d, const Gx3dFrame *g) {
    memcpy(d->vertices, g->vertices, sizeof(GxVertex) * g->vertex_count);
    memcpy(d->polygons, g->polygons, sizeof(GxPolygon) * g->polygon_count);
    size_t header = offsetof(Gx3dFrame, vertex_count);
    memcpy((uint8_t *) d + header, (const uint8_t *) g + header, sizeof(Gx3dFrame) - header);
}

void present_set_wide(float aspect) { s_wide = aspect > 4.0f / 3.0f ? aspect / (4.0f / 3.0f) : 1.0f; }
bool present_is_race(void) { return s_shown.race; }

void present_init(void) {
    s_prog_plain = program(FS_PLAIN);
    s_prog_compose = program(FS_COMPOSE);
    glUseProgram(s_prog_compose);
    glUniform1i(glGetUniformLocation(s_prog_compose, "above"), 0);
    glUniform1i(glGetUniformLocation(s_prog_compose, "below"), 1);
    glUniform1i(glGetUniformLocation(s_prog_compose, "layer3d"), 2);
    s_u_bright_mode = glGetUniformLocation(s_prog_compose, "bright_mode");
    s_u_bright = glGetUniformLocation(s_prog_compose, "bright");
    s_u_k2 = glGetUniformLocation(s_prog_compose, "k2");
    s_u_k3 = glGetUniformLocation(s_prog_compose, "k3");
    s_u_backdrop = glGetUniformLocation(s_prog_compose, "backdrop");
    s_tex_plain[0] = make_texture();
    s_tex_plain[1] = make_texture();
    s_tex_above = make_texture();
    s_tex_below = make_texture();
    for (int i = 0; i < 2; i++) {
        s_held[i].above = make_texture();
        s_held[i].below = make_texture();
    }
    gl3d_init();
}

void present_update(const FrameSnapshot *f, int scale) {
    static uint32_t buf_a[GPU2D_MAX_W * 192], buf_b[GPU2D_MAX_W * 192], buf_below[GPU2D_MAX_W * 192],
        layer3d[256 * 192];
    s_shown.a_on_top = gpu2d_engine_a_on_top(f);
    // Widescreen: the top screen's 2D is drawn as wide as the display (DS columns added on both sides).
    int top_w = (int) (192 * 4.0f / 3.0f * s_wide + 0.5f) & ~1;
    if (top_w < 256) top_w = 256;
    if (top_w > GPU2D_MAX_W) top_w = GPU2D_MAX_W;
    int margin = (top_w - 256) / 2;
    s_shown.width_top = top_w;
    s_shown.width_bottom = 256;
    int width_a = s_shown.a_on_top ? top_w : 256, width_b = s_shown.a_on_top ? 256 : top_w;
    bool capture = f->io[0x67] & 0x80;
    uint32_t dispcnt_a = (uint32_t) f->io[0] | f->io[1] << 8 | f->io[2] << 16 | (uint32_t) f->io[3] << 24;
    bool has_3d = (dispcnt_a & 8) && ((dispcnt_a >> 16) & 3) == 1;

    // Dual-screen 3D: a whole-screen capture of engine A (256x192, source A only) while the screens swap. It goes
    // on while the capture does, even if a frame was skipped here.
    uint32_t capcnt = (uint32_t) f->io[0x64] | f->io[0x65] << 8 | f->io[0x66] << 16 | (uint32_t) f->io[0x67] << 24;
    bool whole_capture = capture && has_3d && ((capcnt >> 20) & 3) == 3 && ((capcnt >> 29) & 3) == 0;
    // The capture bit can be seen already cleared (the game process clears it when the app has drawn the capture,
    // while the next frame is being taken): the mode ends only when the screens stop swapping or a few frames in a
    // row have no capture.
    static int s_alt_misses;
    bool swapped = f->number == s_last_number + 1 && s_shown.a_on_top != s_prev_a_on_top;
    if (s_alt && has_3d && !whole_capture && (swapped || ++s_alt_misses < 4)) {
        whole_capture = true;
    } else if (whole_capture) {
        s_alt_misses = 0;
    }
    if (!whole_capture) {
        s_held[0].valid = s_held[1].valid = false;
        s_alt = false;
        s_alt_misses = 0;
    } else if (!s_alt && f->number == s_last_number + 1 && s_shown.a_on_top != s_prev_a_on_top) {
        s_alt = true;
        s_held[0].valid = s_held[1].valid = false;
    }
    s_prev_a_on_top = s_shown.a_on_top;
    if (s_alt) {
        // No capture needed: the other screen is drawn from its own frame.
        Held *h = &s_held[s_shown.a_on_top ? 0 : 1];
        gpu2d_render_split(f, buf_a, buf_below, buf_b, s_shown.a_on_top ? margin : 0, s_shown.a_on_top ? 0 : margin);
        h->width = width_a;
        upload(h->above, buf_a, width_a);
        upload(h->below, buf_below, width_a);
        h->bright = (uint16_t) (f->io[0x6c] | f->io[0x6d] << 8);
        uint16_t bd = (uint16_t) (f->palette[0] | f->palette[1] << 8);
        h->backdrop[0] = (bd & 31) / 31.0f;
        h->backdrop[1] = ((bd >> 5) & 31) / 31.0f;
        h->backdrop[2] = ((bd >> 10) & 31) / 31.0f;
        h->have_prev = h->valid && f->number == h->number + 2;
        h->cur ^= 1;
        copy_list(&s_alt3d[s_shown.a_on_top ? 0 : 1][h->cur], &f->frame3d);
        h->number = f->number;
        h->valid = true;
        s_shown.software = false;
        upload(s_tex_plain[1], buf_b, width_b); // engine B: shown until the other screen has a frame of its own
        if (g_capture_done) g_capture_done(); // the game waits for the capture
        s_have_prev = false;
        s_shown.race = f->scene == 2;
        s_shown.scale = scale;
        s_shown.snap = f;
        s_last_number = f->number;
        return;
    }
    if (capture) {
        // The capture writes the frame into VRAM for later frames (the game shows it again, blends with it): it is
        // made in software at the DS resolution. With 3D, what is displayed still comes from the GPU path below.
        soft3d_render(&f->frame3d, f, layer3d);
        gpu2d_render(f, layer3d, buf_a, buf_b, margin);
    }
    if (!has_3d) {
        s_shown.software = true;
        if (!capture) gpu2d_render(f, NULL, buf_a, buf_b, margin); // buf_a: top screen, buf_b: bottom
        upload(s_tex_plain[0], buf_a, top_w);
        upload(s_tex_plain[1], buf_b, 256);
    } else {
        s_shown.software = false;
        gpu2d_render_split(f, buf_a, buf_below, buf_b, s_shown.a_on_top ? margin : 0, s_shown.a_on_top ? 0 : margin);
        upload(s_tex_above, buf_a, width_a);
        upload(s_tex_below, buf_below, width_a);
        upload(s_tex_plain[1], buf_b, width_b); // engine B
        s_shown.bright = (uint16_t) (f->io[0x6c] | f->io[0x6d] << 8);
        uint16_t bd = (uint16_t) (f->palette[0] | f->palette[1] << 8);
        s_shown.backdrop[0] = (bd & 31) / 31.0f;
        s_shown.backdrop[1] = ((bd >> 5) & 31) / 31.0f;
        s_shown.backdrop[2] = ((bd >> 10) & 31) / 31.0f;
        s_have_prev = f->number == s_last_number + 1;
        s_cur3d ^= 1;
        copy_list(&s_frames3d[s_cur3d], &f->frame3d);
    }
    s_shown.race = f->scene == 2;
    s_shown.scale = scale;
    s_shown.snap = f;
    s_last_number = f->number;
}

// The largest rectangle of the given aspect ratio centred in r.
static void fit(const int *r, float aspect, int *out) {
    float w = (float) r[2], h = (float) r[3];
    if (w / h > aspect) w = h * aspect;
    else h = w / aspect;
    out[0] = r[0] + (int) ((r[2] - w) / 2);
    out[1] = r[1] + (int) ((r[3] - h) / 2);
    out[2] = (int) w;
    out[3] = (int) h;
}

bool present_engine_a_on_top(void) { return s_shown.a_on_top; }

static GLuint s_target; // the framebuffer present_draw draws into (0: the window)
void present_set_target(uint32_t framebuffer) { s_target = framebuffer; }

static void draw_alt(const int layout[8], int w, int h, float t) {
    glBindFramebuffer(GL_FRAMEBUFFER, s_target);
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    int live = s_shown.a_on_top ? 0 : 1;
    for (int screen = 0; screen < 2; screen++) {
        if (layout[screen * 4 + 2] <= 0 || layout[screen * 4 + 3] <= 0) continue; // hidden
        const Held *hd = &s_held[screen];
        int r[4];
        if (!hd->valid) {
            fit(&layout[screen * 4], 256.0f / 192.0f, r);
            glUseProgram(s_prog_plain);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, s_tex_plain[1]);
            draw_quad(r[0], r[1], r[2], r[3], w, h);
            continue;
        }
        // Each screen's frames are two VBlanks apart: the live one is half way from its previous frame, the other
        // one goes on from there (the picture lags its frame by two VBlanks, and moves without pauses).
        float ts = screen == live ? t * 0.5f : 0.5f + t * 0.5f;
        if (ts > 1.0f) ts = 1.0f;
        float wide = (float) hd->width / 256.0f;
        const Gx3dFrame *cur = &s_alt3d[screen][hd->cur], *prev = hd->have_prev ? &s_alt3d[screen][hd->cur ^ 1] : NULL;
        gl3d_use_target(screen);
        GLuint tex3d = gl3d_render(cur, prev, prev ? ts : 1.0f, s_shown.snap, s_shown.scale, wide);
        gl3d_use_target(0);
        glBindFramebuffer(GL_FRAMEBUFFER, s_target);
        glViewport(0, 0, w, h);
        float wq = (float) hd->width;
        fit(&layout[screen * 4], wq / 192.0f, r);
        glUseProgram(s_prog_compose);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hd->above);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, hd->below);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, tex3d);
        glUniform1f(s_u_k2, 1.0f);
        glUniform1f(s_u_k3, 1.0f);
        glUniform3fv(s_u_backdrop, 1, hd->backdrop);
        uint32_t mode = hd->bright >> 14, factor = hd->bright & 31;
        if (factor > 16) factor = 16;
        glUniform1i(s_u_bright_mode, (mode == 1 || mode == 2) ? (int) mode : 0);
        glUniform1f(s_u_bright, factor / 16.0f);
        glActiveTexture(GL_TEXTURE0);
        draw_quad(r[0], r[1], r[2], r[3], w, h);
    }
}

void present_draw(const int layout[8], int w, int h, float t) {
    if (s_alt && s_shown.snap) {
        draw_alt(layout, w, h, t);
        return;
    }
    GLuint tex3d = 0;
    // The 3D layer is as wide as the 2D of its screen (widescreen on the top screen).
    // (Each polygon keeps the proportions of its own projection: gl3d narrows those projected for 4:3.)
    float wide = (float) (s_shown.a_on_top ? s_shown.width_top : 256) / 256.0f;
    if (!s_shown.software && s_shown.snap) {
        tex3d = gl3d_render(&s_frames3d[s_cur3d], s_have_prev ? &s_frames3d[s_cur3d ^ 1] : NULL, t, s_shown.snap,
                            s_shown.scale, wide);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, s_target);
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    for (int screen = 0; screen < 2; screen++) { // 0 = top rectangle, 1 = bottom rectangle
        bool engine_a = (screen == 0) == s_shown.a_on_top;
        int r[4];
        if (layout[screen * 4 + 2] <= 0 || layout[screen * 4 + 3] <= 0) continue; // hidden
        float w2 = (float) (screen == 0 ? s_shown.width_top : s_shown.width_bottom), w3 = 256.0f * wide;
        float wq = engine_a && !s_shown.software && w3 > w2 ? w3 : w2; // the screen's width in DS pixels
        fit(&layout[screen * 4], wq / 192.0f, r);
        if (s_shown.software) {
            glUseProgram(s_prog_plain);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, s_tex_plain[screen]);
        } else if (!engine_a) {
            glUseProgram(s_prog_plain);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, s_tex_plain[1]);
        } else {
            glUseProgram(s_prog_compose);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, s_tex_above);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, s_tex_below);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, tex3d);
            glUniform1f(s_u_k2, wq / w2);
            glUniform1f(s_u_k3, wq / w3);
            glUniform3fv(s_u_backdrop, 1, s_shown.backdrop);
            uint32_t mode = s_shown.bright >> 14, factor = s_shown.bright & 31;
            if (factor > 16) factor = 16;
            glUniform1i(s_u_bright_mode, (mode == 1 || mode == 2) ? (int) mode : 0);
            glUniform1f(s_u_bright, factor / 16.0f);
            glActiveTexture(GL_TEXTURE0);
        }
        draw_quad(r[0], r[1], r[2], r[3], w, h);
    }
}
