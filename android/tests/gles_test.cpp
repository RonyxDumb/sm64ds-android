#include "scene.h"
#include "gles_renderer.h"
#include "screen_renderer.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <cstdio>
#include <stdexcept>
#include <vector>
static void check(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
int main(int argc, char **argv) {
    try {
        auto platform = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
        EGLDisplay display = platform ? platform(0x31DD, EGL_DEFAULT_DISPLAY, nullptr)
                                      : eglGetDisplay(EGL_DEFAULT_DISPLAY);
        check(eglInitialize(display, nullptr, nullptr), "EGL initialization failed");
        check(eglBindAPI(EGL_OPENGL_ES_API), "EGL GLES API failed");
        EGLint attrs[] = {EGL_SURFACE_TYPE,
                          EGL_PBUFFER_BIT,
                          EGL_RENDERABLE_TYPE,
                          EGL_OPENGL_ES3_BIT_KHR,
                          EGL_RED_SIZE,
                          8,
                          EGL_GREEN_SIZE,
                          8,
                          EGL_BLUE_SIZE,
                          8,
                          EGL_DEPTH_SIZE,
                          24,
                          EGL_NONE};
        EGLConfig config;
        EGLint count = 0;
        check(eglChooseConfig(display, attrs, &config, 1, &count) && count, "No GLES3 config");
        EGLint pbuffer[] = {EGL_WIDTH, 960, EGL_HEIGHT, 540, EGL_NONE};
        EGLSurface surface = eglCreatePbufferSurface(display, config, pbuffer);
        EGLint contextAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttrs);
        check(eglMakeCurrent(display, surface, surface, context), "EGL make current failed");
        std::printf("GL renderer: %s\n", glGetString(GL_RENDERER));
        sm64ds::Scene scene;
        if (argc > 1)
            scene = sm64ds::load_scene(argv[1]);
        else {
            scene.radius = 1;
            scene.vertices = {{-.7f, -.7f, 0, 0, 0, 255, 255, 255, 255},
                              {.7f, -.7f, 0, 1, 0, 255, 255, 255, 255},
                              {0, .7f, 0, .5f, 1, 255, 255, 255, 255}};
            scene.textures.push_back(
                {2, 2, {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255}});
            scene.batches.push_back({0, 3, 0});
        }
        sm64ds::GlesRenderer renderer;
        renderer.create(scene);
        renderer.draw(scene, 960, 540, 0, 1);
        std::vector<uint8_t> pixels(960 * 540 * 4);
        glReadPixels(0, 0, 960, 540, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        check(glGetError() == GL_NO_ERROR, "OpenGL error after drawing");
        size_t coverage = 0;
        for (size_t i = 0; i < pixels.size(); i += 4)
            if (pixels[i] > 30 || pixels[i + 1] > 30 || pixels[i + 2] > 30)
                ++coverage;
        check(coverage > 2000 && coverage < 960 * 540 / 2, "Unexpected rendered coverage");
        std::printf("%zu triangles; %zu visible pixels\n", scene.vertices.size() / 3, coverage);
        if (argc > 2) {
            FILE *f = fopen(argv[2], "wb");
            check(f, "Image output failed");
            fprintf(f, "P6\n960 540\n255\n");
            for (int y = 539; y >= 0; --y)
                for (int x = 0; x < 960; ++x)
                    fwrite(&pixels[(y * 960 + x) * 4], 1, 3, f);
            fclose(f);
        }
        renderer.release();
        // Repeat creation/teardown to cover context lifecycle resource handling.
        renderer.create(scene);
        renderer.draw(scene, 960, 540, 1.2f, .8f);
        renderer.release();
        check(glGetError() == GL_NO_ERROR, "GL error after recreate");
        ntr::Framebuffer top{}, bottom{};
        for (int y = 0; y < ntr::SCREEN_H; ++y)
            for (int x = 0; x < ntr::SCREEN_W; ++x) {
                top.px[y][x] = y < 96 ? 0xffff0000u : 0xff0000ffu;
                bottom.px[y][x] = 0xff00ff00u;
            }
        sm64ds::ScreenRenderer screens;
        for (int repeat = 0; repeat < 2; ++repeat) {
            screens.create();
            screens.draw(top, bottom, 960, 540);
            uint8_t sample[4]{};
            glReadPixels(240, 400, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sample);
            check(sample[0] == 255 && sample[1] == 0 && sample[2] == 0,
                  "top screen orientation/color");
            glReadPixels(240, 120, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sample);
            check(sample[0] == 0 && sample[1] == 0 && sample[2] == 255,
                  "bottom row orientation/color");
            glReadPixels(720, 270, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sample);
            check(sample[0] == 0 && sample[1] == 255 && sample[2] == 0, "sub-screen scanout");
            screens.draw(top, bottom, 960, 540, false);
            glReadPixels(240, 270, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sample);
            check(sample[0] == 0 && sample[1] == 255 && sample[2] == 0,
                  "POWCNT1 swaps engine B onto the physical upper screen");
            glReadPixels(720, 400, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sample);
            check(sample[0] == 255 && sample[1] == 0 && sample[2] == 0,
                  "POWCNT1 swaps engine A onto the lower touch screen");
            screens.release();
        }
        check(glGetError() == GL_NO_ERROR, "native screen GL lifecycle error");
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(display, context);
        eglDestroySurface(display, surface);
        eglTerminate(display);
        puts("GLES3 renderer tests passed");
    } catch (const std::exception &e) {
        puts(e.what());
        return 1;
    }
}
