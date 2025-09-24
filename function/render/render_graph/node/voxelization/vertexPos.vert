#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_debug_printf : enable

#include "../../shader/common.glsl"

layout(set = 0, binding = 0) uniform ObjectParam
{
    mat4 model;
    mat4 modelInvTrans;
    Handle material;
    Handle vertBuf;
}
objectParam;

layout(location = 0) in vec3 inPosition;

layout(location = 0, xfb_buffer = 0, xfb_offset = 0) out vec4 outPosition;

#define GetModel objectParam.model

void main()
{
    outPosition = GetModel * vec4(inPosition, 1.0);
}