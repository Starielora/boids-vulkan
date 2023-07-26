#version 460 core

layout(set = 0, binding = 0) uniform CameraData
{
    vec4 position;
    mat4 projview;
} camera_data;

layout(location = 0) out vec4 out_color;
layout(location = 0) in vec3 world_pos;
layout(location = 1) in float grid_size;

#define PI 3.1415
#define AXIS

void main()
{
    vec3 dir = world_pos - camera_data.position.xyz;

    float distance_to_camera = length(dir.xz);

    float x_size = 1.;
    float z_size = x_size;
    float thickness = 0.01;
    float x_step = abs(sin(x_size * world_pos.x*PI));
    float z_step = abs(sin(z_size * world_pos.z*PI));

    float linecount = 2.0 * x_size;
    float blendregion = 2.8;

    vec2 dF = fwidth(world_pos.xz) * linecount;
    float valueX = 1.0 - smoothstep(dF.s * thickness, dF.s * (thickness + blendregion), x_step);
    float valueY = 1.0 - smoothstep(dF.t * thickness, dF.t * (thickness + blendregion), z_step);
    vec3 vertical = vec3(valueX);
    vec3 horizontal = vec3(valueY);
    float bloom = smoothstep(0.0, 1., distance_to_camera/100.);

    vec3 color = max(vertical + bloom, horizontal + bloom);
    color *= vec3(0.25,0.25,0.25);

    #ifdef AXIS
    if (length(world_pos * vec3(0., 1., 1.)) < 0.04)
    {
        color = vec3(1.0, 0.0, 0.0);
    }
    else if (length(world_pos * vec3(1., 1., 0.)) < 0.04)
    {
        color = vec3(0.0, 0.0, 1.0);
    }
    #endif

    out_color = vec4(color, (1. - pow(distance_to_camera/grid_size, 3.0)) * length(color));
}
