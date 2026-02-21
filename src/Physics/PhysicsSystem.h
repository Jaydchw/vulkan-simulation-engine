#pragma once
#include <glm/glm.hpp>

class Registry;

class PhysicsSystem final {
 public:
  PhysicsSystem();

  void setRegistry(Registry* reg);
  void update(float deltaTime);

  void setGravity(const glm::vec3& g) { gravity = g; }
  glm::vec3 getGravity() const { return gravity; }

 private:
  Registry* registry = nullptr;
  glm::vec3 gravity = glm::vec3(0.0f, -9.81f, 0.0f);

  void integrate(float deltaTime);
  void resolveCollisions();
};
