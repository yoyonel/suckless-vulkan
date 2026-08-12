#version 450

// UBO (Binding 0) : View-Projection + rotation commune à toutes les sphères
layout(binding = 0) uniform UniformBufferObject {
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

// Attributs par vertex (binding 0, rate = VERTEX)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec3 outAlbedo;
layout(location = 3) flat out int outMaterialIdx;

layout(std430, set = 0, binding = 9) readonly buffer TransformBuffer {
    vec4 instancePositions[];
};

void main() {
    vec3 pos = instancePositions[gl_InstanceIndex].xyz;
    mat4 modelMat = ubo.modelRotation;
    modelMat[3][0] = pos.x;
    modelMat[3][1] = pos.y;
    modelMat[3][2] = pos.z;

    vec4 worldPos = modelMat * vec4(inPosition, 1.0);
    gl_Position = ubo.vp * worldPos;

    outWorldPos = worldPos.xyz;
    outNormal = normalize(mat3(modelMat) * inPosition);
    outAlbedo = inColor;
    outMaterialIdx = gl_InstanceIndex;
}
