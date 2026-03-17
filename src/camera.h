#ifndef CAMERA_H
#define CAMERA_H

#include <glm/glm.hpp>

struct Camera {
    glm::vec3 position;
    glm::vec3 front;
    glm::vec3 up;
    glm::vec3 right;
    glm::vec3 worldUp;

    float yaw;
    float pitch;
    float movementSpeed;
    float mouseSensitivity;
    float zoom;

    float yawTarget;
    float pitchTarget;
    float rotationSmoothing;

    bool moveForward;
    bool moveBackward;
    bool moveLeft;
    bool moveRight;
    bool moveUp;
    bool moveDown;

    double lastMouseX;
    double lastMouseY;
    bool firstMouse;
};

void camera_init(Camera* camera);
void camera_update_vectors(Camera* camera);
void camera_process_mouse(Camera* camera, float xoffset, float yoffset);
void camera_process_scroll(Camera* camera, float yoffset);
void camera_fixed_update(Camera* camera, float deltaSeconds);

#endif
