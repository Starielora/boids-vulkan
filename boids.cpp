#include "boids.hpp"

namespace boids
{
    glm::vec4 steer(std::size_t index, const std::vector<cone_instance>& boids, float visual_range, float cohesion_weight, float separation_weight, float alignment_weight)
    {
        assert(index < boids.size());
        const auto& current_boid = boids[index];

        auto observed_boids = std::size_t{ 0 };
        auto avg_observable_cluster_position = glm::vec4(0);
        auto separation = glm::vec4(0);
        auto alignment = glm::vec4();
        for (std::size_t i = 0; i < boids.size(); ++i)
        {
            const auto& boid = boids[i];
            const auto distance = glm::distance(current_boid.position, boid.position);
            if (i != index && distance < visual_range) // TODO use distance2 to avoid paying for sqrt
            {
                observed_boids++;
                avg_observable_cluster_position += boid.position;
                separation += (current_boid.position - boid.position) / glm::abs(distance);
                alignment += boid.velocity;
            }
        }

        if (observed_boids)
        {
            avg_observable_cluster_position /= observed_boids;
            alignment /= observed_boids;
            const auto total_cohesion = (avg_observable_cluster_position - current_boid.position) * cohesion_weight;
            const auto total_separation = separation * separation_weight;
            const auto total_alignment = alignment * alignment_weight;
            return total_cohesion + total_separation + total_alignment;
        }
        else
        {
            return glm::vec4(0);
        }
    }
}