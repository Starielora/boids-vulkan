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

struct aquarium_cell
{
    vec4 pos;
};

layout(set = 0, binding = 0) uniform CameraData
{
    vec4 position;
    mat4 projview;
} camera_data;

layout(set = 0, binding = 1) readonly buffer boids_ssbo
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

layout(set = 0, binding = 4) readonly buffer AquariumCells
{
    aquarium_cell aquarium_cells[];
};
