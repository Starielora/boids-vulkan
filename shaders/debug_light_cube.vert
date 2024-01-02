#version 460 core

#extension GL_GOOGLE_include_directive : enable
#include "descriptor_set_0.glsl"

vec3 vertices[] = {
    vec3(-0.5f, -0.5f, -0.5f),
    vec3( 0.5f, -0.5f, -0.5f),
    vec3( 0.5f,  0.5f, -0.5f),
    vec3( 0.5f,  0.5f, -0.5f),
    vec3(-0.5f,  0.5f, -0.5f),
    vec3(-0.5f, -0.5f, -0.5f),

    vec3(-0.5f, -0.5f,  0.5f),
    vec3( 0.5f, -0.5f,  0.5f),
    vec3( 0.5f,  0.5f,  0.5f),
    vec3( 0.5f,  0.5f,  0.5f),
    vec3(-0.5f,  0.5f,  0.5f),
    vec3(-0.5f, -0.5f,  0.5f),

    vec3(-0.5f,  0.5f,  0.5f),
    vec3(-0.5f,  0.5f, -0.5f),
    vec3(-0.5f, -0.5f, -0.5f),
    vec3(-0.5f, -0.5f, -0.5f),
    vec3(-0.5f, -0.5f,  0.5f),
    vec3(-0.5f,  0.5f,  0.5f),

    vec3( 0.5f,  0.5f,  0.5f),
    vec3( 0.5f,  0.5f, -0.5f),
    vec3( 0.5f, -0.5f, -0.5f),
    vec3( 0.5f, -0.5f, -0.5f),
    vec3( 0.5f, -0.5f,  0.5f),
    vec3( 0.5f,  0.5f,  0.5f),

    vec3(-0.5f, -0.5f, -0.5f),
    vec3( 0.5f, -0.5f, -0.5f),
    vec3( 0.5f, -0.5f,  0.5f),
    vec3( 0.5f, -0.5f,  0.5f),
    vec3(-0.5f, -0.5f,  0.5f),
    vec3(-0.5f, -0.5f, -0.5f),

    vec3(-0.5f,  0.5f, -0.5f),
    vec3( 0.5f,  0.5f, -0.5f),
    vec3( 0.5f,  0.5f,  0.5f),
    vec3( 0.5f,  0.5f,  0.5f),
    vec3(-0.5f,  0.5f,  0.5f),
    vec3(-0.5f,  0.5f, -0.5f),
};

layout(location = 0) out vec4 frag_color;

void main()
{
    PointLight light = point_lights[gl_InstanceIndex];
    mat4 model = mat4(0.25f);
    model[3] = vec4(light.position.xyz, 1.0);
    gl_Position = camera_data.projview * model * vec4(vertices[gl_VertexIndex], 1.);
    frag_color = light.ambient;
}
