#version 450

#extension GL_GOOGLE_include_directive : enable

#include "descriptor_set0.glsl"

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 normal;

layout(location = 0) out vec4 object_color;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec3 out_world_pos;

void main() {
    vec4 world_pos = boids_in[gl_InstanceIndex].model_matrix * vec4(pos, 1.0);
    gl_Position = camera_data.projview * world_pos;
    object_color = boids_in[gl_InstanceIndex].color;
    out_normal = normalize(mat3(transpose(inverse(boids_in[gl_InstanceIndex].model_matrix))) * normal);
    out_world_pos = world_pos.xyz;
}
