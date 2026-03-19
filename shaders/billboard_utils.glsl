#ifndef BILLBOARD_UTILS_GLSL
#define BILLBOARD_UTILS_GLSL

struct Rect {
    vec2 min;
    vec2 max;
};

// Helper function to calculate 1D projected bounds (NDC)
void getProjectedBounds(vec2 axis, float radius, float projScale,
                        out float outMin, out float outMax)
{
    float d2 = dot(axis, axis);
    float r2 = radius * radius;

    // ISO OGL logic: singularity handling 
    if (d2 <= r2) {
        outMin = -1.0;
        outMax = 1.0;
        return;
    }

    float L = sqrt(max(0.0, d2 - r2));

    // Tangent logic to find normal of tangent lines
    float nx1 = (axis.x * L - axis.y * radius) / d2;
    float nz1 = (axis.y * L + axis.x * radius) / d2;

    float nx2 = (axis.x * L + axis.y * radius) / d2;
    float nz2 = (axis.y * L - axis.x * radius) / d2;

    float p1, p2;
    // Division by -nz because RHS looks down -Z
    // ISO FIX: Singularity extreme must account for projScale sign (Vulkan Y-flip)
    if (nz1 > -0.001) p1 = (nx1 * projScale >= 0.0 ? 1.0 : -1.0) * 10.0; 
    else p1 = projScale * (nx1 / -nz1);

    if (nz2 > -0.001) p2 = (nx2 * projScale >= 0.0 ? 1.0 : -1.0) * 10.0;
    else p2 = projScale * (nx2 / -nz2);

    outMin = min(p1, p2);
    outMax = max(p1, p2);
}

void computeBillboardSphere(vec2 quadVertexPos, vec3 sphereCenterWorld,
                            float sphereRadius, mat4 view, mat4 projection,
                            out vec4 outClipPos, out vec3 outWorldPos)
{
    vec3 viewPos = (view * vec4(sphereCenterWorld, 1.0)).xyz;
    float distSq = dot(viewPos, viewPos);
    float r2 = sphereRadius * sphereRadius;

    float sx = projection[0][0];
    float sy = projection[1][1];

    // ISO OGL Threshold
    float threshold = r2 + max(r2 * 0.005, 1e-4);

    if (distSq <= threshold) {
        // Inside sphere: cover screen
        outClipPos = vec4(quadVertexPos * 2.0, 0.5, 1.0);

        // Reconstruct world_axes from view matrix rows
        vec3 camRight   = vec3(view[0][0], view[1][0], view[2][0]);
        vec3 camUp      = vec3(view[0][1], view[1][1], view[2][1]);
        vec3 camForward = -vec3(view[0][2], view[1][2], view[2][2]);

        vec3 camPos = -(transpose(mat3(view)) * view[3].xyz);

        outWorldPos = camPos + camForward +
                      camRight * (quadVertexPos.x * 2.0 / sx) +
                      camUp * (quadVertexPos.y * 2.0 / sy);
    } else if (viewPos.z > sphereRadius) {
        // Entirely behind
        outClipPos = vec4(-2.0, -2.0, 0.0, 1.0);
        outWorldPos = sphereCenterWorld;
    } else {
        float minX, maxX, minY, maxY;
        getProjectedBounds(vec2(viewPos.x, viewPos.z), sphereRadius, sx, minX, maxX);
        getProjectedBounds(vec2(viewPos.y, viewPos.z), sphereRadius, sy, minY, maxY);

        // BE CONSERVATIVE: matches the screenshot bug if quads are too tight?
        // Actually, screenshot shows they are HUGE.
        float ndc_x = (quadVertexPos.x < 0.0) ? minX : maxX;
        float ndc_y = (quadVertexPos.y < 0.0) ? minY : maxY;

        float zNear = projection[3][2] / projection[2][2]; 
        float nearestZ = viewPos.z + sphereRadius;
        nearestZ = min(nearestZ, -(zNear + 0.01)); 

        float clipW = -nearestZ;
        float clipZ = projection[2][2] * nearestZ + projection[3][2];

        outClipPos = vec4(ndc_x * clipW, ndc_y * clipW, clipZ, clipW);

        // Reconstruct WorldPos
        vec3 vertexViewPos;
        vertexViewPos.z = nearestZ;
        vertexViewPos.x = ndc_x * (-nearestZ) / sx;
        vertexViewPos.y = ndc_y * (-nearestZ) / sy;

        vec3 worldOffset = transpose(mat3(view)) * (vertexViewPos - viewPos);
        outWorldPos = sphereCenterWorld + worldOffset;
    }
}

Rect getBillboardRect(vec3 sphereCenterWorld, float sphereRadius, mat4 view, mat4 projection) {
    vec3 viewPos = (view * vec4(sphereCenterWorld, 1.0)).xyz;
    float distSq = dot(viewPos, viewPos);
    float r2 = sphereRadius * sphereRadius;

    float sx = projection[0][0];
    float sy = projection[1][1];
    float threshold = r2 + max(r2 * 0.005, 1e-4);
    
    Rect r;
    if (distSq <= threshold) {
        r.min = vec2(-1.0, -1.0);
        r.max = vec2(1.0, 1.0);
    } else {
        getProjectedBounds(vec2(viewPos.x, viewPos.z), sphereRadius, sx, r.min.x, r.max.x);
        getProjectedBounds(vec2(viewPos.y, viewPos.z), sphereRadius, sy, r.min.y, r.max.y);
    }
    return r;
}

#endif // BILLBOARD_UTILS_GLSL
