#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_debug_printf : enable

#include "../../shader/common.glsl"

layout(triangles) in;
layout(triangle_strip, max_vertices = 4) out;

layout(location = 0) in vec3 inVelocity[];
layout(location = 1) flat in int inInstanceIndex[];

layout (set = 1, binding = 0) uniform PipelineParam
{
    vec2 projSpacePixDim;
    float deltaT;
    Handle voxelizationViewMat;
    Handle voxelizationProjMats;
}
pipelineParam;

layout(location = 0) out vec3 outVelocity;

struct IntersectionVertex {
    vec2 Pos;
    vec3 Velocity;
};

void GetEdgePlaneIntersection(vec4 vAPos, vec3 vAVelocity, vec4 vBPos, vec3 vBVelocity, float zPlane,
                              inout IntersectionVertex intersections[2], inout int idx)
{
    // If the edge is parallel to the slice plane, it cannot intersect unless it's
    // perfectly co-planar. In either case, this prevents a division by zero.
    if (abs(vAPos.z - vBPos.z) < 1e-6) {
        return;
    }

    float t = (zPlane - vAPos.z) / (vBPos.z - vAPos.z);
    if (t < 0.0 || t > 1.0) {
        return;
    }

    intersections[idx].Pos = mix(vAPos.xy, vBPos.xy, t);
    intersections[idx].Velocity = mix(vAVelocity, vBVelocity, t);
    idx++;
}

void main() {
    const float sliceZ = 0.0f;

    vec4 v0 = gl_in[0].gl_Position;
    vec4 v1 = gl_in[1].gl_Position;
    vec4 v2 = gl_in[2].gl_Position;

    // --- Early Cull Check ---
    float minZ = min(min(v0.z, v1.z), v2.z);
    float maxZ = max(max(v0.z, v1.z), v2.z);
    if ((sliceZ < minZ) || (sliceZ > maxZ)) {
        return; // This triangle doesn't intersect the slice.
    }

    // --- Find Intersections ---
    IntersectionVertex intersections[2];
    int idx = 0;

    // Check each edge of the input triangle.
    GetEdgePlaneIntersection(v0, inVelocity[0], v1, inVelocity[1], sliceZ, intersections, idx);
    if (idx < 2) {
        GetEdgePlaneIntersection(v1, inVelocity[1], v2, inVelocity[2], sliceZ, intersections, idx);
    }
    if (idx < 2) {
        GetEdgePlaneIntersection(v2, inVelocity[2], v0, inVelocity[0], sliceZ, intersections, idx);
    }

    // If we didn't find exactly two intersections, the slice either missed,
    // hit a single vertex, or fully contained an edge. We only care about crossings.
    if (idx < 2) {
        return;
    }

    // --- Generate Quad ---
    vec2 segmentVector = intersections[1].Pos - intersections[0].Pos;
    vec2 segmentDir    = normalize(segmentVector);
    vec2 normal        = vec2(-segmentDir.y, segmentDir.x);
    vec2 thickness     = normal * pipelineParam.projSpacePixDim * 1.41421356f * 2.f; // sqrt(2) * 2, cover two texel diagnal

    int layer = inInstanceIndex[0];

    // Vertex 1, bottom-left
    gl_Layer    = layer;
    outVelocity = intersections[0].Velocity;
    gl_Position = vec4(intersections[0].Pos, 0.0, 1.0);
    EmitVertex();

    // Vertex 2, top-left
    gl_Layer    = layer;
    outVelocity = intersections[0].Velocity;
    gl_Position = vec4(intersections[0].Pos + thickness, 0.0, 1.0);
    EmitVertex();

    // Vertex 3, bottom-right
    gl_Layer    = layer;
    outVelocity = intersections[1].Velocity;
    gl_Position = vec4(intersections[1].Pos, 0.0, 1.0);
    EmitVertex();

    // Vertex 4, top-right
    gl_Layer    = layer;
    outVelocity = intersections[1].Velocity;
    gl_Position = vec4(intersections[1].Pos + thickness, 0.0, 1.0);
    EmitVertex();

    EndPrimitive();
}