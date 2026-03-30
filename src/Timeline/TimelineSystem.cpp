#include "TimelineSystem.h"

#include "ECS/Components.h"
#include "ECS/Registry.h"
#include "Util/Debug.h"

TimelineSystem::TimelineSystem() {
  Debug::log(Debug::Category::PHYSICS, "TimelineSystem: Created");
}

void TimelineSystem::setRegistry(Registry* reg) {
  registry = reg;
  clearSnapshots();
  initialSnapshotValid = false;
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
}

void TimelineSystem::restoreInitialSnapshot() {
  if (!initialSnapshotValid) return;
  applyFrame(initialSnapshot);
}

void TimelineSystem::saveSnapshot() {
  snapshots.push_back(captureFrame());
}

void TimelineSystem::saveSnapshotUncompressed() {
  snapshots.push_back(captureFrame());
}

void TimelineSystem::restoreSnapshot(int index) {
  if (!registry || index < 0 ||
      index >= static_cast<int>(snapshots.size()))
    return;
  applyFrame(snapshots[index]);
}

void TimelineSystem::truncateAfter(int index) {
  if (index < 0 || index >= static_cast<int>(snapshots.size())) return;
  snapshots.erase(snapshots.begin() + index + 1, snapshots.end());
}

void TimelineSystem::clearSnapshots() {
  snapshots.clear();
}

int TimelineSystem::getSnapshotCount() const {
  return static_cast<int>(snapshots.size());
}

void TimelineSystem::loadFromBake(std::vector<FrameSnapshot> frames) {
  snapshots.clear();
  for (auto& f : frames)
    snapshots.push_back(std::move(f));
}
