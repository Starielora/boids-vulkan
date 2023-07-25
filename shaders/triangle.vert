#version 450

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 color;

layout(location = 0) out vec3 fragColor;

layout(set = 0, binding = 0) uniform CameraData
{
    vec4 position;
    mat4 projview;
} camera_data;

void main() {
    gl_Position = camera_data.projview * vec4(pos, 1.0);
    fragColor = color;
}
