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

layout(set = 0, binding = 5) buffer cells_ssbo
{
    uvec4 cells[];
};

uint cell_index(uvec3 cell, uvec4 cells_max)
{
    return cell.z * cells_max.x * cells_max.y + cell.y * cells_max.x + cell.x;
}

uint index_in_buffer(uint boid_id, uint cell_id)
{
    // treat as 2D array, where first index is cell_id and second boid_id
    return cell_id * boids_in.length() + boid_id;
}

