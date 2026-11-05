/*
 * platform/android.c - Android, through the NDK: a NativeActivity
 * (android_native_app_glue), an EGL OpenGL ES 3.0 context on its window,
 * keys, gamepads and touch from the input queue, and AAudio.
 *
 * android_main runs on the glue's thread and calls main.c's main
 * (built as ign_main), so the game's loops run there unchanged;
 * plat_pump_events drains the looper. When the app goes to the
 * background its window goes away: the loop then only waits for events
 * until a window comes back, keeping the EGL context.
 *
 * The game data goes in the app's folder on shared storage
 * (Android/data/<package>/files), which is also the working folder.
 */

#include <EGL/egl.h>
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <android_native_app_glue.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "app.h"
#include "dik.h"
#include "platform/platform.h"
#include "platform/common.h"
#include "platform/touch.h"
#include "rnd_gl.h"

int ign_main(int argc, char **argv);

static struct android_app *s_app;
static EGLDisplay s_egl_display = EGL_NO_DISPLAY;
static EGLConfig s_egl_config;
static EGLContext s_egl_context = EGL_NO_CONTEXT;
static EGLSurface s_egl_surface = EGL_NO_SURFACE;
static EGLSurface s_egl_idle = EGL_NO_SURFACE;  /* 1x1 pbuffer, see bind_idle */
static int s_width, s_height;
static int s_resumed;
static int s_quit;
static u32 s_pad_keys;      /* gamepad buttons, from key events */
static u32 s_pad_axes;      /* and from the sticks and triggers */

