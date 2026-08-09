#version 450
#extension GL_GOOGLE_include_directive : enable

#include "billboard_utils.glsl"

layout(binding = 0) uniform UniformBufferObject {
    mat4 vp;
    mat4 modelRotation;
    mat4 invViewProj;
    vec4 cameraPosEnvLod;
    vec4 debugParams; // x: mode, y: scale, z: billboardMode
    vec4 postParams1; // x: exposure, y: saturation, z: contrast, w: gamma
    vec4 postParams2; // x: gain, y: offset, z: wbTemp, w: wbTint
    mat4 view;
    mat4 proj;
}
ubo;

layout(std430, set = 0, binding = 6) readonly buffer PosBuffer {
    vec4 instanceOffsets[];
};
layout(std430, set = 0, binding = 7) readonly buffer MatBuffer {
    int instanceMaterials[];
};
layout(std430, set = 0, binding = 8) readonly buffer IndexBuffer {
    uint sortedIndices[];
};

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outSphereCenter;
layout(location = 2) out float outSphereRadius;
layout(location = 3) flat out int outMaterialIdx;
layout(location = 4) out vec4 outCurrentClipPos;

void main() {
    vec2 quadPos[6] = vec2[](vec2(-0.5, -0.5), vec2(0.5, -0.5), vec2(-0.5, 0.5), vec2(-0.5, 0.5), vec2(0.5, -0.5), vec2(0.5, 0.5));

    vec2 pos = quadPos[gl_VertexIndex % 6];

    uint inInstanceIdx = sortedIndices[gl_InstanceIndex];
    outSphereCenter = instanceOffsets[inInstanceIdx].xyz;
    outSphereRadius = 1.0;
    outMaterialIdx = instanceMaterials[inInstanceIdx];

    vec4 clipPos;
    computeBillboardSphere(pos, outSphereCenter, outSphereRadius, ubo.view, ubo.proj, clipPos, outWorldPos);

    outCurrentClipPos = clipPos;
    gl_Position = clipPos;
}
