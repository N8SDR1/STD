#version 440

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D trace;

void main()
{
    fragColor = texture(trace, vUv);
}
