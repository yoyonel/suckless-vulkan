#version 450

layout(binding = 0) uniform GlobalUniforms {
    mat4 vp;
    mat4 modelRotation;
    mat4 invViewProj;
    vec4 cameraPosEnvLod;
    vec4 debugParams; // x: mode, y: scale
    vec4 postParams1; // x: exposure, y: saturation, z: contrast, w: gamma
    vec4 postParams2; // x: gain, y: offset, z: wbTemp, w: wbTint
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

// --- Post-Processing (Sync with shader.frag) ---

vec3 applyWhiteBalance(vec3 color) {
    float wb_temperature = ubo.postParams2.z;
    float wb_tint = ubo.postParams2.w;

    if (abs(wb_temperature - 6500.0) < 1.0 && abs(wb_tint) < 0.001) {
        return color;
    }

    float tempShift = (wb_temperature - 6500.0) / 10000.0;
    vec3 wbColor = vec3(1.0);

    if (tempShift < 0.0) {
        wbColor.b = 1.0 - tempShift;
    } else {
        wbColor.r = 1.0 + tempShift;
        wbColor.g = 1.0 + tempShift * 0.5;
    }
    wbColor.g += wb_tint * 0.5;

    return color * wbColor;
}

vec3 applyColorGrading(vec3 color) {
    color = applyWhiteBalance(color);

    float cg_saturation = ubo.postParams1.y;
    float cg_contrast = ubo.postParams1.z;
    float cg_gamma = ubo.postParams1.w;
    float cg_gain = ubo.postParams2.x;
    float cg_offset = ubo.postParams2.y;

    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luminance), color, cg_saturation);
    color = (color - 0.5) * cg_contrast + 0.5;
    color = max(vec3(0.0), color);
    if (cg_gamma > 0.001) {
        color = pow(color, vec3(cg_gamma));
    }
    color = color * cg_gain;
    color = color + cg_offset;

    return max(vec3(0.0), color);
}

vec3 unrealTonemap(vec3 x) {
    // Matches legacy `unrealTonemap()` exactly with Neutral defaults (slope=1.0, toe=0, shoulder=0)
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.0;
    const float e = 0.154;

    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec2 uv = sample_equirectangular(rayDir);
    float lod = max(0.0, ubo.cameraPosEnvLod.w);
    vec3 color = textureLod(environmentMap, uv, lod).rgb;

    // Sanitize
    color = max(color, vec3(0.0));
    color = min(color, vec3(200.0));

    // Pipeline
    color *= ubo.postParams1.x; // Exposure
    color = applyColorGrading(color);
    // color = unrealTonemap(color);

    outColor = vec4(color, 1.0);
}
