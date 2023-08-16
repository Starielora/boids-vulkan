#version 450

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 normal;

layout(location = 0) out vec4 object_color;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec3 out_world_pos;

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
    vec4 world_pos = model.cones[gl_InstanceIndex].model_matrix * vec4(pos, 1.0);
    gl_Position = camera_data.projview * world_pos;
    object_color = model.cones[gl_InstanceIndex].color;
    out_normal = normalize(mat3(transpose(inverse(model.cones[gl_InstanceIndex].model_matrix))) * normal);
    out_world_pos = world_pos.xyz;
}
