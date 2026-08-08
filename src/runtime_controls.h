#ifndef RUNTIME_CONTROLS_H
#define RUNTIME_CONTROLS_H

#include "engine_state.h"
#include <GLFW/glfw3.h>

struct WindowOps {
    int (*get_key)(GLFWwindow* window, int key);
    void (*set_window_should_close)(GLFWwindow* window, int value);
    GLFWmonitor* (*get_primary_monitor)();
    const GLFWvidmode* (*get_video_mode)(GLFWmonitor* monitor);
    void (*get_window_pos)(GLFWwindow* window, int* xpos, int* ypos);
    void (*get_window_size)(GLFWwindow* window, int* width, int* height);
    void (*set_window_monitor)(GLFWwindow* window, GLFWmonitor* monitor, int xpos, int ypos, int width, int height, int refresh_rate);
};

const WindowOps* runtime_default_window_ops();
bool runtime_is_key_pressed_once(GLFWwindow* window, int key, bool* wasDown, const WindowOps* ops);
bool runtime_toggle_fullscreen(EngineState* state, const WindowOps* ops);
void runtime_update_controls(EngineState* state, const WindowOps* ops);

#endif
