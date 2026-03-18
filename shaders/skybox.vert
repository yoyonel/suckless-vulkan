#version 450

layout(binding = 0) uniform GlobalUniforms {
    mat4 vp;
    mat4 modelRotation;
    mat4 invViewProj;
    vec4 cameraPosEnvLod;
}
ubo;

layout(location = 0) out vec3 rayDir;

vec2 fullscreen_triangle_position(uint index) {
    if (index == 0u) {
        return vec2(-1.0, -1.0);
    }
    if (index == 1u) {
        return vec2(3.0, -1.0);
    }
    return vec2(-1.0, 3.0);
}

void main() {
    vec2 ndc = fullscreen_triangle_position(gl_VertexIndex);
    gl_Position = vec4(ndc, 1.0, 1.0);

    vec4 world = ubo.invViewProj * vec4(ndc, 1.0, 1.0);
    rayDir = world.xyz / world.w;
}
