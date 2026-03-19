#version 450
#extension GL_GOOGLE_include_directive : enable

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inSphereCenter;
layout(location = 2) in float inSphereRadius;
layout(location = 3) flat in int inMaterialIdx;
layout(location = 4) in vec4 inCurrentClipPos;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform UniformBufferObject {
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

// --- SHARED PBR LOGIC (EMBEDDED FOR SIMPLICITY) ---
// Note: Ideally these would be in a common.glsl but we stay minimal.

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

vec3 unrealTonemap(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.0;
    const float e = 0.154;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// ----------------------------------------------------------------------------
// Ray-Sphere Intersection
// ----------------------------------------------------------------------------
bool intersectSphere(vec3 ro, vec3 rd, vec3 center, float radius, out float t, out vec3 normal, out float h, out bool isInside) {
    vec3 oc = ro - center;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - radius * radius;
    float h2 = b * b - c;

    h = h2;
    isInside = (c < 0.0);

    if (h2 < 0.0)
        return false;

    float sqrtH = sqrt(h2);
    float t1 = -b - sqrtH;
    float t2 = -b + sqrtH;

    if (t1 >= 0.0)
        t = t1;
    else if (t2 >= 0.0)
        t = t2;
    else
        return false;

    vec3 hitPos = ro + t * rd;
    normal = normalize(hitPos - center);
    return true;
}

void main() {
    vec3 camPos = ubo.cameraPosEnvLod.xyz;
    vec3 rayDir = normalize(inWorldPos - camPos);
    vec3 rayOrigin = camPos;

    float t;
    vec3 N;
    float h;
    bool isInside;

    if (!intersectSphere(rayOrigin, rayDir, inSphereCenter, inSphereRadius, t, N, h, isInside)) {
        discard;
    }

    // Reconstruction of exact hit position
    vec3 sphereHitPos = rayOrigin + t * rayDir;

    // Depth correction (Vulkan [0,1] range)
    vec4 clipPosActual = ubo.vp * vec4(sphereHitPos, 1.0);
    gl_FragDepth = clipPosActual.z / clipPosActual.w;

    // Lighting / Shading
    vec3 V = -rayDir;
    vec3 R = reflect(-V, N);

    Material mat = materials[inMaterialIdx];
    vec3 albedo = mat.albedoMetallic.rgb;
    float metallic = mat.albedoMetallic.a;
    float roughness = max(mat.roughnessAoPad.x, 0.04);
    float ao = mat.roughnessAoPad.y;

    float NdotV = max(dot(N, V), 0.0);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = fresnelSchlickRoughness(NdotV, F0, roughness);

    // --- IBL ---
    vec3 irradiance = textureLod(irradianceMap, dirToUV(N), 0.0).rgb;
    vec3 diffuse = max(irradiance, vec3(0.0)) * albedo;

    const float MAX_REFLECTION_LOD = 4.0;
    vec3 prefilteredColor = textureLod(prefilterMap, dirToUV(R), roughness * MAX_REFLECTION_LOD).rgb;
    prefilteredColor = max(prefilteredColor, vec3(0.0));

    vec2 texSize = vec2(textureSize(brdfLUT, 0));
    vec2 brdfUV = vec2(NdotV, roughness) * (texSize - 1.0) / texSize + 0.5 / texSize;
    vec2 brdf = texture(brdfLUT, brdfUV).rg;

    vec3 FssEss = F * brdf.x + brdf.y;
    vec3 Favg = F0 + (1.0 - F0) * (1.0 / 21.0);
    float Ess = brdf.x + brdf.y;
    vec3 Fms = Favg * FssEss / max(1.0 - Favg * (1.0 - Ess), 1e-6);
    vec3 multipleScattering = Fms * (1.0 - Ess);

    vec3 specular = prefilteredColor * (FssEss + multipleScattering);
    vec3 kD = (1.0 - (FssEss + multipleScattering)) * (1.0 - metallic);
    vec3 color = (kD * diffuse + specular) * ao;

    // Debug Modes
    int debugMode = int(ubo.debugParams.x + 0.5);
    if (debugMode != 0) {
        if (debugMode == 1)
            color = albedo;
        else if (debugMode == 2)
            color = N * 0.5 + 0.5;
        else if (debugMode == 3)
            color = vec3(metallic);
        else if (debugMode == 4)
            color = vec3(roughness);
        else if (debugMode == 5)
            color = vec3(ao);
        else if (debugMode == 6)
            color = irradiance * ubo.debugParams.y;
        else if (debugMode == 7)
            color = prefilteredColor * ubo.debugParams.y;
        else if (debugMode == 8)
            color = vec3(brdf, 0.0);
    }

    // Post-Processing
    color *= ubo.postParams1.x; // Exposure
    // Simplified Color Grading (Saturation here for brevity, full parity in main fragment if needed)
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luma), color, ubo.postParams1.y);

    // Tonemapping
    color = unrealTonemap(color);

    // Analytic Edge Smoothing (AA)
    // h is the discriminant (r^2 - d^2)
    float pixelSizeWorld = (2.0 * inCurrentClipPos.w) / (abs(ubo.proj[1][1]) * ubo.windowSize.y);
    float analyticFwidthH = 2.0 * inSphereRadius * pixelSizeWorld;
    float edgeFactor = clamp(h / max(analyticFwidthH, 1e-4), 0.0, 1.0);
    edgeFactor = smoothstep(0.0, 1.0, edgeFactor);

    if (!isInside) {
        color *= edgeFactor;
    }

    outColor = vec4(color, 1.0);
}
