#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_debug_printf : enable

#include "../../shader/common.glsl"

layout (set = 0, binding = BindlessUniformBinding) uniform VoxelizationView
{
    mat4 view;
}
voxelizationView[];

layout (set = 1, binding = 0) uniform VoxelizationPipelineParam
{
    Handle voxelizationViewMat;
    Handle voxelizationProjMats;
}
pipelineParam;

layout(location = 0) in vec3 inPosition;

#define GetView voxelizationView[pipelineParam.voxelizationViewMat]

void main()
{
    mat4 view = GetView.view;
    gl_Position = view * vec4(inPosition, 1.0);
}