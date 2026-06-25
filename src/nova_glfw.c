/* nova_glfw.c — GLFW 3.x lifecycle/input shim implemented on libctru + NovaGL.
 * Public surface + 3DS caveats are documented in include/nova_glfw.h. All the
 * libctru-specific types stay in this .c (the header is <3ds.h>-free). */

#include <3ds.h>
#include <string.h>

#include "nova_glfw.h"
#include "NovaGL.h"   /* nova_init / nova_fini / novaSwapBuffers / novaglGetProcAddress */

#define CPAD_RANGE 156.0f

/* ── Single virtual window/context (the 3DS top screen) ──────────────────── */
typedef struct {
    int   should_close;
    int   swap_interval;
    int   width, height;          /* logical top-screen size */
    char  title[64];

    GLFWkeyfun            cb_key;
    GLFWcharfun           cb_char;
    GLFWmousebuttonfun    cb_mouse;
    GLFWcursorposfun      cb_cursorpos;
    GLFWcursorenterfun    cb_cursorenter;
    GLFWscrollfun         cb_scroll;
    GLFWframebuffersizefun cb_fbsize;
    GLFWwindowsizefun     cb_winsize;
    GLFWwindowclosefun    cb_close;
    GLFWwindowfocusfun    cb_focus;
    GLFWwindowrefreshfun  cb_refresh;
} NovaGlfwWindow;

static NovaGlfwWindow s_win;
static int          s_glfw_inited     = 0;  /* gfxInitDefault done */
static int          s_context_inited  = 0;  /* nova_init done */
static u64          s_time_base       = 0;
static GLFWerrorfun s_error_cb        = 0;
static int          s_last_error      = GLFW_NO_ERROR;
static GLFWmonitor *s_primary_monitor = (GLFWmonitor *)0x1; /* opaque sentinel */
static GLFWvidmode  s_vidmode = { NOVA_SCREEN_W, NOVA_SCREEN_H, 8, 8, 8, 60 };
static u32          s_prev_keys_held  = 0;

static void set_error(int code) {
    if (s_last_error == GLFW_NO_ERROR) s_last_error = code;
    if (s_error_cb) s_error_cb(code, "NovaGL GLFW shim");
}

static float clampf_(float v) {
    if (v < -1.0f) return -1.0f;
    if (v >  1.0f) return  1.0f;
    return v;
}

/* Lazily create the GL context (C3D / citro3d) the first time it's needed.
 * Triggered by both glfwCreateWindow and glfwMakeContextCurrent so a game that
 * uses either ordering ends up with a live context before its first GL call. */
static void ensure_context(void) {
    if (s_context_inited) return;
    nova_init();
    s_context_inited = 1;
    /* Fire the framebuffer-size callback once so resize-driven viewport setup
     * (common in GLFW games) initializes against the real 400x240 surface. */
    if (s_win.cb_fbsize) s_win.cb_fbsize((GLFWwindow *)&s_win, s_win.width, s_win.height);
}

/* ── Key token -> 3DS button mask. 0 = unmapped (glfwGetKey -> RELEASE). ──── */
static u32 key_to_3ds(int key) {
    switch (key) {
        case GLFW_KEY_ESCAPE:        return KEY_START;
        case GLFW_KEY_ENTER:
        case GLFW_KEY_SPACE:         return KEY_A;
        case GLFW_KEY_BACKSPACE:     return KEY_B;
        case GLFW_KEY_TAB:           return KEY_SELECT;
        case GLFW_KEY_LEFT:
        case GLFW_KEY_A:             return KEY_DLEFT;
        case GLFW_KEY_RIGHT:
        case GLFW_KEY_D:             return KEY_DRIGHT;
        case GLFW_KEY_UP:
        case GLFW_KEY_W:             return KEY_DUP;
        case GLFW_KEY_DOWN:
        case GLFW_KEY_S:             return KEY_DDOWN;
        case GLFW_KEY_LEFT_SHIFT:
        case GLFW_KEY_LEFT_CONTROL:
        case GLFW_KEY_Q:             return KEY_L;
        case GLFW_KEY_RIGHT_SHIFT:
        case GLFW_KEY_RIGHT_CONTROL:
        case GLFW_KEY_E:             return KEY_R;
        case GLFW_KEY_X:             return KEY_X;
        case GLFW_KEY_Y:             return KEY_Y;
        default:                     return 0;
    }
}

