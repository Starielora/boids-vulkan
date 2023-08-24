#pragma once

#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

#include <vector>

struct cone_instance
{
    glm::vec4 position = glm::vec4(0, 0, 0, 0);
    glm::vec4 direction = glm::vec4(0, 0, 0, 0);
    glm::vec4 velocity = glm::vec4(0, 0, 0, 0);
    glm::vec4 color = glm::vec4(.5, .5, .5, 1);
    glm::mat4 model_matrix = glm::mat4(1.);
};

namespace boids
{
    glm::vec4 steer(std::size_t index, const std::vector<cone_instance>& boids, float visual_range, float cohesion_weight, float separation_weight, float alignment_weight);

    class repellent
    {
    public:
        virtual glm::vec3 get_velocity_diff(const cone_instance&) const = 0;
    };

    class plane_repellent final : public repellent
    {
    public:
        plane_repellent(glm::vec3 normal, float pos, float& wall_force_weight) : _normal(std::move(normal)), _pos(pos), _wall_force_weight(wall_force_weight)
        {
        }

        glm::vec3 get_velocity_diff(const cone_instance& boid) const override
        {
            const auto boid_position = glm::vec3(boid.position);
            const auto v = boid_position - _normal * boid_position + _pos * _normal; // project boid onto plane
            const auto distance2 = glm::distance2(boid_position, v);

            return (_normal / distance2) * _wall_force_weight;
        }

    private:
        glm::vec3 _normal;
        float _pos;
        float& _wall_force_weight;
    };
}

