#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <PhysicsWorld.h>
#include "ECS/Components.h"
#include "ECS/Entity.h"
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
  void setNetworkManager(NetworkManager* nm) { networkManager = nm; }
  void setPhysicsMaterialManager(PhysicsMaterialManager* pmm) { physMatManager = pmm; }
  void setEnvironmentSettings(const EnvironmentSettings* settings) { environmentSettings = settings; }

  void update(float deltaTime);
  PhysicsStepTimings timedUpdate(float deltaTime);
  void setKillbox(bool enabled, float y) { killboxEnabled = enabled; killboxY = y; }

  void setGravity(const glm::vec3& g) { world.setGravity(g); }
  glm::vec3 getGravity() const { return world.getGravity(); }

  int getObjectCount() const { return static_cast<int>(world.getObjects().size()); }
  const jphys::PhysicsWorld& getWorld() const { return world; }
  float getLastGridCellSize() const { return world.getLastGridCellSize(); }

  void markPhysicsDirty(Entity e) { dirtyDynamic.insert(e); }
  void markAllDirty() { dirtyDynamic = trackedEntities; }

 private:
  Registry*               registry       = nullptr;
  NetworkManager*         networkManager = nullptr;
  PhysicsMaterialManager* physMatManager = nullptr;
  const EnvironmentSettings* environmentSettings = nullptr;
  jphys::PhysicsWorld world;
  bool killboxEnabled = true;
  float killboxY = -150.0f;

  std::unordered_set<Entity> trackedEntities;

  struct StaticObjectCache {
    glm::vec3    position;
    glm::quat    rotation;
    ColliderType colliderType;
  };
  std::unordered_map<Entity, StaticObjectCache> staticCache;

  std::unordered_set<Entity> dirtyDynamic;

  void syncToLibrary();
  void syncFromLibrary();
  void resolveCrossPeerCollisions();

  void rebuildWorldObjects();

  void configurePhysicsObject(jphys::PhysicsObject* obj,
                               const SimulatedComponent*  phys,
                               const ColliderComponent*   collider,
                               const TransformComponent*  transform);
};
