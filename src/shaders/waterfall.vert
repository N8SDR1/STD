#version 440

layout(location = 0) in vec4 position;
layout(location = 0) out vec2 vUv;

layout(std140, binding = 0) uniform buf {
    mat4 clip;
};

void main()
{
    vUv = position.zw;
    gl_Position = clip * vec4(position.xy, 0.0, 1.0);
}
