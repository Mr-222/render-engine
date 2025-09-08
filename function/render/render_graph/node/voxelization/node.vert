#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_debug_printf : enable
#extension GL_EXT_multiview : enable

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

layout(location = 0) in vec3 inPosition;

#define GetView voxelizationView[pipelineParam.voxelizationViewMat]
#define GetProjs voxelizationProjs[pipelineParam.voxelizationProjMats]

void main()
{
    mat4 view = GetView.view;
    mat4 proj = GetProjs.proj[gl_viewIndex];
    gl_Position = proj * view * vec4(inPosition, 1.0);
}