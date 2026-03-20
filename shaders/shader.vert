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

// Attribut par instance (binding 1, rate = INSTANCE)
layout(location = 2) in vec3 instanceOffset;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec3 outAlbedo;
layout(location = 3) flat out int outMaterialIdx;

void main() {
    // Rotation locale de la sphère
    vec4 localPos = ubo.modelRotation * vec4(inPosition, 1.0);
    // Translation vers la position de l'instance dans le monde
    vec4 worldPos = localPos + vec4(instanceOffset, 0.0);
    gl_Position = ubo.vp * worldPos;

    outWorldPos = worldPos.xyz;
    // For a sphere centered at origin, normal is just the rotated local position
    outNormal = normalize(localPos.xyz);
    outAlbedo = inColor;
    outMaterialIdx = gl_InstanceIndex;
}
