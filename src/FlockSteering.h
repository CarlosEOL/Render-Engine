#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace FlockSteering {

inline glm::vec3 Direction(const glm::vec3& vector) {
    return vector / std::max(glm::length(vector), .001f);
}

inline glm::vec3 Limit(const glm::vec3& vector, float maximum) {
    return vector * (maximum / std::max(glm::length(vector), maximum));
}

// Read a snapshot so every member steers from the same positions and headings.
template<class Agent>
glm::vec3 Neighbors(const Agent* group, std::size_t count, std::size_t index,
                    float sight, float spacing, float cruise) {
    const auto& self = group[index];
    glm::vec3 separation(0), headings(0), offsets(0);
    float neighbors = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto offset = group[i].position - self.position;
        const float distanceSquared = glm::dot(offset, offset);
        if (i == index || distanceSquared >= sight * sight) continue;
        const float distance = std::sqrt(distanceSquared);
        const float weight = 1 - distance / sight;
        separation -= Direction(offset) * std::max(0.f, 1 - distance / spacing);
        headings += Direction(group[i].velocity) * weight;
        offsets += offset * weight;
        neighbors += weight;
    }
    return separation * (cruise * 2.5f)
        + Direction(headings) * (cruise * .5f)
        + offsets * (.78f / std::max(neighbors, 1.f));
}

}
