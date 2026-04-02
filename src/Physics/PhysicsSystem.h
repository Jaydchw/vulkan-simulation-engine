#pragma once
#include <glm/glm.hpp>
#include <vector>

#include <PhysicsWorld.h>
#include "Physics/PhysicsMaterialManager.h"
#include "Environment/EnvironmentSettings.h"

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
  // Optional: provide a PhysicsMaterialManager for per-pair interaction lookups.
  void setPhysicsMaterialManager(PhysicsMaterialManager* pmm) { physMatManager = pmm; }
  void setEnvironmentSettings(const EnvironmentSettings* settings) { environmentSettings = settings; }

  void update(float deltaTime);
  PhysicsStepTimings timedUpdate(float deltaTime);
  void setKillbox(bool enabled, float y) { killboxEnabled = enabled; killboxY = y; }

  void setGravity(const glm::vec3& g) { world.setGravity(g); }
  glm::vec3 getGravity() const { return world.getGravity(); }

  int getObjectCount() const { return static_cast<int>(world.getObjects().size()); }
  const jphys::PhysicsWorld& getWorld() const { return world; }

 private:
  Registry*               registry       = nullptr;
  NetworkManager*         networkManager = nullptr;
  PhysicsMaterialManager* physMatManager = nullptr;
  const EnvironmentSettings* environmentSettings = nullptr;
  jphys::PhysicsWorld world;
  bool killboxEnabled = true;
  float killboxY = -150.0f;

  void syncToLibrary();
  void syncFromLibrary();
  // Resolves collisions between locally-owned and remote-owned dynamic objects.
  // Only the local object's velocity and position are modified; the remote peer
  // does the symmetric correction on its side.
  void resolveCrossPeerCollisions();
};
