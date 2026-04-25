#include "TimelineSystem.h"

#include <unordered_set>

#include "ECS/Components.h"
#include "ECS/Registry.h"
#include "Util/Debug.h"

TimelineSystem::TimelineSystem() {
  Debug::log(Debug::Category::TIMELINE, "TimelineSystem: Created");
}

void TimelineSystem::setRegistry(Registry* reg) {
  registry = reg;
  clearSnapshots();
  spawnedEntities.clear();
  initialSnapshotValid = false;
  Debug::log(Debug::Category::TIMELINE, "TimelineSystem: Registry set, snapshots cleared");
}

FrameSnapshot TimelineSystem::captureFrame() const {
  FrameSnapshot snap;
  if (!registry) return snap;
  const auto& simulated = registry->allSimulated();
  snap.reserve(simulated.size());
  for (const auto& [entity, phys] : simulated) {
    const auto* transform = registry->getComponent<TransformComponent>(entity);
    if (!transform) continue;
    snap.push(entity, {transform->position, phys.velocity});
  }
  return snap;
}

void TimelineSystem::applyFrame(const FrameSnapshot& snap) {
  if (!registry) return;

  std::unordered_set<Entity> snapSet(snap.entities.begin(), snap.entities.end());

  std::vector<Entity> toDestroy;
  for (const auto& [entity, _] : registry->allSimulated()) {
    if (!snapSet.count(entity) && spawnedEntities.count(entity))
      toDestroy.push_back(entity);
  }
  for (Entity e : toDestroy)
    registry->destroyEntity(e);

  for (Entity entity : snap.entities) {
    if (!registry->getComponent<SimulatedComponent>(entity)) {
      auto it = spawnedEntities.find(entity);
      if (it != spawnedEntities.end())
        recreateSpawnedEntity(entity, it->second);
    }
  }

  for (size_t i = 0; i < snap.entities.size(); ++i) {
    const Entity entity = snap.entities[i];
    const EntitySnapshot& state = snap.states[i];
    auto* transform = registry->getComponent<TransformComponent>(entity);
    auto* phys = registry->getComponent<SimulatedComponent>(entity);
    if (transform) transform->position = state.position;
    if (phys) phys->velocity = state.velocity;
  }
}

void TimelineSystem::saveInitialSnapshot() {
  initialSnapshot = captureFrame();
  initialSnapshotValid = true;
  Debug::log(Debug::Category::TIMELINE,
      "TimelineSystem: Saved initial snapshot with ", initialSnapshot.entities.size(), " entities");
}

void TimelineSystem::restoreInitialSnapshot() {
  if (!initialSnapshotValid) return;
  applyFrame(initialSnapshot);
  Debug::log(Debug::Category::TIMELINE,
      "TimelineSystem: Restored initial snapshot (", initialSnapshot.entities.size(), " entities)");
}

void TimelineSystem::saveSnapshot() {
  snapshots.push_back(captureFrame());
  Debug::logVerbose(Debug::Category::TIMELINE,
      "TimelineSystem: Saved snapshot #", snapshots.size() - 1,
      " entities=", snapshots.back().entities.size());
}

void TimelineSystem::saveSnapshotUncompressed() {
  snapshots.push_back(captureFrame());
  Debug::logVerbose(Debug::Category::TIMELINE,
      "TimelineSystem: Saved snapshot (uncompressed) #", snapshots.size() - 1,
      " entities=", snapshots.back().entities.size());
}

void TimelineSystem::restoreSnapshot(int index) {
  if (!registry || index < 0 ||
      index >= static_cast<int>(snapshots.size()))
    return;
  applyFrame(snapshots[index]);
  Debug::log(Debug::Category::TIMELINE,
      "TimelineSystem: Restored snapshot #", index,
      " of ", snapshots.size(), " (", snapshots[index].entities.size(), " entities)");
}

void TimelineSystem::truncateAfter(int index) {
  if (index < 0 || index >= static_cast<int>(snapshots.size())) return;
  int removedCount = static_cast<int>(snapshots.size()) - index - 1;
  snapshots.erase(snapshots.begin() + index + 1, snapshots.end());
  Debug::logVerbose(Debug::Category::TIMELINE,
      "TimelineSystem: Truncated timeline after #", index,
      " removed=", removedCount, " remaining=", snapshots.size());
}

void TimelineSystem::clearSnapshots() {
  int prev = static_cast<int>(snapshots.size());
  snapshots.clear();
  Debug::logVerbose(Debug::Category::TIMELINE,
      "TimelineSystem: Cleared ", prev, " snapshots");
}

int TimelineSystem::getSnapshotCount() const {
  return static_cast<int>(snapshots.size());
}

void TimelineSystem::loadFromBake(std::vector<FrameSnapshot> frames) {
  snapshots.clear();
  for (auto& f : frames)
    snapshots.push_back(std::move(f));
  Debug::log(Debug::Category::TIMELINE,
      "TimelineSystem: Loaded ", snapshots.size(), " frames from bake");
}

void TimelineSystem::registerSpawnedEntity(Entity e, SpawnedEntityRecord record) {
  spawnedEntities[e] = std::move(record);
}

void TimelineSystem::recreateSpawnedEntity(Entity e, const SpawnedEntityRecord& rec) {
  registry->createEntityWithId(e);
  registry->addComponent<NameComponent>(e, {rec.name});

  TransformComponent tc;
  tc.rotation = rec.rotation;
  tc.scale    = rec.scale;
  registry->addComponent<TransformComponent>(e, tc);

  registry->addComponent<SimulatedComponent>(e, rec.simulated);
  registry->addComponent<ColliderComponent>(e, rec.collider);

  if (rec.hasRender && rec.meshID != INVALID_MESH_ID && rec.renderMaterialID != INVALID_RENDER_MATERIAL_ID) {
    registry->addComponent<MeshComponent>(e, {rec.meshID});
    registry->addComponent<RenderMaterialComponent>(e, {rec.renderMaterialID});
    registry->addComponent<RenderComponent>(e, {});
  }

  if (rec.physicsMaterialID != INVALID_PHYSICS_MATERIAL_ID)
    registry->addComponent<PhysicsMaterialComponent>(e, {rec.physicsMaterialID});

  Debug::logVerbose(Debug::Category::TIMELINE,
      "TimelineSystem: Recreated spawned entity ", e, " '", rec.name, "'");
}
