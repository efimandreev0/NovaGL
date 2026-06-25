#ifndef NOVA_GLFW_H
#define NOVA_GLFW_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GLFWAPI
#define GLFWAPI
#endif

#define GLFW_FALSE                        0
#define GLFW_TRUE                         1
#define GLFW_DONT_CARE                    (-1)

#define GLFW_JOYSTICK_1            0
#define GLFW_JOYSTICK_LAST         0

#define GLFW_GAMEPAD_BUTTON_A             0   // bottom
#define GLFW_GAMEPAD_BUTTON_B             1   // right
#define GLFW_GAMEPAD_BUTTON_X             2   // left
#define GLFW_GAMEPAD_BUTTON_Y             3   // top
#define GLFW_GAMEPAD_BUTTON_LEFT_BUMPER   4
#define GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER  5
#define GLFW_GAMEPAD_BUTTON_BACK          6
#define GLFW_GAMEPAD_BUTTON_START         7
#define GLFW_GAMEPAD_BUTTON_GUIDE         8
#define GLFW_GAMEPAD_BUTTON_LEFT_THUMB    9
#define GLFW_GAMEPAD_BUTTON_RIGHT_THUMB   10
#define GLFW_GAMEPAD_BUTTON_DPAD_UP       11
#define GLFW_GAMEPAD_BUTTON_DPAD_RIGHT    12
#define GLFW_GAMEPAD_BUTTON_DPAD_DOWN     13
#define GLFW_GAMEPAD_BUTTON_DPAD_LEFT     14
#define GLFW_GAMEPAD_BUTTON_LAST          GLFW_GAMEPAD_BUTTON_DPAD_LEFT
#define GLFW_GAMEPAD_BUTTON_CROSS         GLFW_GAMEPAD_BUTTON_A
#define GLFW_GAMEPAD_BUTTON_CIRCLE        GLFW_GAMEPAD_BUTTON_B
#define GLFW_GAMEPAD_BUTTON_SQUARE        GLFW_GAMEPAD_BUTTON_X
#define GLFW_GAMEPAD_BUTTON_TRIANGLE      GLFW_GAMEPAD_BUTTON_Y

#define GLFW_GAMEPAD_AXIS_LEFT_X          0
#define GLFW_GAMEPAD_AXIS_LEFT_Y          1
#define GLFW_GAMEPAD_AXIS_RIGHT_X         2
#define GLFW_GAMEPAD_AXIS_RIGHT_Y         3
#define GLFW_GAMEPAD_AXIS_LEFT_TRIGGER    4
#define GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER   5
#define GLFW_GAMEPAD_AXIS_LAST            GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER

#define GLFW_RELEASE                      0
#define GLFW_PRESS                        1
#define GLFW_REPEAT                       2

#define GLFW_MOD_SHIFT                    0x0001
#define GLFW_MOD_CONTROL                  0x0002
#define GLFW_MOD_ALT                      0x0004
#define GLFW_MOD_SUPER                    0x0008
#define GLFW_MOD_CAPS_LOCK                0x0010
#define GLFW_MOD_NUM_LOCK                 0x0020

#define GLFW_CURSOR                       0x00033001
#define GLFW_STICKY_KEYS                  0x00033002
#define GLFW_STICKY_MOUSE_BUTTONS         0x00033003
#define GLFW_CURSOR_NORMAL                0x00034001
#define GLFW_CURSOR_HIDDEN                0x00034002
#define GLFW_CURSOR_DISABLED              0x00034003

#define GLFW_MOUSE_BUTTON_1               0
#define GLFW_MOUSE_BUTTON_2               1
#define GLFW_MOUSE_BUTTON_3               2
#define GLFW_MOUSE_BUTTON_4               3
#define GLFW_MOUSE_BUTTON_5               4
#define GLFW_MOUSE_BUTTON_LEFT            GLFW_MOUSE_BUTTON_1
#define GLFW_MOUSE_BUTTON_RIGHT           GLFW_MOUSE_BUTTON_2
#define GLFW_MOUSE_BUTTON_MIDDLE          GLFW_MOUSE_BUTTON_3
#define GLFW_MOUSE_BUTTON_LAST            GLFW_MOUSE_BUTTON_5

