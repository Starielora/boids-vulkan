#version 460 core

#extension GL_GOOGLE_include_directive : enable
#include "descriptor_set_0.glsl"

const vec3 pos[] = vec3[](
    vec3(-1.0,  -1.0,  1.0),
    vec3( 1.0,  1.0,  1.0),
    vec3(-1.0,  1.0,  1.0),
    vec3( 1.0,  -1.0,  1.0),
    vec3(-1.0,  -1.0, -1.0),
    vec3( 1.0,  1.0, -1.0),
    vec3(-1.0,  1.0, -1.0),
    vec3( 1.0,  -1.0, -1.0)
);

const int indices[] = int[](
    1, 2, 0, 0, 3, 1, // front
    5, 1, 3, 3, 7, 5, // right
    6, 5, 7, 7, 4, 6, // back
    2, 6, 4, 4, 0, 2, // left
    5, 6, 2, 2, 1, 5, // top
    0, 4, 3, 3, 4, 7 // back
);

layout(push_constant) uniform constants
{
    vec4 scale;
} push_constants;

void main()
{
    gl_Position = camera_data.projview * vec4(vec3(0.975, 0.975, 0.975) * push_constants.scale.xyz * (pos[indices[gl_VertexIndex]] ) + aquarium_cells[gl_InstanceIndex].pos.xyz, 1.0);
}
