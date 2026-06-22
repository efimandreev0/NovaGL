#include <3ds.h>

#include "nova_glfw.h"

#define CPAD_RANGE 156.0f

static float clampf(float v) {
	if (v < -1.0f) return -1.0f;
	if (v >  1.0f) return  1.0f;
	return v;
}

int glfwJoystickPresent(int jid) {
	return jid == GLFW_JOYSTICK_1 ? 1 : 0;
}

int glfwJoystickIsGamepad(int jid) {
	return jid == GLFW_JOYSTICK_1 ? 1 : 0;
}

const char *glfwGetJoystickName(int jid) {
	return jid == GLFW_JOYSTICK_1 ? "Nova_3DS" : (const char *)0;
}

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

	out[GLFW_GAMEPAD_AXIS_LEFT_X]        = clampf( cp.dx / CPAD_RANGE);
	out[GLFW_GAMEPAD_AXIS_LEFT_Y]        = clampf(-cp.dy / CPAD_RANGE);
	out[GLFW_GAMEPAD_AXIS_RIGHT_X]       = clampf( cs.dx / CPAD_RANGE);
	out[GLFW_GAMEPAD_AXIS_RIGHT_Y]       = clampf(-cs.dy / CPAD_RANGE);
	out[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER]  = (k & KEY_ZL) ? 1.0f : -1.0f;
	out[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] = (k & KEY_ZR) ? 1.0f : -1.0f;
}

const unsigned char *glfwGetJoystickButtons(int jid, int *count) {
	static unsigned char buttons[15];
	if (jid != GLFW_JOYSTICK_1) {
		if (count) *count = 0;
		return (const unsigned char *)0;
	}
	fill_buttons(buttons);
	if (count) *count = 15;
	return buttons;
}

const float *glfwGetJoystickAxes(int jid, int *count) {
	static float axes[6];
	if (jid != GLFW_JOYSTICK_1) {
		if (count) *count = 0;
		return (const float *)0;
	}
	fill_axes(axes);
	if (count) *count = 6;
	return axes;
}

int glfwGetGamepadState(int jid, GLFWgamepadstate *state) {
	if (jid != GLFW_JOYSTICK_1 || state == (GLFWgamepadstate *)0)
		return 0;
	fill_buttons(state->buttons);
	fill_axes(state->axes);
	return 1;
}

void glfwGetCursorPos(void *window, double *xpos, double *ypos) {
	(void)window;
	if (xpos) *xpos = 0.0;
	if (ypos) *ypos = 0.0;
}

void glfwSetCursorPos(void *window, double xpos, double ypos) {
	(void)window; (void)xpos; (void)ypos;
}

int glfwGetMouseButton(void *window, int button) {
	(void)window; (void)button;
	return GLFW_RELEASE;
}

void glfwSetInputMode(void *window, int mode, int value) {
	(void)window; (void)mode; (void)value;
}