#define GLFW_KEY_UNKNOWN                  (-1)
#define GLFW_KEY_SPACE                    32
#define GLFW_KEY_APOSTROPHE               39
#define GLFW_KEY_COMMA                    44
#define GLFW_KEY_MINUS                    45
#define GLFW_KEY_PERIOD                   46
#define GLFW_KEY_SLASH                    47
#define GLFW_KEY_0                        48
#define GLFW_KEY_1                        49
#define GLFW_KEY_2                        50
#define GLFW_KEY_3                        51
#define GLFW_KEY_4                        52
#define GLFW_KEY_5                        53
#define GLFW_KEY_6                        54
#define GLFW_KEY_7                        55
#define GLFW_KEY_8                        56
#define GLFW_KEY_9                        57
#define GLFW_KEY_SEMICOLON                59
#define GLFW_KEY_EQUAL                    61
#define GLFW_KEY_A                        65
#define GLFW_KEY_B                        66
#define GLFW_KEY_C                        67
#define GLFW_KEY_D                        68
#define GLFW_KEY_E                        69
#define GLFW_KEY_F                        70
#define GLFW_KEY_G                        71
#define GLFW_KEY_H                        72
#define GLFW_KEY_I                        73
#define GLFW_KEY_J                        74
#define GLFW_KEY_K                        75
#define GLFW_KEY_L                        76
#define GLFW_KEY_M                        77
#define GLFW_KEY_N                        78
#define GLFW_KEY_O                        79
#define GLFW_KEY_P                        80
#define GLFW_KEY_Q                        81
#define GLFW_KEY_R                        82
#define GLFW_KEY_S                        83
#define GLFW_KEY_T                        84
#define GLFW_KEY_U                        85
#define GLFW_KEY_V                        86
#define GLFW_KEY_W                        87
#define GLFW_KEY_X                        88
#define GLFW_KEY_Y                        89
#define GLFW_KEY_Z                        90
#define GLFW_KEY_LEFT_BRACKET             91
#define GLFW_KEY_BACKSLASH                92
#define GLFW_KEY_RIGHT_BRACKET            93
#define GLFW_KEY_GRAVE_ACCENT             96
#define GLFW_KEY_ESCAPE                   256
#define GLFW_KEY_ENTER                    257
#define GLFW_KEY_TAB                      258
#define GLFW_KEY_BACKSPACE                259
#define GLFW_KEY_INSERT                   260
#define GLFW_KEY_DELETE                   261
#define GLFW_KEY_RIGHT                    262
#define GLFW_KEY_LEFT                     263
#define GLFW_KEY_DOWN                     264
#define GLFW_KEY_UP                       265
#define GLFW_KEY_PAGE_UP                  266
#define GLFW_KEY_PAGE_DOWN                267
#define GLFW_KEY_HOME                     268
#define GLFW_KEY_END                      269
#define GLFW_KEY_CAPS_LOCK                280
#define GLFW_KEY_F1                       290
#define GLFW_KEY_F2                       291
#define GLFW_KEY_F3                       292
#define GLFW_KEY_F4                       293
#define GLFW_KEY_F5                       294
#define GLFW_KEY_F6                       295
#define GLFW_KEY_F7                       296
#define GLFW_KEY_F8                       297
#define GLFW_KEY_F9                       298
#define GLFW_KEY_F10                      299
#define GLFW_KEY_F11                      300
#define GLFW_KEY_F12                      301
#define GLFW_KEY_LEFT_SHIFT               340
#define GLFW_KEY_LEFT_CONTROL             341
#define GLFW_KEY_LEFT_ALT                 342
#define GLFW_KEY_LEFT_SUPER               343
#define GLFW_KEY_RIGHT_SHIFT             344
#define GLFW_KEY_RIGHT_CONTROL           345
#define GLFW_KEY_RIGHT_ALT               346
#define GLFW_KEY_RIGHT_SUPER             347
#define GLFW_KEY_MENU                     348
#define GLFW_KEY_LAST                     GLFW_KEY_MENU

#define GLFW_NO_ERROR                     0
#define GLFW_NOT_INITIALIZED              0x00010001
#define GLFW_NO_CURRENT_CONTEXT           0x00010002
#define GLFW_INVALID_ENUM                 0x00010003
#define GLFW_INVALID_VALUE                0x00010004
#define GLFW_OUT_OF_MEMORY                0x00010005
#define GLFW_API_UNAVAILABLE              0x00010006
#define GLFW_PLATFORM_ERROR               0x00010008

#define GLFW_FOCUSED                      0x00020001
#define GLFW_RESIZABLE                    0x00020003
#define GLFW_VISIBLE                      0x00020004
#define GLFW_DECORATED                    0x00020005
#define GLFW_AUTO_ICONIFY                 0x00020006
#define GLFW_FLOATING                     0x00020007
#define GLFW_MAXIMIZED                    0x00020008

