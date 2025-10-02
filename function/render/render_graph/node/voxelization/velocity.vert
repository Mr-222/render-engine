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
    mat4[] projs;
}
voxelizationProjs[];

layout (set = 0, binding = BindlessStorageBinding) buffer VertexPosWorld {
    vec4[] positions;
}
vertexPosWorld[];

layout (set = 1, binding = 0) uniform PipelineParam
{
    vec2 projSpacePixDim;
    float deltaT;
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

layout(location = 0) out vec3 outVelocity;
layout(location = 1) flat out int outInstanceIndex;

#define GetModel objectParam.model
#define GetView voxelizationView[pipelineParam.voxelizationViewMat].view
#define GetProj(index) voxelizationProjs[pipelineParam.voxelizationProjMats].projs[index]
#define GetPrevVertexPos(index) vertexPosWorld[objectParam.vertBuf].positions[index]

void main()
{
    int layer = gl_InstanceIndex;

    vec4 prevPos = GetPrevVertexPos(gl_VertexIndex);
    vec4 currPos = GetModel * vec4(inPosition, 1.0);
    vec3 velocity = (currPos.xyz - prevPos.xyz) / pipelineParam.deltaT;

    outVelocity      = velocity;
    outInstanceIndex = layer;
    gl_Position = GetProj(layer) * GetView * currPos;
}