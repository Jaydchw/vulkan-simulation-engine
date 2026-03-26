#pragma once
#include <glm/glm.hpp>
#include <vector>

#include <PhysicsWorld.h>

class Registry;
class NetworkManager;

struct PhysicsStepTimings {
  double syncToMs = 0.0;
  double physStepMs = 0.0;
  double syncFromMs = 0.0;
  std::vector<jphys::CollisionPairStat> collisionStats;
};

class PhysicsSystem final {
 public:
  PhysicsSystem();

  void setRegistry(Registry* reg);
  // Optional: provide a NetworkManager so only locally-owned objects are simulated.
  // Pass nullptr to simulate all objects (single-player / no network).
  void setNetworkManager(NetworkManager* nm) { networkManager = nm; }

  void update(float deltaTime);
  PhysicsStepTimings timedUpdate(float deltaTime);

  void setGravity(const glm::vec3& g) { world.setGravity(g); }
  glm::vec3 getGravity() const { return world.getGravity(); }

  int getObjectCount() const { return static_cast<int>(world.getObjects().size()); }

 private:
  Registry* registry = nullptr;
  NetworkManager* networkManager = nullptr;
  jphys::PhysicsWorld world;

  void syncToLibrary();
  void syncFromLibrary();
};