u32 plat_time_ms(void)
{
    static struct timespec start;
    struct timespec now;

    if (start.tv_sec == 0 && start.tv_nsec == 0) {
        clock_gettime(CLOCK_MONOTONIC, &start);
    }
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (u32)((now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000);
}

/* ------------------------------------------------------------ EGL */

/* Without a window (Home, Back, a locked screen) the context stays
 * current, on a 1x1 pbuffer, or surfaceless if that cannot be made. The
 * game pumps events from inside its frame (input_poll), so the window can
 * go away between two GL calls of one frame: with no current context
 * they would be lost, and the textures and canvases the game keeps
 * would come back half drawn, then black. */
static void bind_idle(void)
{
    if (s_egl_context != EGL_NO_CONTEXT
        && !eglMakeCurrent(s_egl_display, s_egl_idle, s_egl_idle, s_egl_context)) {
        fprintf(stderr, "ignition: cannot keep the context current (0x%x)\n", eglGetError());
    }
}

static void query_size(void)
{
    if (s_egl_surface != EGL_NO_SURFACE) {
        eglQuerySurface(s_egl_display, s_egl_surface, EGL_WIDTH, &s_width);
        eglQuerySurface(s_egl_display, s_egl_surface, EGL_HEIGHT, &s_height);
    }
}

static void make_surface(void)
{
    EGLSurface surface;

    if (s_app->window == NULL || s_egl_context == EGL_NO_CONTEXT
        || s_egl_surface != EGL_NO_SURFACE) {
        return;
    }
    surface = eglCreateWindowSurface(s_egl_display, s_egl_config, s_app->window, NULL);
    if (surface == EGL_NO_SURFACE) {
        fprintf(stderr, "ignition: no window surface (0x%x)\n", eglGetError());
        return;
    }
    if (!eglMakeCurrent(s_egl_display, surface, surface, s_egl_context)) {
        fprintf(stderr, "ignition: cannot use the window surface (0x%x)\n", eglGetError());
        eglDestroySurface(s_egl_display, surface);
        bind_idle();
        return;
    }
    s_egl_surface = surface;
    eglSwapInterval(s_egl_display, 1);
    query_size();
}

static void drop_surface(void)
{
    if (s_egl_surface != EGL_NO_SURFACE) {
        bind_idle();
        eglDestroySurface(s_egl_display, s_egl_surface);
        s_egl_surface = EGL_NO_SURFACE;
    }
}

/* ------------------------------------------------------------ input */

static int map_key(int32_t code)
{
    if (code >= AKEYCODE_A && code <= AKEYCODE_Z) {
        static const u8 letters[26] = {
            DIK_A, DIK_B, DIK_C, DIK_D, DIK_E, DIK_F, DIK_G, DIK_H, DIK_I, DIK_J, DIK_K, DIK_L,
            DIK_M, DIK_N, DIK_O, DIK_P, DIK_Q, DIK_R, DIK_S, DIK_T, DIK_U, DIK_V, DIK_W, DIK_X,
            DIK_Y, DIK_Z
        };
        return letters[code - AKEYCODE_A];
    }
    if (code >= AKEYCODE_1 && code <= AKEYCODE_9) {
        return DIK_1 + (code - AKEYCODE_1);
    }
    switch (code) {
    case AKEYCODE_0: return DIK_0;
    case AKEYCODE_BACK: return DIK_ESCAPE;
    case AKEYCODE_ESCAPE: return DIK_ESCAPE;
    case AKEYCODE_ENTER: return DIK_RETURN;
    case AKEYCODE_NUMPAD_ENTER: return DIK_NUMPADENTER;
    case AKEYCODE_SPACE: return DIK_SPACE;
    case AKEYCODE_TAB: return DIK_TAB;
    case AKEYCODE_DEL: return DIK_BACK;
    case AKEYCODE_FORWARD_DEL: return DIK_DELETE;
    case AKEYCODE_SHIFT_LEFT: return DIK_LSHIFT;
    case AKEYCODE_SHIFT_RIGHT: return DIK_RSHIFT;
    case AKEYCODE_CTRL_LEFT: return DIK_LCONTROL;
    case AKEYCODE_CTRL_RIGHT: return DIK_RCONTROL;
    case AKEYCODE_ALT_LEFT: return DIK_LMENU;
    case AKEYCODE_ALT_RIGHT: return DIK_RMENU;
    case AKEYCODE_MINUS: return DIK_MINUS;
    case AKEYCODE_EQUALS: return DIK_EQUALS;
    case AKEYCODE_COMMA: return DIK_COMMA;
    case AKEYCODE_PERIOD: return DIK_PERIOD;
    case AKEYCODE_SLASH: return DIK_SLASH;
    case AKEYCODE_SEMICOLON: return DIK_SEMICOLON;
    case AKEYCODE_APOSTROPHE: return DIK_APOSTROPHE;
    case AKEYCODE_LEFT_BRACKET: return DIK_LBRACKET;
    case AKEYCODE_RIGHT_BRACKET: return DIK_RBRACKET;
    case AKEYCODE_F1: return DIK_F1;
    case AKEYCODE_F2: return DIK_F2;
    case AKEYCODE_F3: return DIK_F3;
    case AKEYCODE_F4: return DIK_F4;
    case AKEYCODE_F5: return DIK_F5;
    case AKEYCODE_F6: return DIK_F6;
    case AKEYCODE_F7: return DIK_F7;
    case AKEYCODE_F8: return DIK_F8;
    case AKEYCODE_F9: return DIK_F9;
    case AKEYCODE_F10: return DIK_F10;
    case AKEYCODE_MOVE_HOME: return DIK_HOME;
    case AKEYCODE_MOVE_END: return DIK_END;
    case AKEYCODE_PAGE_UP: return DIK_PRIOR;
    case AKEYCODE_PAGE_DOWN: return DIK_NEXT;
    default: return 0;
    }
}

/* Gamepad buttons and the d-pad, as PAD_* bits. */
static u32 map_button(int32_t code)
{
    switch (code) {
    case AKEYCODE_DPAD_LEFT: return PAD_LEFT;
    case AKEYCODE_DPAD_RIGHT: return PAD_RIGHT;
    case AKEYCODE_DPAD_UP: return PAD_UP;
    case AKEYCODE_DPAD_DOWN: return PAD_DOWN;
    case AKEYCODE_DPAD_CENTER: return PAD_A;
    case AKEYCODE_BUTTON_A: return PAD_A;
    case AKEYCODE_BUTTON_B: return PAD_B;
    case AKEYCODE_BUTTON_X: return PAD_X;
    case AKEYCODE_BUTTON_Y: return PAD_Y;
    case AKEYCODE_BUTTON_L1: case AKEYCODE_BUTTON_L2: return PAD_L;
    case AKEYCODE_BUTTON_R1: case AKEYCODE_BUTTON_R2: return PAD_R;
    case AKEYCODE_BUTTON_START: return PAD_START;
    case AKEYCODE_BUTTON_SELECT: return PAD_SELECT;
    default: return 0;
    }
}

static int32_t on_input(struct android_app *app, AInputEvent *e)
{
    (void)app;
    if (AInputEvent_getType(e) == AINPUT_EVENT_TYPE_KEY) {
        int32_t code = AKeyEvent_getKeyCode(e);
        int down = AKeyEvent_getAction(e) == AKEY_EVENT_ACTION_DOWN;
        u32 button = map_button(code);
        int dik = map_key(code);

        if (button != 0) {
            s_pad_keys = down ? s_pad_keys | button : s_pad_keys & ~button;
            return 1;
        }
        if (dik == 0) {
            return 0;
        }
        if (down && AKeyEvent_getRepeatCount(e) == 0 && !g_plat_keys[dik]) {
            plat_key_event(dik, 1);
        } else if (!down && g_plat_keys[dik]) {
            plat_key_event(dik, 0);
        }
        return 1;
    }
    if (AInputEvent_getType(e) == AINPUT_EVENT_TYPE_MOTION) {
        int32_t source = AInputEvent_getSource(e);
        int32_t action = AMotionEvent_getAction(e);
        int32_t kind = action & AMOTION_EVENT_ACTION_MASK;
        size_t i, n = AMotionEvent_getPointerCount(e);

        if ((source & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK) {
            float x = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_X, 0);
            float y = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_Y, 0);
            float hx = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_HAT_X, 0);
            float hy = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_HAT_Y, 0);
            float lt = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_LTRIGGER, 0);
            float rt = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_RTRIGGER, 0);

            s_pad_axes = (x < -0.5f || hx < -0.5f ? PAD_LEFT : 0)
                       | (x > 0.5f || hx > 0.5f ? PAD_RIGHT : 0)
                       | (y < -0.5f || hy < -0.5f ? PAD_UP : 0)
                       | (y > 0.5f || hy > 0.5f ? PAD_DOWN : 0)
                       | (lt > 0.5f ? PAD_L : 0) | (rt > 0.5f ? PAD_R : 0);
            return 1;
        }
        for (i = 0; i < n; i++) {
            int up = 0;

            if (kind == AMOTION_EVENT_ACTION_UP || kind == AMOTION_EVENT_ACTION_CANCEL) {
                up = 1;
            } else if (kind == AMOTION_EVENT_ACTION_POINTER_UP) {
                up = (size_t)((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                              >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT) == i;
            }
            touch_finger(AMotionEvent_getPointerId(e, i), AMotionEvent_getX(e, i),
                         AMotionEvent_getY(e, i), !up);
        }
        return 1;
    }
    return 0;
}