static int current_mods(u32 held) {
    int m = 0;
    if (held & (KEY_L | KEY_R)) m |= GLFW_MOD_SHIFT;
    return m;
}

/* The subset of GLFW keys we can report press/release transitions for. */
static const int s_dispatch_keys[] = {
    GLFW_KEY_ESCAPE, GLFW_KEY_ENTER, GLFW_KEY_SPACE, GLFW_KEY_BACKSPACE, GLFW_KEY_TAB,
    GLFW_KEY_LEFT, GLFW_KEY_RIGHT, GLFW_KEY_UP, GLFW_KEY_DOWN,
    GLFW_KEY_LEFT_SHIFT, GLFW_KEY_RIGHT_SHIFT, GLFW_KEY_X, GLFW_KEY_Y,
};

/* =======================================================================
 * Lifecycle
 * ===================================================================== */
int glfwInit(void) {
    if (s_glfw_inited) return GLFW_TRUE; /* idempotent */
    gfxInitDefault();
    memset(&s_win, 0, sizeof(s_win));
    s_win.width = NOVA_SCREEN_W;   /* 400 */
    s_win.height = NOVA_SCREEN_H;  /* 240 */
    s_win.swap_interval = 1;
    s_time_base = svcGetSystemTick();
    s_prev_keys_held = 0;
    s_glfw_inited = 1;
    return GLFW_TRUE;
}

void glfwTerminate(void) {
    if (s_context_inited) { nova_fini(); s_context_inited = 0; }
    if (s_glfw_inited)    { gfxExit();   s_glfw_inited = 0; }
}

void glfwGetVersion(int *major, int *minor, int *rev) {
    if (major) *major = 3;
    if (minor) *minor = 3;
    if (rev)   *rev   = 8;
}

const char *glfwGetVersionString(void) { return "3.3.8 NovaGL-3DS"; }

GLFWerrorfun glfwSetErrorCallback(GLFWerrorfun callback) {
    GLFWerrorfun prev = s_error_cb;
    s_error_cb = callback;
    return prev;
}

int glfwGetError(const char **description) {
    int e = s_last_error;
    s_last_error = GLFW_NO_ERROR;
    if (description) *description = (e == GLFW_NO_ERROR) ? (const char *)0 : "NovaGL GLFW shim";
    return e;
}

/* Window hints are inert on PICA (fixed-function, single fixed surface) but must
 * accept the calls so a desktop game's setup code runs unchanged. */
void glfwDefaultWindowHints(void) {}
void glfwWindowHint(int hint, int value) { (void)hint; (void)value; }
void glfwWindowHintString(int hint, const char *value) { (void)hint; (void)value; }

GLFWwindow *glfwCreateWindow(int width, int height, const char *title,
                             GLFWmonitor *monitor, GLFWwindow *share) {
    (void)monitor; (void)share;
    if (!s_glfw_inited) { set_error(GLFW_NOT_INITIALIZED); return (GLFWwindow *)0; }
    /* Record the requested size but keep the logical surface fixed at the top
     * screen — games read the real size back via glfwGetFramebufferSize. */
    (void)width; (void)height;
    if (title) { strncpy(s_win.title, title, sizeof(s_win.title) - 1); s_win.title[sizeof(s_win.title)-1] = 0; }
    ensure_context();
    return (GLFWwindow *)&s_win;
}

void glfwDestroyWindow(GLFWwindow *window) {
    (void)window;
    if (s_context_inited) { nova_fini(); s_context_inited = 0; }
}

int glfwWindowShouldClose(GLFWwindow *window) {
    (void)window;
    /* Drive the libctru main loop here: HOME/sleep/power requests end the loop,
     * exactly like the canonical `while (aptMainLoop())`. */
    if (!aptMainLoop()) return GLFW_TRUE;
    return s_win.should_close ? GLFW_TRUE : GLFW_FALSE;
}

void glfwSetWindowShouldClose(GLFWwindow *window, int value) {
    (void)window;
    s_win.should_close = value;
}

void glfwSetWindowTitle(GLFWwindow *window, const char *title) {
    (void)window;
    if (title) { strncpy(s_win.title, title, sizeof(s_win.title) - 1); s_win.title[sizeof(s_win.title)-1] = 0; }
}

void glfwGetWindowSize(GLFWwindow *window, int *width, int *height) {
    (void)window;
    if (width)  *width  = s_win.width;
    if (height) *height = s_win.height;
}

