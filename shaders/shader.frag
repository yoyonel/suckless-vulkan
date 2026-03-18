#version 450

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inAlbedo;
layout(location = 3) flat in int inMaterialIdx;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 vp;
    mat4 modelRotation;
    mat4 invViewProj;
    vec4 cameraPosEnvLod;
    vec4 debugParams; // x: mode, y: scale
    vec4 postParams1; // x: exposure, y: saturation, z: contrast, w: gamma
    vec4 postParams2; // x: gain, y: offset, z: wbTemp, w: wbTint
}
ubo;

layout(set = 0, binding = 1) uniform sampler2D envMap;
layout(set = 0, binding = 2) uniform sampler2D irradianceMap;
layout(set = 0, binding = 3) uniform sampler2D prefilterMap;
layout(set = 0, binding = 4) uniform sampler2D brdfLUT;

struct Material {
    vec4 albedoMetallic;
    vec4 roughnessAoPad;
};

layout(std140, set = 0, binding = 5) readonly buffer MaterialBuffer {
    Material materials[];
};

const float PI = 3.14159265359;

// ============================================================================
// Post-Processing Functions (Legacy Parity)
// ============================================================================

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

    // 1. Saturation
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luminance), color, cg_saturation);

    // 2. Contrast
    color = (color - 0.5) * cg_contrast + 0.5;
    color = max(vec3(0.0), color);

    // 3. Gamma
    if (cg_gamma > 0.001) {
        color = pow(color, vec3(cg_gamma));
    }

    // 4. Gain
    color = color * cg_gain;

    // 5. Offset
    color = color + cg_offset;

    return max(vec3(0.0), color);
}

vec3 unrealTonemap(vec3 x) {
    // Matches legacy `unrealTonemap()` exactly with Neutral defaults (slope=1.0, toe=0, shoulder=0)
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.0;   // DEFAULT_FILMIC_SHOULDER = 0.0
    const float e = 0.154; // 0.14 * (1.1 - DEFAULT_FILMIC_TOE) = 0.14 * 1.1

    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// ============================================================================
// Gamma Correction — NOT APPLIED HERE
// Swapchain is VK_FORMAT_B8G8R8A8_SRGB: hardware applies linear→sRGB
// on framebuffer write automatically.
// ============================================================================

// Helper: Direction to Equirectangular UV
vec2 dirToUV(vec3 v) {
    float phi = (abs(v.z) < 1e-5 && abs(v.x) < 1e-5) ? 0.0 : atan(v.z, v.x);
    vec2 uv = vec2(phi, asin(clamp(v.y, -1.0, 1.0)));
    uv *= vec2(0.1591, 0.3183);
    uv.x += 0.5;
    uv.y = 0.5 - uv.y;
    return uv;
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    float f = pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * f;
}

void main() {
    vec3 N = normalize(inNormal);
    vec3 V = normalize(ubo.cameraPosEnvLod.xyz - inWorldPos);
    vec3 R = reflect(-V, N);

    Material mat = materials[inMaterialIdx];
    vec3 albedo = mat.albedoMetallic.rgb;
    float metallic = mat.albedoMetallic.a;
    float roughness = max(mat.roughnessAoPad.x, 0.04);
    float ao = mat.roughnessAoPad.y;

    float NdotV = max(dot(N, V), 0.0);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 F = fresnelSchlickRoughness(NdotV, F0, roughness);

    // --- DIFFUSE IBL ---
    vec3 irradiance = textureLod(irradianceMap, dirToUV(N), 0.0).rgb;
    irradiance = max(irradiance, vec3(0.0));
    vec3 diffuse = irradiance * albedo;

    // --- SPECULAR IBL (Split-Sum) ---
    const float MAX_REFLECTION_LOD = 4.0;
    vec3 prefilteredColor = textureLod(prefilterMap, dirToUV(R), roughness * MAX_REFLECTION_LOD).rgb;
    prefilteredColor = max(prefilteredColor, vec3(0.0));

    // BRDF LUT lookup with half-pixel offset
    vec2 texSize = vec2(textureSize(brdfLUT, 0));
    vec2 brdfUV = vec2(NdotV, roughness) * (texSize - 1.0) / texSize + 0.5 / texSize;
    vec2 brdf = texture(brdfLUT, brdfUV).rg;

    // --- COMPENSATION MULTIPLE SCATTERING (OGL ISO) ---
    vec3 FssEss = F * brdf.x + brdf.y;
    vec3 Favg = F0 + (1.0 - F0) * (1.0 / 21.0);
    float Ess = brdf.x + brdf.y;
    vec3 Fms = Favg * FssEss / max(1.0 - Favg * (1.0 - Ess), 1e-6);
    vec3 multipleScattering = Fms * (1.0 - Ess);

    vec3 specular = prefilteredColor * (FssEss + multipleScattering);

    // Final Energy Conservation
    vec3 kD = (1.0 - (FssEss + multipleScattering)) * (1.0 - metallic);
    vec3 fullPBR = (kD * diffuse + specular) * ao;

    int debugMode = int(ubo.debugParams.x + 0.5);
    vec3 color = fullPBR;

    // Replay legacy debug modes (0-9)
    if (debugMode != 0) {
        if (debugMode == 1) { // Albedo
            color = mat.albedoMetallic.rgb;
        } else if (debugMode == 2) { // Normal
            color = N * 0.5 + 0.5;
        } else if (debugMode == 3) { // Metallic
            color = vec3(mat.albedoMetallic.a);
        } else if (debugMode == 4) { // Roughness
            color = vec3(mat.roughnessAoPad.x);
        } else if (debugMode == 5) { // AO
            color = vec3(mat.roughnessAoPad.y);
        } else if (debugMode == 6) { // Irradiance (Diff)
            color = textureLod(irradianceMap, dirToUV(N), 0.0).rgb * ubo.debugParams.y;
        } else if (debugMode == 7) { // Prefilter (Spec)
            vec3 R_debug = reflect(-V, N);
            float roughness_debug = mat.roughnessAoPad.x;
            color = textureLod(prefilterMap, dirToUV(R_debug), roughness_debug * MAX_REFLECTION_LOD).rgb * ubo.debugParams.y;
        } else if (debugMode == 8) { // BRDF LUT
            float NdotV_debug = max(dot(N, V), 0.0);
            float roughness_debug = mat.roughnessAoPad.x;
            vec2 texSize = vec2(textureSize(brdfLUT, 0));
            vec2 brdfUV = vec2(NdotV_debug, roughness_debug) * (texSize - 1.0) / texSize + 0.5 / texSize;
            color = vec3(texture(brdfLUT, brdfUV).rg, 0.0);
        } else if (debugMode == 9) { // 1-Bounce GI (Probes)
            color = vec3(0.0);       // Not implemented yet
        }
    }

    // ========================================================================
    // Post-Processing Pipeline (Linear Space)
    // ========================================================================

    // 1. Exposure
    float exposure = ubo.postParams1.x;
    color *= exposure;

    // 2. Color Grading (includes White Balance)
    color = applyColorGrading(color);

    // 3. Tone Mapping (HDR → LDR) — DISABLED by default in Legacy OGL-ISO
    color = unrealTonemap(color);

    outColor = vec4(color, 1.0);
}