static void on_command(struct android_app *app, int32_t cmd)
{
    (void)app;
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        make_surface();
        break;
    case APP_CMD_TERM_WINDOW:
        drop_surface();
        break;
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONFIG_CHANGED:
        query_size();
        break;
    case APP_CMD_RESUME:
        s_resumed = 1;
        break;
    case APP_CMD_PAUSE:
        s_resumed = 0;
        break;
    case APP_CMD_DESTROY:
        s_quit = 1;
        break;
    default:
        break;
    }
}

/* Handles what the looper has; waits up to `ms` for it (-1: forever). */
static void poll_looper(int ms)
{
    struct android_poll_source *source;
    int events;

    while (ALooper_pollOnce(ms, NULL, &events, (void **)&source) >= 0) {
        if (source != NULL) {
            source->process(s_app, source);
        }
        if (s_app->destroyRequested) {
            s_quit = 1;
            return;
        }
        ms = 0;
    }
}

/* ------------------------------------------------------------ display */

/* The screen is the window: nothing to size. */
void plat_window_size(int width, int height, int *old_width, int *old_height)
{
    (void)width;
    (void)height;
    *old_width = *old_height = 0;
}

int plat_window_open(const char *title, int width, int height, int fullscreen)
{
    EGLint config_attrs[] = {
        EGL_RENDERABLE_TYPE, 0x40 /* EGL_OPENGL_ES3_BIT */,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE
    };
    static const EGLint context_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    static const EGLint idle_attrs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
    EGLint n = 0;

    (void)title;
    (void)width;
    (void)height;
    (void)fullscreen;
    if (s_egl_context != EGL_NO_CONTEXT) {
        return 1;
    }
    /* The window comes with the activity's first INIT_WINDOW. */
    while (s_app->window == NULL && !s_quit) {
        poll_looper(-1);
    }
    s_egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(s_egl_display, NULL, NULL);
    if (!eglChooseConfig(s_egl_display, config_attrs, &s_egl_config, 1, &n) || n == 0) {
        /* No configuration with a pbuffer too: window only, and the
         * context idles surfaceless. */
        config_attrs[3] = EGL_WINDOW_BIT;
        if (!eglChooseConfig(s_egl_display, config_attrs, &s_egl_config, 1, &n) || n == 0) {
            fprintf(stderr, "ignition: no EGL configuration for OpenGL ES 3\n");
            return 0;
        }
    }
    s_egl_context = eglCreateContext(s_egl_display, s_egl_config, EGL_NO_CONTEXT, context_attrs);
    if (s_egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "ignition: no OpenGL ES 3 context\n");
        return 0;
    }
    if (config_attrs[3] & EGL_PBUFFER_BIT) {
        s_egl_idle = eglCreatePbufferSurface(s_egl_display, s_egl_config, idle_attrs);
    }
    bind_idle();
    make_surface();
    return 1;
}

void plat_destroy_window(void)
{
}

