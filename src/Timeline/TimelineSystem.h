#pragma once
#include <deque>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ECS/Components.h"
#include "ECS/Entity.h"
#include "Physics/PhysicsMaterial.h"

class Registry;

struct EntitySnapshot {
  glm::vec3 position;
  glm::vec3 velocity;
};

struct SpawnedEntityRecord {
  std::string name;
  glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  glm::vec3 scale    = glm::vec3(1.0f);
  SimulatedComponent simulated;
  ColliderComponent  collider;
  bool hasRender = false;
  MeshID meshID = INVALID_MESH_ID;
  RenderMaterialID renderMaterialID = INVALID_RENDER_MATERIAL_ID;
  PhysicsMaterialID physicsMaterialID = INVALID_PHYSICS_MATERIAL_ID;
};

struct FrameSnapshot {
  std::vector<Entity> entities;
  std::vector<EntitySnapshot> states;

  size_t size() const { return entities.size(); }
  void reserve(size_t n) { entities.reserve(n); states.reserve(n); }
  void push(Entity e, EntitySnapshot s) { entities.push_back(e); states.push_back(s); }
};

class TimelineSystem final {
 public:
  TimelineSystem();

  void setRegistry(Registry* reg);

  void saveInitialSnapshot();
  void restoreInitialSnapshot();
  bool hasInitialSnapshot() const { return initialSnapshotValid; }

  void saveSnapshot();
  void saveSnapshotUncompressed();
  void restoreSnapshot(int index);
  void truncateAfter(int index);
  void clearSnapshots();
  int getSnapshotCount() const;

  void registerSpawnedEntity(Entity e, SpawnedEntityRecord record);

  // Read-only access to the snapshot deque (for serialization)
  const std::deque<FrameSnapshot>& getSnapshots() const { return snapshots; }

  // Replace all snapshots with the provided frames (for bake loading)
  void loadFromBake(std::vector<FrameSnapshot> frames);

 private:
  Registry* registry = nullptr;

  FrameSnapshot initialSnapshot;
  bool initialSnapshotValid = false;

  std::deque<FrameSnapshot> snapshots;
  std::unordered_map<Entity, SpawnedEntityRecord> spawnedEntities;

  FrameSnapshot captureFrame() const;
  void applyFrame(const FrameSnapshot& snap);
  void recreateSpawnedEntity(Entity e, const SpawnedEntityRecord& rec);
};
