#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_debug_printf : enable

#include "../../shader/common.glsl"

layout (set = 0, binding = BindlessUniformBinding) uniform VoxelizationView
{
    mat4 view;
}
voxelizationView[];

layout (set = 0, binding = BindlessStorageBinding) readonly buffer VoxelizationProjs
{
    mat4[] proj;
}
voxelizationProjs[];

layout (set = 1, binding = 0) uniform VoxelizationPipelineParam
{
    Handle voxelizationViewMat;
    Handle voxelizationProjMats;
}
pipelineParam;

#define GetProjs voxelizationProjs[pipelineParam.voxelizationProjMats]
#define LAYER_COUNT 64

layout (triangles) in;
layout (triangle_strip, max_vertices = 3 * LAYER_COUNT) out;

void main()
{
    // Loop every single layer
    for (int layer = 0; layer < LAYER_COUNT; layer++)
    {
        // For each layer, re-process all three vertices of the input triangle
        for (int i = 0; i < gl_in.length(); ++i)
        {
            vec4 viewPos = gl_in[i].gl_Position;
            gl_Position  = GetProjs.proj[layer] * viewPos;
            gl_Layer     = layer;
            EmitVertex();
        }
        // After emitting 3 vertices, finish the new triangle for this layer.
        EndPrimitive();
    }
}