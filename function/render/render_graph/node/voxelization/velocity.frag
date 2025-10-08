#version 450

layout(location = 0) in vec3 inVelocity;
layout(location = 0) out vec4 outColor;

void main()
{
    // Write to HDR texture
    outColor = vec4(inVelocity, 1.0);
}