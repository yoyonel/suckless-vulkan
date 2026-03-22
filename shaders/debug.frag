#version 450

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) flat in int vMaterialIdx;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 color;
    float radius;
    int mode;
    int stippled;
}
push;

layout(location = 0) out vec4 FragColor;

struct Material {
    vec4 albedoMetallic;
    vec4 roughnessAoPad;
};

layout(std140, set = 0, binding = 5) readonly buffer MaterialBuffer {
    Material materials[];
};

void main() {
    if (push.stippled == 1) {
        float pattern = vWorldPos.x + vWorldPos.y + vWorldPos.z;
        if (sin(pattern * 50.0) < 0.0) {
            discard;
        }
    }

    vec4 finalColor = push.color;
    if (push.stippled == 2) { // Fill mode: use instance albedo with requested alpha
        finalColor = vec4(materials[vMaterialIdx].albedoMetallic.rgb, push.color.a);
    }

    FragColor = finalColor;
}
