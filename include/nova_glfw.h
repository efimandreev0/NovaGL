#ifndef NOVA_GLFW_H
#define NOVA_GLFW_H

#ifdef __cplusplus
extern "C" {
#endif

#define GLFW_JOYSTICK_1            0
#define GLFW_JOYSTICK_LAST         0   // one virtual pad (the 3DS itself)

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

#define GLFW_CURSOR                       0x00033001
#define GLFW_CURSOR_NORMAL                0x00034001
#define GLFW_CURSOR_HIDDEN                0x00034002
#define GLFW_CURSOR_DISABLED              0x00034003

#define GLFW_MOUSE_BUTTON_LEFT            0
#define GLFW_MOUSE_BUTTON_RIGHT           1
#define GLFW_MOUSE_BUTTON_MIDDLE          2
#define GLFW_MOUSE_BUTTON_4               3
#define GLFW_MOUSE_BUTTON_5               4

typedef struct GLFWgamepadstate {
	unsigned char buttons[15];
	float         axes[6];
} GLFWgamepadstate;

int                   glfwJoystickPresent(int jid);
int                   glfwJoystickIsGamepad(int jid);
const char           *glfwGetJoystickName(int jid);
const unsigned char  *glfwGetJoystickButtons(int jid, int *count);
const float          *glfwGetJoystickAxes(int jid, int *count);
int                   glfwGetGamepadState(int jid, GLFWgamepadstate *state);

// stubs
void   glfwGetCursorPos(void *window, double *xpos, double *ypos);
void   glfwSetCursorPos(void *window, double xpos, double ypos);
int    glfwGetMouseButton(void *window, int button);
void   glfwSetInputMode(void *window, int mode, int value);

#ifdef __cplusplus
}
#endif

#endif // NOVA_GLFW_H