void glfwSetWindowSize(GLFWwindow *window, int width, int height) {
    (void)window; (void)width; (void)height; /* fixed surface */
}

void glfwGetFramebufferSize(GLFWwindow *window, int *width, int *height) {
    (void)window;
    if (width)  *width  = s_win.width;
    if (height) *height = s_win.height;
}

void glfwGetWindowPos(GLFWwindow *window, int *xpos, int *ypos) {
    (void)window; if (xpos) *xpos = 0; if (ypos) *ypos = 0;
}
void glfwSetWindowPos(GLFWwindow *window, int xpos, int ypos) { (void)window; (void)xpos; (void)ypos; }

void glfwGetWindowContentScale(GLFWwindow *window, float *xscale, float *yscale) {
    (void)window; if (xscale) *xscale = 1.0f; if (yscale) *yscale = 1.0f;
}

void glfwMakeContextCurrent(GLFWwindow *window) {
    if (window) ensure_context();
}

GLFWwindow *glfwGetCurrentContext(void) {
    return s_context_inited ? (GLFWwindow *)&s_win : (GLFWwindow *)0;
}

void glfwSwapBuffers(GLFWwindow *window) {
    (void)window;
    novaSwapBuffers();
}

void glfwSwapInterval(int interval) {
    /* VSync is compile-time on NovaGL (NOVAGL_VSYNC); record the request for
     * glfwGetInputMode-style introspection but the cadence is fixed by the build. */
    s_win.swap_interval = interval;
}

int glfwExtensionSupported(const char *extension) { (void)extension; return GLFW_FALSE; }

GLFWglproc glfwGetProcAddress(const char *procname) {
    return (GLFWglproc)novaglGetProcAddress(procname);
}

void glfwPollEvents(void) {
    hidScanInput();
    u32 held = hidKeysHeld();
    int mods = current_mods(held);

    /* Dispatch key press/release transitions for the mapped subset. Two distinct
     * GLFW keys can map to the same 3DS button (e.g. LEFT and A); we report each
     * GLFW key independently based on its button's down/up edge this frame. */
    u32 down = hidKeysDown();
    u32 up   = hidKeysUp();
    if (s_win.cb_key) {
        for (unsigned i = 0; i < sizeof(s_dispatch_keys)/sizeof(s_dispatch_keys[0]); i++) {
            int k = s_dispatch_keys[i];
            u32 m = key_to_3ds(k);
            if (!m) continue;
            if (down & m) s_win.cb_key((GLFWwindow *)&s_win, k, 0, GLFW_PRESS,   mods);
            if (up   & m) s_win.cb_key((GLFWwindow *)&s_win, k, 0, GLFW_RELEASE, mods);
        }
    }

    /* Touchscreen as a "mouse": fire cursor-pos + button callbacks if present. */
    if (s_win.cb_mouse || s_win.cb_cursorpos) {
        touchPosition tp;
        hidTouchRead(&tp);
        if (s_win.cb_cursorpos && (down | held) & KEY_TOUCH)
            s_win.cb_cursorpos((GLFWwindow *)&s_win, (double)tp.px, (double)tp.py);
        if (s_win.cb_mouse) {
            if (down & KEY_TOUCH) s_win.cb_mouse((GLFWwindow *)&s_win, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS,   mods);
            if (up   & KEY_TOUCH) s_win.cb_mouse((GLFWwindow *)&s_win, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, mods);
        }
    }

    s_prev_keys_held = held;
}

void glfwWaitEvents(void) { glfwPollEvents(); } /* no blocking event source we want to stall on */
void glfwWaitEventsTimeout(double timeout) { (void)timeout; glfwPollEvents(); }
void glfwPostEmptyEvent(void) {}

double glfwGetTime(void) {
    return (double)(svcGetSystemTick() - s_time_base) / (double)SYSCLOCK_ARM11;
}

void glfwSetTime(double time) {
    s_time_base = svcGetSystemTick() - (u64)(time * (double)SYSCLOCK_ARM11);
}

unsigned long long glfwGetTimerValue(void)     { return (unsigned long long)svcGetSystemTick(); }
unsigned long long glfwGetTimerFrequency(void) { return (unsigned long long)SYSCLOCK_ARM11; }

/* ── Monitors ────────────────────────────────────────────────────────────── */
GLFWmonitor *glfwGetPrimaryMonitor(void) { return s_primary_monitor; }

