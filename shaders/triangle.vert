#version 450

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 color;

layout(location = 0) out vec4 frag_color;

layout(set = 0, binding = 0) uniform CameraData
{
    vec4 position;
    mat4 projview;
} camera_data;

struct ConeInstance
{
    vec4 position;
    vec4 direction;
    vec4 velocity;
    vec4 color;
    mat4 model_matrix;
};

layout(set = 0, binding = 1) readonly buffer ModelData
{
    ConeInstance cones[];
} model;

void main() {
    gl_Position = camera_data.projview * model.cones[gl_InstanceIndex].model_matrix * vec4(pos, 1.0);
    frag_color = model.cones[gl_InstanceIndex].color;
}