void plat_drawable_size(int *width, int *height)
{
    *width = s_width > 0 ? s_width : 1;
    *height = s_height > 0 ? s_height : 1;
}

void plat_present(void)
{
    if (s_egl_surface == EGL_NO_SURFACE) {
        return;
    }
    touch_draw(s_width, s_height);
    if (!eglSwapBuffers(s_egl_display, s_egl_surface)) {
        EGLint error = eglGetError();

        fprintf(stderr, "ignition: swap failed (0x%x)\n", error);
        if (error == EGL_BAD_SURFACE || error == EGL_BAD_NATIVE_WINDOW) {
            /* The window went away under the surface: wait for a new one. */
            drop_surface();
        }
    }
}

void plat_request_quit(void)
{
    s_quit = 1;
}

void plat_pump_events(void)
{
    plat_run_scheduled();
    poll_looper(0);
    plat_pad_update(s_pad_keys | s_pad_axes | touch_buttons(s_width, s_height));
}

/* ------------------------------------------------------------ audio */

static AAudioStream *s_stream;
static plat_audio_fn s_audio_cb;
static void *s_audio_user;

static aaudio_data_callback_result_t audio_cb(AAudioStream *stream, void *user, void *data,
                                              int32_t frames)
{
    (void)stream;
    (void)user;
    s_audio_cb((float *)data, frames, s_audio_user);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

/* AAudio, 32 bit float stereo, pulled on its own thread. */
int plat_audio_open(s32 rate, plat_audio_fn cb, void *user)
{
    AAudioStreamBuilder *b;

    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) {
        return 0;
    }
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setSampleRate(b, rate);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(b, audio_cb, NULL);
    s_audio_cb = cb;
    s_audio_user = user;
    if (AAudioStreamBuilder_openStream(b, &s_stream) != AAUDIO_OK) {
        AAudioStreamBuilder_delete(b);
        s_stream = NULL;
        return 0;
    }
    AAudioStreamBuilder_delete(b);
    AAudioStream_requestStart(s_stream);
    return 1;
}

void plat_audio_close(void)
{
    if (s_stream != NULL) {
        AAudioStream_requestStop(s_stream);
        AAudioStream_close(s_stream);
        s_stream = NULL;
    }
}

/* ------------------------------------------------------------ loop */

/* While the app has no window, it only waits for events. */
static int run(int (*frame)(void *), void *user)
{
    while (!s_quit) {
        int active = s_egl_surface != EGL_NO_SURFACE;

        g_app_active = active;
        if (!active) {
            if (s_stream != NULL) {
                AAudioStream_requestPause(s_stream);
            }
            while (s_egl_surface == EGL_NO_SURFACE && !s_quit) {
                poll_looper(-1);
                make_surface();
            }
            if (s_stream != NULL) {
                AAudioStream_requestStart(s_stream);
            }
            continue;
        }
        plat_pump_events();
        if (s_egl_surface == EGL_NO_SURFACE) {
            continue;           /* the window went away: wait for it above */
        }
        if (!frame(user)) {
            plat_request_quit();
        }
    }
    plat_audio_close();
    return 0;
}

static int tick(void *user)
{
    (void)user;
    return app_tick();
}

int plat_run(void)
{
    return run(tick, NULL);
}

int plat_run_with(int (*frame)(void *user), void *user)
{
    return run(frame, user);
}

/* ------------------------------------------------------------ folders */

const char *plat_host_exe_dir(void)
{
    return NULL;
}

const char *plat_host_pref_dir(void)
{
    const char *dir = s_app->activity->externalDataPath;

    if (dir == NULL) {
        dir = s_app->activity->internalDataPath;
    }
    return dir;
}

/* stdout and stderr go to logcat. */
static void *log_thread(void *arg)
{
    int *fds = arg;
    char line[512];
    FILE *in = fdopen(fds[0], "r");

    while (in != NULL && fgets(line, sizeof(line), in) != NULL) {
        __android_log_write(ANDROID_LOG_INFO, "ignition", line);
    }
    return NULL;
}

void android_main(struct android_app *app)
{
    static int fds[2];
    static char *argv[] = { "ignition", NULL };
    pthread_t t;
    const char *dir;

    s_app = app;
    app->onAppCmd = on_command;
    app->onInputEvent = on_input;
    if (pipe(fds) == 0) {
        setvbuf(stdout, NULL, _IOLBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        pthread_create(&t, NULL, log_thread, fds);
    }
    /* The game data and the log (Default.fil) live in the app's folder. */
    dir = plat_host_pref_dir();
    if (dir != NULL && chdir(dir) != 0) {
        fprintf(stderr, "ignition: cannot enter %s\n", dir);
    }
    ign_main(1, argv);
    /* The engine keeps its state in globals: a new start needs a new
     * process. */
    exit(0);
}