GLFWmonitor **glfwGetMonitors(int *count) {
    static GLFWmonitor *mons[1];
    mons[0] = s_primary_monitor;
    if (count) *count = 1;
    return mons;
}

const GLFWvidmode *glfwGetVideoMode(GLFWmonitor *monitor) { (void)monitor; return &s_vidmode; }
void glfwGetMonitorPos(GLFWmonitor *monitor, int *xpos, int *ypos) {
    (void)monitor; if (xpos) *xpos = 0; if (ypos) *ypos = 0;
}

/* =======================================================================
 * Input
 * ===================================================================== */
int glfwGetKey(GLFWwindow *window, int key) {
    (void)window;
    u32 m = key_to_3ds(key);
    if (!m) return GLFW_RELEASE;
    return (hidKeysHeld() & m) ? GLFW_PRESS : GLFW_RELEASE;
}

const char *glfwGetKeyName(int key, int scancode) { (void)key; (void)scancode; return (const char *)0; }

int glfwGetMouseButton(GLFWwindow *window, int button) {
    (void)window;
    if (button == GLFW_MOUSE_BUTTON_LEFT && (hidKeysHeld() & KEY_TOUCH)) return GLFW_PRESS;
    return GLFW_RELEASE;
}

void glfwGetCursorPos(GLFWwindow *window, double *xpos, double *ypos) {
    (void)window;
    touchPosition tp;
    hidTouchRead(&tp);
    if (xpos) *xpos = (double)tp.px;
    if (ypos) *ypos = (double)tp.py;
}

void glfwSetCursorPos(GLFWwindow *window, double xpos, double ypos) {
    (void)window; (void)xpos; (void)ypos; /* no settable cursor on a touchscreen */
}

void glfwSetInputMode(GLFWwindow *window, int mode, int value) { (void)window; (void)mode; (void)value; }
int  glfwGetInputMode(GLFWwindow *window, int mode) { (void)window; (void)mode; return 0; }

/* Callback registration — store + return previous (GLFW contract). */
#define NOVA_GLFW_SETCB(field, type) \
    type prev = s_win.field; s_win.field = callback; (void)window; return prev;

GLFWkeyfun glfwSetKeyCallback(GLFWwindow *window, GLFWkeyfun callback)                       { NOVA_GLFW_SETCB(cb_key, GLFWkeyfun) }
GLFWcharfun glfwSetCharCallback(GLFWwindow *window, GLFWcharfun callback)                    { NOVA_GLFW_SETCB(cb_char, GLFWcharfun) }
GLFWmousebuttonfun glfwSetMouseButtonCallback(GLFWwindow *window, GLFWmousebuttonfun callback) { NOVA_GLFW_SETCB(cb_mouse, GLFWmousebuttonfun) }
GLFWcursorposfun glfwSetCursorPosCallback(GLFWwindow *window, GLFWcursorposfun callback)     { NOVA_GLFW_SETCB(cb_cursorpos, GLFWcursorposfun) }
GLFWcursorenterfun glfwSetCursorEnterCallback(GLFWwindow *window, GLFWcursorenterfun callback) { NOVA_GLFW_SETCB(cb_cursorenter, GLFWcursorenterfun) }
GLFWscrollfun glfwSetScrollCallback(GLFWwindow *window, GLFWscrollfun callback)              { NOVA_GLFW_SETCB(cb_scroll, GLFWscrollfun) }
GLFWframebuffersizefun glfwSetFramebufferSizeCallback(GLFWwindow *window, GLFWframebuffersizefun callback) { NOVA_GLFW_SETCB(cb_fbsize, GLFWframebuffersizefun) }
GLFWwindowsizefun glfwSetWindowSizeCallback(GLFWwindow *window, GLFWwindowsizefun callback)  { NOVA_GLFW_SETCB(cb_winsize, GLFWwindowsizefun) }
GLFWwindowclosefun glfwSetWindowCloseCallback(GLFWwindow *window, GLFWwindowclosefun callback) { NOVA_GLFW_SETCB(cb_close, GLFWwindowclosefun) }
GLFWwindowfocusfun glfwSetWindowFocusCallback(GLFWwindow *window, GLFWwindowfocusfun callback) { NOVA_GLFW_SETCB(cb_focus, GLFWwindowfocusfun) }
GLFWwindowrefreshfun glfwSetWindowRefreshCallback(GLFWwindow *window, GLFWwindowrefreshfun callback) { NOVA_GLFW_SETCB(cb_refresh, GLFWwindowrefreshfun) }

