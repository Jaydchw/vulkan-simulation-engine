#pragma once

#include <glm/glm.hpp>

namespace jphys {

inline glm::vec3 computeWindAcceleration(const glm::vec3& wind,
                                         const glm::vec3& velocity,
                                         float drag) {
  const float clampedDrag = glm::max(drag, 0.0f);
  return (wind - velocity) * clampedDrag;
}

}
