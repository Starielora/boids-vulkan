struct boid
{
    vec4 position;
    vec4 direction;
    vec4 velocity;
    vec4 color;
    mat4 model_matrix;
};

struct DirectionalLight
{
    vec4 direction;
    vec4 ambient;
    vec4 diffuse;
    vec4 specular;
};

struct PointLight
{
    vec4 position;
    vec4 ambient;
    vec4 diffuse;
    vec4 specular;

    float constant;
    float linear;
    float quadratic;
};

layout(set = 0, binding = 0) uniform CameraData
{
    vec4 position;
    mat4 projview;
} camera_data;

layout(set = 0, binding = 1) readonly buffer boids_ssbo_in
{
    boid boids_in[];
};

layout(set = 0, binding = 2) readonly buffer DirLightsData
{
    DirectionalLight directional_lights[];
};

layout(set = 0, binding = 3) readonly buffer PointLightsData
{
    PointLight point_lights[];
};

layout(set = 0, binding = 4) writeonly buffer boids_ssbo_out
{
    boid boids_out[];
};

layout(set = 0, binding = 5, rgba32ui) uniform uimage2D grid_buffer;
