#version 450

// Notre Uniform Buffer (Binding 0)
layout(binding = 0) uniform UniformBufferObject {
    mat4 mvp; // Model-View-Projection Matrix
}
ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

void main() {
    // On multiplie la position 2D (transformée en 4D) par notre matrice magique
    gl_Position = ubo.mvp * vec4(inPosition, 1.0);
    fragColor = inColor;
}
