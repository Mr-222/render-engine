#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_debug_printf : enable
#extension GL_ARB_shader_viewport_layer_array : enable

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

layout(set = 2, binding = 0) uniform ObjectParam
{
    mat4 model;
    mat4 modelInvTrans;
    Handle material;
    Handle vertBuf;
}
objectParam;

layout(location = 0) in vec3 inPosition;

#define GetView voxelizationView[pipelineParam.voxelizationViewMat]
#define GetProjs voxelizationProjs[pipelineParam.voxelizationProjMats]
#define GetObject objectParam

void main()
{
    int layer = gl_InstanceIndex;
    gl_Layer = layer;

    vec4 position_w = GetObject.model * vec4(inPosition, 1.0);
    mat4 view = GetView.view;
    mat4 proj = GetProjs.proj[layer];

    gl_Position = proj * view * position_w;
}