#define GLFW_RED_BITS                     0x00021001
#define GLFW_GREEN_BITS                   0x00021002
#define GLFW_BLUE_BITS                    0x00021003
#define GLFW_ALPHA_BITS                   0x00021004
#define GLFW_DEPTH_BITS                   0x00021005
#define GLFW_STENCIL_BITS                 0x00021006
#define GLFW_SAMPLES                      0x0002100D
#define GLFW_DOUBLEBUFFER                 0x00021010

#define GLFW_CLIENT_API                   0x00022001
#define GLFW_CONTEXT_VERSION_MAJOR        0x00022002
#define GLFW_CONTEXT_VERSION_MINOR        0x00022003
#define GLFW_CONTEXT_REVISION             0x00022004
#define GLFW_OPENGL_FORWARD_COMPAT        0x00022006
#define GLFW_OPENGL_DEBUG_CONTEXT         0x00022007
#define GLFW_OPENGL_PROFILE               0x00022008

#define GLFW_NO_API                       0
#define GLFW_OPENGL_API                   0x00030001
#define GLFW_OPENGL_ES_API                0x00030002

#define GLFW_OPENGL_ANY_PROFILE           0
#define GLFW_OPENGL_CORE_PROFILE          0x00032001
#define GLFW_OPENGL_COMPAT_PROFILE        0x00032002

#define GLFW_CONNECTED                    0x00040001
#define GLFW_DISCONNECTED                 0x00040002

typedef struct GLFWwindow  GLFWwindow;
typedef struct GLFWmonitor GLFWmonitor;
typedef struct GLFWcursor  GLFWcursor;

typedef struct GLFWvidmode {
    int width;
    int height;
    int redBits;
    int greenBits;
    int blueBits;
    int refreshRate;
} GLFWvidmode;

typedef struct GLFWgamepadstate {
    unsigned char buttons[15];
    float         axes[6];
} GLFWgamepadstate;

typedef void (*GLFWglproc)(void);

typedef void (*GLFWerrorfun)(int error_code, const char *description);
typedef void (*GLFWkeyfun)(GLFWwindow *window, int key, int scancode, int action, int mods);
typedef void (*GLFWcharfun)(GLFWwindow *window, unsigned int codepoint);
typedef void (*GLFWmousebuttonfun)(GLFWwindow *window, int button, int action, int mods);
typedef void (*GLFWcursorposfun)(GLFWwindow *window, double xpos, double ypos);
typedef void (*GLFWcursorenterfun)(GLFWwindow *window, int entered);
typedef void (*GLFWscrollfun)(GLFWwindow *window, double xoffset, double yoffset);
typedef void (*GLFWframebuffersizefun)(GLFWwindow *window, int width, int height);
typedef void (*GLFWwindowsizefun)(GLFWwindow *window, int width, int height);
typedef void (*GLFWwindowclosefun)(GLFWwindow *window);
typedef void (*GLFWwindowfocusfun)(GLFWwindow *window, int focused);
typedef void (*GLFWwindowrefreshfun)(GLFWwindow *window);

GLFWAPI int          glfwInit(void);
GLFWAPI void         glfwTerminate(void);
GLFWAPI void         glfwGetVersion(int *major, int *minor, int *rev);
GLFWAPI const char  *glfwGetVersionString(void);
GLFWAPI GLFWerrorfun glfwSetErrorCallback(GLFWerrorfun callback);
GLFWAPI int          glfwGetError(const char **description);

GLFWAPI void         glfwDefaultWindowHints(void);
GLFWAPI void         glfwWindowHint(int hint, int value);
GLFWAPI void         glfwWindowHintString(int hint, const char *value);

GLFWAPI GLFWwindow  *glfwCreateWindow(int width, int height, const char *title,
                                      GLFWmonitor *monitor, GLFWwindow *share);
GLFWAPI void         glfwDestroyWindow(GLFWwindow *window);
GLFWAPI int          glfwWindowShouldClose(GLFWwindow *window);
GLFWAPI void         glfwSetWindowShouldClose(GLFWwindow *window, int value);
GLFWAPI void         glfwSetWindowTitle(GLFWwindow *window, const char *title);
GLFWAPI void         glfwGetWindowSize(GLFWwindow *window, int *width, int *height);
GLFWAPI void         glfwSetWindowSize(GLFWwindow *window, int width, int height);
GLFWAPI void         glfwGetFramebufferSize(GLFWwindow *window, int *width, int *height);
GLFWAPI void         glfwGetWindowPos(GLFWwindow *window, int *xpos, int *ypos);
GLFWAPI void         glfwSetWindowPos(GLFWwindow *window, int xpos, int ypos);
GLFWAPI void         glfwGetWindowContentScale(GLFWwindow *window, float *xscale, float *yscale);

