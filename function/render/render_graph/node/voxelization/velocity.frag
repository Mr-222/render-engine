#version 450

layout(location = 0) in vec3 inVelocity;
layout(location = 0) out vec4 outColor;

void main()
{
    const vec3 minVelocity = vec3(-1.0, -1.0, -1.0);
    const vec3 maxVelocity = vec3(1.0, 1.0, 1.0);

    vec3 velocity = clamp(inVelocity, minVelocity, maxVelocity);
    vec3 normalizedColor = velocity / maxVelocity;
    normalizedColor = normalizedColor * 0.5 + 0.5; // Map from [-1, 1] to [0, 1]

    outColor = vec4(normalizedColor, 1.0);
}