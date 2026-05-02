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
  float getLastGridCellSize() const { return world.getLastGridCellSize(); }

  // Call after externally teleporting an entity or directly modifying its velocity/position.
  // Without this, the physics object will NOT receive the new ECS values next frame.
  void markPhysicsDirty(Entity e) { dirtyDynamic.insert(e); }
  // Call after any external snapshot restore so syncToLibrary() pushes the
  // recovered ECS positions into the physics library on the next step.
  void markAllDirty() { dirtyDynamic = trackedEntities; }

 private:
  Registry*               registry       = nullptr;
  NetworkManager*         networkManager = nullptr;
  PhysicsMaterialManager* physMatManager = nullptr;
  const EnvironmentSettings* environmentSettings = nullptr;
  jphys::PhysicsWorld world;
  bool killboxEnabled = true;
  float killboxY = -150.0f;

  // ── Incremental sync state ────────────────────────────────────────────────
  // Entities currently registered in the physics world.
  std::unordered_set<Entity> trackedEntities;

  // Cached transform snapshot for static objects so we only update when they move.
  struct StaticObjectCache {
    glm::vec3    position;
    glm::quat    rotation;
    ColliderType colliderType;
  };
  std::unordered_map<Entity, StaticObjectCache> staticCache;

  // Dynamic entities whose ECS position/velocity was externally changed this frame.
  std::unordered_set<Entity> dirtyDynamic;

  void syncToLibrary();
  void syncFromLibrary();
  void resolveCrossPeerCollisions();

  // Rebuild world.getObjects() pointer list from the dense ComponentStore.
  // Must be called after any add/remove that may have changed component addresses.
  void rebuildWorldObjects();

  // Full configuration of a PhysicsObject from ECS components (used for new objects
  // and dirty static objects).
  void configurePhysicsObject(jphys::PhysicsObject* obj,
                               const SimulatedComponent*  phys,
                               const ColliderComponent*   collider,
                               const TransformComponent*  transform);
};
