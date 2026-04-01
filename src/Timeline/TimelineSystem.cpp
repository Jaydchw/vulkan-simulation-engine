#include "TimelineSystem.h"

#include "ECS/Components.h"
#include "ECS/Registry.h"
#include "Util/Debug.h"

TimelineSystem::TimelineSystem() {
  Debug::log(Debug::Category::TIMELINE, "TimelineSystem: Created");
}

void TimelineSystem::setRegistry(Registry* reg) {
  registry = reg;
  clearSnapshots();
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
