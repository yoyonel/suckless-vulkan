#version 450

layout(binding = 0) uniform GlobalUniforms {
    mat4 vp;
    mat4 modelRotation;
    mat4 invViewProj;
    vec4 cameraPosEnvLod;
}
ubo;

layout(binding = 1) uniform sampler2D environmentMap;

layout(location = 0) in vec3 rayDir;
layout(location = 0) out vec4 outColor;

const vec2 invAtan = vec2(0.15915494, 0.31830989);

vec2 sample_equirectangular(vec3 dir) {
    vec3 n = normalize(dir);
    float phi = (abs(n.z) < 1e-5 && abs(n.x) < 1e-5) ? 0.0 : atan(n.z, n.x);
    vec2 uv = vec2(phi, asin(clamp(n.y, -1.0, 1.0)));
    uv *= invAtan;
    uv.x += 0.5;
    uv.y = 0.5 - uv.y;
    return uv;
}

void main() {
    vec2 uv = sample_equirectangular(rayDir);
    float lod = max(0.0, ubo.cameraPosEnvLod.w);
    vec3 color = textureLod(environmentMap, uv, lod).rgb;
    color = min(max(color, vec3(0.0)), vec3(200.0));
    outColor = vec4(color, 1.0);
}
