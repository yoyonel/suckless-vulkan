#version 450 core

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D u_scene_hdr;
layout(set = 0, binding = 1) uniform sampler2D u_bloom_texture;
layout(set = 0, binding = 2) uniform sampler2D u_autoexposure_texture;
layout(std430, set = 0, binding = 3) readonly buffer DebugHistogramBuffer {
    uint bins[64];
}
u_debug_histogram;

layout(push_constant) uniform PostProcessPushConstants {
    float u_bloom_intensity;
    int u_bloom_enabled;
    int u_bloom_debug_mode; // 0=Off, 1=FinalMap, 2=Prefilter, 3=Downsample, 4=Upsample
    int u_bloom_debug_mip;
    float u_exposure;
    float u_saturation;
    float u_contrast;
    float u_gamma;
    float u_gain;
    float u_offset;
    float u_wb_temp;
    float u_wb_tint;
    int u_autoexposure_enabled;
    int u_autoexposure_debug;
    float u_screen_width;
    float u_screen_height;
}
pc;

vec3 applyWhiteBalance(vec3 color) {
    if (abs(pc.u_wb_temp - 6500.0) < 1.0 && abs(pc.u_wb_tint) < 0.001) {
        return color;
    }

    float tempShift = (pc.u_wb_temp - 6500.0) / 10000.0;
    vec3 wbColor = vec3(1.0);

    if (tempShift < 0.0) {
        wbColor.b = 1.0 - tempShift;
    } else {
        wbColor.r = 1.0 + tempShift;
        wbColor.g = 1.0 + tempShift * 0.5;
    }
    wbColor.g += pc.u_wb_tint * 0.5;

    return color * wbColor;
}

vec3 applyColorGrading(vec3 color) {
    color = applyWhiteBalance(color);

    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luminance), color, pc.u_saturation);
    color = (color - 0.5) * pc.u_contrast + 0.5;
    color = max(vec3(0.0), color);
    if (pc.u_gamma > 0.001) {
        color = pow(color, vec3(pc.u_gamma));
    }
    color = color * pc.u_gain;
    color = color + pc.u_offset;

    return max(vec3(0.0), color);
}

vec3 unrealTonemap(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.0;
    const float e = 0.154;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 renderHistogramOverlay(vec3 baseColor, vec2 pixelPos, vec4 aeData) {
    vec2 boxPos = vec2(30.0, pc.u_screen_height - 180.0);
    vec2 boxSize = vec2(320.0, 150.0);
    vec2 boxEnd = boxPos + boxSize;

    // Check if pixel is inside graph bounding box (with 2px border)
    if (pixelPos.x < boxPos.x - 2.0 || pixelPos.x > boxEnd.x + 2.0 || pixelPos.y < boxPos.y - 2.0 || pixelPos.y > boxEnd.y + 2.0) {
        return baseColor;
    }

    // Border (Green neon accent)
    if (pixelPos.x < boxPos.x || pixelPos.x > boxEnd.x || pixelPos.y < boxPos.y || pixelPos.y > boxEnd.y) {
        return vec3(0.0, 1.0, 0.0);
    }

    // Graph Area Coordinates
    vec2 graphMin = boxPos + vec2(8.0, 8.0);
    vec2 graphMax = boxEnd - vec2(8.0, 8.0);
    vec2 graphSize = graphMax - graphMin;

    if (pixelPos.x < graphMin.x || pixelPos.x > graphMax.x || pixelPos.y < graphMin.y || pixelPos.y > graphMax.y) {
        return vec3(0.02, 0.02, 0.02); // Dark box padding
    }

    float normX = (pixelPos.x - graphMin.x) / graphSize.x;
    int binIdx = int(clamp(normX * 64.0, 0.0, 63.0));

    // Find max count for scaling (or use a dynamic baseline)
    uint maxBinVal = 1u;
    for (int i = 0; i < 64; ++i) {
        uint val = u_debug_histogram.bins[i];
        if (val > maxBinVal) {
            maxBinVal = val;
        }
    }

    uint binVal = u_debug_histogram.bins[binIdx];
    float barHeightNorm = float(binVal) / float(maxBinVal);
    float barPixelY = graphMax.y - barHeightNorm * graphSize.y;

    // Background of graph area
    vec3 col = vec3(0.05, 0.05, 0.05);

    // Draw Bar
    if (pixelPos.y >= barPixelY && pixelPos.y <= graphMax.y) {
        // Color coding: Blue (low shadow percentile), Green (active range), Red (high highlight percentile)
        if (binIdx < 3) {
            col = vec3(0.1, 0.45, 0.9); // Shadow percentile (<5%)
        } else if (binIdx > 62) {
            col = vec3(0.9, 0.2, 0.1); // Highlight percentile (>98%)
        } else {
            col = vec3(0.0, 0.9, 0.1); // Active metering range (5% - 98%)
        }
    }

    // Current adapted luminance indicator (Orange needle)
    float sceneLum = aeData.g;
    float minLogLum = aeData.b;
    float maxLogLum = aeData.a;
    float logLumRange = max(maxLogLum - minLogLum, 1.0);
    float adaptedBinNorm = clamp((log2(max(sceneLum, 0.0001)) - minLogLum) / logLumRange, 0.0, 1.0);
    float needleX = graphMin.x + adaptedBinNorm * graphSize.x;

    if (abs(pixelPos.x - needleX) <= 1.5) {
        col = vec3(1.0, 0.6, 0.0); // Bright orange indicator
    }

    return col;
}

void main() {
    vec4 scene = texture(u_scene_hdr, inUV);
    vec3 bloom = texture(u_bloom_texture, inUV).rgb;

    // Mode Debug Bloom (ISO exact avec suckless-ogl : affiche uniquement la map de bloom isolée)
    if (pc.u_bloom_debug_mode != 0) {
        outColor = vec4(bloom, 1.0);
        return;
    }

    vec3 color = scene.rgb;
    if (pc.u_bloom_enabled != 0) {
        color += bloom * pc.u_bloom_intensity;
    }

    vec4 aeData = vec4(1.0, 0.1, -8.0, 8.0);
    if (pc.u_autoexposure_enabled != 0 || pc.u_autoexposure_debug != 0) {
        aeData = texture(u_autoexposure_texture, vec2(0.5));
    }

    if (pc.u_autoexposure_enabled != 0) {
        float exposure = aeData.r;
        if (exposure <= 0.0001 || isnan(exposure) || isinf(exposure)) {
            exposure = pc.u_exposure;
        }
        color *= exposure;
    } else {
        color *= pc.u_exposure;
    }

    color = applyColorGrading(color);
    color = unrealTonemap(color);

    if (pc.u_autoexposure_debug != 0) {
        vec2 pixelPos = inUV * vec2(pc.u_screen_width, pc.u_screen_height);
        color = renderHistogramOverlay(color, pixelPos, aeData);
    }

    outColor = vec4(color, scene.a);
}