GLFWAPI void         glfwMakeContextCurrent(GLFWwindow *window);
GLFWAPI GLFWwindow  *glfwGetCurrentContext(void);
GLFWAPI void         glfwSwapBuffers(GLFWwindow *window);
GLFWAPI void         glfwSwapInterval(int interval);
GLFWAPI int          glfwExtensionSupported(const char *extension);
GLFWAPI GLFWglproc   glfwGetProcAddress(const char *procname);

GLFWAPI void         glfwPollEvents(void);
GLFWAPI void         glfwWaitEvents(void);
GLFWAPI void         glfwWaitEventsTimeout(double timeout);
GLFWAPI void         glfwPostEmptyEvent(void);

GLFWAPI double       glfwGetTime(void);
GLFWAPI void         glfwSetTime(double time);
GLFWAPI unsigned long long glfwGetTimerValue(void);
GLFWAPI unsigned long long glfwGetTimerFrequency(void);

GLFWAPI GLFWmonitor      *glfwGetPrimaryMonitor(void);
GLFWAPI GLFWmonitor     **glfwGetMonitors(int *count);
GLFWAPI const GLFWvidmode *glfwGetVideoMode(GLFWmonitor *monitor);
GLFWAPI void              glfwGetMonitorPos(GLFWmonitor *monitor, int *xpos, int *ypos);

GLFWAPI int          glfwGetKey(GLFWwindow *window, int key);
GLFWAPI const char  *glfwGetKeyName(int key, int scancode);
GLFWAPI int          glfwGetMouseButton(GLFWwindow *window, int button);
GLFWAPI void         glfwGetCursorPos(GLFWwindow *window, double *xpos, double *ypos);
GLFWAPI void         glfwSetCursorPos(GLFWwindow *window, double xpos, double ypos);
GLFWAPI void         glfwSetInputMode(GLFWwindow *window, int mode, int value);
GLFWAPI int          glfwGetInputMode(GLFWwindow *window, int mode);

GLFWAPI GLFWkeyfun            glfwSetKeyCallback(GLFWwindow *window, GLFWkeyfun callback);
GLFWAPI GLFWcharfun           glfwSetCharCallback(GLFWwindow *window, GLFWcharfun callback);
GLFWAPI GLFWmousebuttonfun    glfwSetMouseButtonCallback(GLFWwindow *window, GLFWmousebuttonfun callback);
GLFWAPI GLFWcursorposfun      glfwSetCursorPosCallback(GLFWwindow *window, GLFWcursorposfun callback);
GLFWAPI GLFWcursorenterfun    glfwSetCursorEnterCallback(GLFWwindow *window, GLFWcursorenterfun callback);
GLFWAPI GLFWscrollfun         glfwSetScrollCallback(GLFWwindow *window, GLFWscrollfun callback);
GLFWAPI GLFWframebuffersizefun glfwSetFramebufferSizeCallback(GLFWwindow *window, GLFWframebuffersizefun callback);
GLFWAPI GLFWwindowsizefun     glfwSetWindowSizeCallback(GLFWwindow *window, GLFWwindowsizefun callback);
GLFWAPI GLFWwindowclosefun    glfwSetWindowCloseCallback(GLFWwindow *window, GLFWwindowclosefun callback);
GLFWAPI GLFWwindowfocusfun    glfwSetWindowFocusCallback(GLFWwindow *window, GLFWwindowfocusfun callback);
GLFWAPI GLFWwindowrefreshfun  glfwSetWindowRefreshCallback(GLFWwindow *window, GLFWwindowrefreshfun callback);

GLFWAPI int                   glfwJoystickPresent(int jid);
GLFWAPI int                   glfwJoystickIsGamepad(int jid);
GLFWAPI const char           *glfwGetJoystickName(int jid);
GLFWAPI const unsigned char  *glfwGetJoystickButtons(int jid, int *count);
GLFWAPI const float          *glfwGetJoystickAxes(int jid, int *count);
GLFWAPI int                   glfwGetGamepadState(int jid, GLFWgamepadstate *state);
GLFWAPI const char           *glfwGetGamepadName(int jid);
GLFWAPI int                   glfwUpdateGamepadMappings(const char *string);

#ifdef __cplusplus
}
#endif

#endif // NOVA_GLFW_H
