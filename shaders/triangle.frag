#version 450

#extension GL_GOOGLE_include_directive : enable
#include "descriptor_set_0.glsl"

layout(location = 0) in vec4 object_color;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 world_pos;
layout(location = 0) out vec4 out_color;

vec3 directional_lights_part(vec3 view_dir);
vec3 point_lights_part(vec3 view_dir);

void main() {
    vec3 result = vec3(0);

    vec3 view_dir = normalize(camera_data.position.xyz - world_pos);

    result += directional_lights_part(view_dir) * object_color.xyz;
    result += point_lights_part(view_dir) * object_color.xyz;

    out_color = vec4(result, 1.0);
}

vec3 directional_lights_part(vec3 view_dir)
{
    vec3 ambient = vec3(0);
    vec3 diffuse = vec3(0);
    vec3 specular = vec3(0);

    for (int i = 0; i < directional_lights.length(); ++i)
    {
        DirectionalLight light = directional_lights[i];
        vec3 dir = -normalize(light.direction.xyz);
        float diffuse_scale = max(dot(normal, dir), 0.0);
        vec3 reflected_dir = reflect(-dir, normal);
        float specular_scale = pow(max(dot(view_dir, reflected_dir), 0.0), 32); // 32 is material shininess

        ambient += light.ambient.xyz;
        diffuse += light.diffuse.xyz * diffuse_scale;
        specular += light.specular.xyz * specular_scale;
    }

    return (ambient + diffuse + specular);
}

vec3 point_lights_part(vec3 view_dir)
{
    vec3 ambient = vec3(0);
    vec3 diffuse = vec3(0);
    vec3 specular = vec3(0);

    for (int i = 0; i < point_lights.length(); ++i)
    {
        PointLight light = point_lights[i];
        float dist = length(light.position.xyz - world_pos);
        float attenuation = 1.0 / (light.constant + light.linear * dist + light.quadratic * dist * dist);

        vec3 dir = -normalize(world_pos - light.position.xyz);
        float diffuse_scale = max(dot(normal, dir), 0.0);
        vec3 reflected_dir = reflect(-dir, normal);
        float specular_scale = pow(max(dot(view_dir, reflected_dir), 0.0), 32); // 32 is material shininess

        ambient += light.ambient.xyz * attenuation;
        diffuse += light.diffuse.xyz * diffuse_scale * attenuation;
        specular += light.specular.xyz * specular_scale * attenuation;
    }

    return (ambient + diffuse + specular);
}
