#include "camera.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kDefaultYaw = -90.0f;
constexpr float kDefaultPitch = 0.0f;
constexpr float kDefaultSpeed = 12.0f;
constexpr float kDefaultSensitivity = 0.12f;
constexpr float kDefaultZoom = 45.0f;
constexpr float kDefaultRotationSmoothing = 0.2f;
constexpr float kPitchMax = 89.0f;
constexpr float kPitchMin = -89.0f;

} // namespace

void camera_init(Camera* camera) {
    camera->position = glm::vec3(0.0f, 0.0f, 40.0f);
    camera->worldUp = glm::vec3(0.0f, 1.0f, 0.0f);

    camera->yaw = kDefaultYaw;
    camera->pitch = kDefaultPitch;
    camera->movementSpeed = kDefaultSpeed;
    camera->mouseSensitivity = kDefaultSensitivity;
    camera->zoom = kDefaultZoom;

    camera->yawTarget = camera->yaw;
    camera->pitchTarget = camera->pitch;
    camera->rotationSmoothing = kDefaultRotationSmoothing;

    camera->moveForward = false;
    camera->moveBackward = false;
    camera->moveLeft = false;
    camera->moveRight = false;
    camera->moveUp = false;
    camera->moveDown = false;

    camera->lastMouseX = 0.0;
    camera->lastMouseY = 0.0;
    camera->firstMouse = true;

    camera_update_vectors(camera);
}

void camera_update_vectors(Camera* camera) {
    glm::vec3 front;
    front.x = std::cos(glm::radians(camera->yaw)) * std::cos(glm::radians(camera->pitch));
    front.y = std::sin(glm::radians(camera->pitch));
    front.z = std::sin(glm::radians(camera->yaw)) * std::cos(glm::radians(camera->pitch));

    camera->front = glm::normalize(front);
    camera->right = glm::normalize(glm::cross(camera->front, camera->worldUp));
    camera->up = glm::normalize(glm::cross(camera->right, camera->front));
}

void camera_process_mouse(Camera* camera, float xoffset, float yoffset) {
    camera->yawTarget += xoffset * camera->mouseSensitivity;
    camera->pitchTarget -= yoffset * camera->mouseSensitivity;
    camera->pitchTarget = std::clamp(camera->pitchTarget, kPitchMin, kPitchMax);
}

void camera_process_scroll(Camera* camera, float yoffset) {
    camera->zoom -= yoffset;
    camera->zoom = std::clamp(camera->zoom, 20.0f, 80.0f);
}

void camera_fixed_update(Camera* camera, float deltaSeconds) {
    const float velocity = camera->movementSpeed * deltaSeconds;

    if (camera->moveForward) {
        camera->position += camera->front * velocity;
    }
    if (camera->moveBackward) {
        camera->position -= camera->front * velocity;
    }
    if (camera->moveLeft) {
        camera->position -= camera->right * velocity;
    }
    if (camera->moveRight) {
        camera->position += camera->right * velocity;
    }
    if (camera->moveUp) {
        camera->position += camera->worldUp * velocity;
    }
    if (camera->moveDown) {
        camera->position -= camera->worldUp * velocity;
    }

    const float alpha = std::clamp(camera->rotationSmoothing, 0.0f, 1.0f);
    camera->yaw += (camera->yawTarget - camera->yaw) * alpha;
    camera->pitch += (camera->pitchTarget - camera->pitch) * alpha;
    camera_update_vectors(camera);
}
