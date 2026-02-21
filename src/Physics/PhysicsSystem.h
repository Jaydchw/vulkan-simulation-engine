#pragma once
#include <glm/glm.hpp>

#include <PhysicsWorld.h>

class Registry;

class PhysicsSystem final {
 public:
  PhysicsSystem();

  void setRegistry(Registry* reg);
  void update(float deltaTime);

  void setGravity(const glm::vec3& g) { world.setGravity(g); }
  glm::vec3 getGravity() const { return world.getGravity(); }

 private:
  Registry* registry = nullptr;
  jphys::PhysicsWorld world;

  void syncToLibrary();
  void syncFromLibrary();
};