/* =======================================================================
 * Joysticks / gamepads — the 3DS itself is GLFW_JOYSTICK_1.
 * ===================================================================== */
int glfwJoystickPresent(int jid)    { return jid == GLFW_JOYSTICK_1 ? 1 : 0; }
int glfwJoystickIsGamepad(int jid)  { return jid == GLFW_JOYSTICK_1 ? 1 : 0; }
const char *glfwGetJoystickName(int jid) { return jid == GLFW_JOYSTICK_1 ? "Nova_3DS" : (const char *)0; }
const char *glfwGetGamepadName(int jid)  { return jid == GLFW_JOYSTICK_1 ? "Nova_3DS" : (const char *)0; }
int glfwUpdateGamepadMappings(const char *string) { (void)string; return 1; }

static void fill_buttons(unsigned char out[15]) {
    u32 k = hidKeysHeld();
    out[GLFW_GAMEPAD_BUTTON_A]            = (k & KEY_B)      ? 1 : 0;
    out[GLFW_GAMEPAD_BUTTON_B]            = (k & KEY_A)      ? 1 : 0;
    out[GLFW_GAMEPAD_BUTTON_X]            = (k & KEY_Y)      ? 1 : 0;
    out[GLFW_GAMEPAD_BUTTON_Y]            = (k & KEY_X)      ? 1 : 0;
    out[GLFW_GAMEPAD_BUTTON_LEFT_BUMPER]  = (k & KEY_L)      ? 1 : 0;
    out[GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER] = (k & KEY_R)      ? 1 : 0;
    out[GLFW_GAMEPAD_BUTTON_BACK]         = (k & KEY_SELECT) ? 1 : 0;
    out[GLFW_GAMEPAD_BUTTON_START]        = (k & KEY_START)  ? 1 : 0;
    out[GLFW_GAMEPAD_BUTTON_GUIDE]        = 0;
    out[GLFW_GAMEPAD_BUTTON_LEFT_THUMB]   = 0;
    out[GLFW_GAMEPAD_BUTTON_RIGHT_THUMB]  = 0;
    out[GLFW_GAMEPAD_BUTTON_DPAD_UP]      = (k & KEY_DUP)    ? 1 : 0;
    out[GLFW_GAMEPAD_BUTTON_DPAD_RIGHT]   = (k & KEY_DRIGHT) ? 1 : 0;
    out[GLFW_GAMEPAD_BUTTON_DPAD_DOWN]    = (k & KEY_DDOWN)  ? 1 : 0;
    out[GLFW_GAMEPAD_BUTTON_DPAD_LEFT]    = (k & KEY_DLEFT)  ? 1 : 0;
}

static void fill_axes(float out[6]) {
    circlePosition cp, cs;
    hidCircleRead(&cp);
    hidCstickRead(&cs);
    u32 k = hidKeysHeld();
    out[GLFW_GAMEPAD_AXIS_LEFT_X]        = clampf_( cp.dx / CPAD_RANGE);
    out[GLFW_GAMEPAD_AXIS_LEFT_Y]        = clampf_(-cp.dy / CPAD_RANGE);
    out[GLFW_GAMEPAD_AXIS_RIGHT_X]       = clampf_( cs.dx / CPAD_RANGE);
    out[GLFW_GAMEPAD_AXIS_RIGHT_Y]       = clampf_(-cs.dy / CPAD_RANGE);
    out[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER]  = (k & KEY_ZL) ? 1.0f : -1.0f;
    out[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] = (k & KEY_ZR) ? 1.0f : -1.0f;
}

const unsigned char *glfwGetJoystickButtons(int jid, int *count) {
    static unsigned char buttons[15];
    if (jid != GLFW_JOYSTICK_1) { if (count) *count = 0; return (const unsigned char *)0; }
    fill_buttons(buttons);
    if (count) *count = 15;
    return buttons;
}

const float *glfwGetJoystickAxes(int jid, int *count) {
    static float axes[6];
    if (jid != GLFW_JOYSTICK_1) { if (count) *count = 0; return (const float *)0; }
    fill_axes(axes);
    if (count) *count = 6;
    return axes;
}

int glfwGetGamepadState(int jid, GLFWgamepadstate *state) {
    if (jid != GLFW_JOYSTICK_1 || state == (GLFWgamepadstate *)0) return 0;
    fill_buttons(state->buttons);
    fill_axes(state->axes);
    return 1;
}
