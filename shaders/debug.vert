#version 450
#extension GL_GOOGLE_include_directive : enable
#include "billboard_utils.glsl"

layout(std430, set = 0, binding = 6) readonly buffer PosBuffer {
    vec4 instanceOffsets[];
};
layout(std430, set = 0, binding = 7) readonly buffer MatBuffer {
    int instanceMaterials[];
};
layout(std430, set = 0, binding = 8) readonly buffer IndexBuffer {
    uint sortedIndices[];
};

layout(set = 0, binding = 0) uniform UBO {
    mat4 vp;
    mat4 modelRotation;
    mat4 invViewProj;
    vec4 cameraPosEnvLod;
    vec4 debugParams;
    vec4 postParams1;
    vec4 postParams2;
    mat4 view;
    mat4 proj;
    vec4 windowSize;
}
ubo;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 color;
    float radius;
    int mode;     // 0: Box, 1: Billboard
    int stippled; // 0: No, 1: Stippled, 2: Fill
}
push;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) flat out int vMaterialIdx;

const vec3 box_verts[8] =
    vec3[8](vec3(-1, -1, -1), vec3(1, -1, -1), vec3(1, 1, -1), vec3(-1, 1, -1), vec3(-1, -1, 1), vec3(1, -1, 1), vec3(1, 1, 1), vec3(-1, 1, 1));

const int box_indices[24] = int[24](0, 1, 1, 2, 2, 3, 3, 0, // Bottom
                                    4, 5, 5, 6, 6, 7, 7, 4, // Top
                                    0, 4, 1, 5, 2, 6, 3, 7  // Verticals
);

void main() {
    uint inInstanceIdx = sortedIndices[gl_InstanceIndex];
    vec3 instancePos = instanceOffsets[inInstanceIdx].xyz;
    int instanceMaterialIdx = instanceMaterials[inInstanceIdx];

    vMaterialIdx = instanceMaterialIdx;
    if (push.mode == 0) {
        // AABB box
        vec3 pos = instancePos + box_verts[box_indices[gl_VertexIndex % 24]] * push.radius;
        gl_Position = ubo.proj * ubo.view * vec4(pos, 1.0);
        vWorldPos = pos;
    } else if (push.mode == 1) {
        // Billboard Quad
        vec2 quadPos[4] = vec2[](vec2(-0.5, -0.5), vec2(0.5, -0.5), vec2(0.5, 0.5), vec2(-0.5, 0.5));

        int idx;
        if (push.stippled == 2) { // Fill (Tris)
            const int tri_idx[6] = int[6](0, 1, 2, 2, 3, 0);
            idx = tri_idx[gl_VertexIndex % 6];
        } else { // Outline (Lines)
            const int quad_indices[8] = int[8](0, 1, 1, 2, 2, 3, 3, 0);
            idx = quad_indices[gl_VertexIndex % 8];
        }

        vec4 clipPos;
        computeBillboardSphere(quadPos[idx], instancePos, push.radius, ubo.view, ubo.proj, clipPos, vWorldPos);
        gl_Position = clipPos;
    }
}
