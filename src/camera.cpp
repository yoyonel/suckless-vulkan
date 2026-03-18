#include "camera.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kDefaultYaw = -90.0f;
constexpr float kDefaultPitch = 0.0f;
constexpr float kDefaultDistance = 20.0f;
constexpr float kDefaultSpeed = 15.0f;
constexpr float kDefaultSensitivity = 0.15f;
constexpr float kDefaultZoom = 60.0f;
constexpr float kDefaultRotationSmoothing = 0.18f;
constexpr float kDefaultAcceleration = 10.0f;
constexpr float kDefaultFriction = 0.85f;
constexpr float kDefaultScrollSensitivity = 50.0f;
constexpr float kMinVelocity = 0.01f;
constexpr float kPitchMax = 89.0f;
constexpr float kPitchMin = -89.0f;

} // namespace

void camera_init(Camera* camera) {
    camera->position = glm::vec3(0.0f, 0.0f, kDefaultDistance);
    camera->worldUp = glm::vec3(0.0f, 1.0f, 0.0f);

    camera->yaw = kDefaultYaw;
    camera->pitch = kDefaultPitch;
    camera->movementSpeed = kDefaultSpeed;
    camera->mouseSensitivity = kDefaultSensitivity;
    camera->zoom = kDefaultZoom;

    camera->yawTarget = camera->yaw;
    camera->pitchTarget = camera->pitch;
    camera->rotationSmoothing = kDefaultRotationSmoothing;

    // Initialize kinetic physics
    camera->velocityCurrent = glm::vec3(0.0f, 0.0f, 0.0f);
    camera->acceleration = kDefaultAcceleration;
    camera->friction = kDefaultFriction;

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
    // Add scroll impulse to velocity in the direction camera is facing
    glm::vec3 impulse = camera->front * (yoffset * kDefaultScrollSensitivity);
    camera->velocityCurrent += impulse;
}

void camera_fixed_update(Camera* camera, float deltaSeconds) {
    // 1. Calculate target velocity based on WASD input
    glm::vec3 targetVelocity(0.0f, 0.0f, 0.0f);

    if (camera->moveForward) {
        targetVelocity += camera->front * camera->movementSpeed;
    }
    if (camera->moveBackward) {
        targetVelocity -= camera->front * camera->movementSpeed;
    }
    if (camera->moveLeft) {
        targetVelocity -= camera->right * camera->movementSpeed;
    }
    if (camera->moveRight) {
        targetVelocity += camera->right * camera->movementSpeed;
    }
    if (camera->moveUp) {
        targetVelocity += camera->worldUp * camera->movementSpeed;
    }
    if (camera->moveDown) {
        targetVelocity -= camera->worldUp * camera->movementSpeed;
    }

    // 2. Interpolate current velocity towards target velocity
    //    Alpha is based on acceleration and deltaTime
    float alpha = std::min(camera->acceleration * deltaSeconds, 1.0f);
    camera->velocityCurrent = glm::mix(camera->velocityCurrent, targetVelocity, alpha);

    // 3. Apply friction when no input is present (dampen momentum)
    float targetMagnitude = glm::length(targetVelocity);
    if (targetMagnitude < kMinVelocity) {
        camera->velocityCurrent *= camera->friction;
    }

    // 4. Integrate position based on current velocity
    camera->position += camera->velocityCurrent * deltaSeconds;

    // 5. Smooth rotation (interpolate towards target orientation)
    const float rotAlpha = std::clamp(camera->rotationSmoothing, 0.0f, 1.0f);
    camera->yaw += (camera->yawTarget - camera->yaw) * rotAlpha;
    camera->pitch += (camera->pitchTarget - camera->pitch) * rotAlpha;
    camera_update_vectors(camera);
}
