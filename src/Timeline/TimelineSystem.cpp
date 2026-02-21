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
  for (const auto& [entity, phys] : registry->allPhysics()) {
    const auto* transform = registry->getComponent<TransformComponent>(entity);
    if (!transform) continue;
    snap[entity] = {transform->position, phys.velocity};
  }
  return snap;
}

void TimelineSystem::applyFrame(const FrameSnapshot& snap) {
  if (!registry) return;
  for (const auto& [entity, state] : snap) {
    auto* transform = registry->getComponent<TransformComponent>(entity);
    auto* phys = registry->getComponent<PhysicsComponent>(entity);
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
  framesSinceCondense++;
  if (framesSinceCondense >= condensePassInterval) {
    condenseOldFrames();
    framesSinceCondense = 0;
  }
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
  framesSinceCondense = 0;
}

int TimelineSystem::getSnapshotCount() const {
  return static_cast<int>(snapshots.size());
}

void TimelineSystem::condenseOldFrames() {
  int total = static_cast<int>(snapshots.size());
  if (total <= fullResFrames) return;

  int oldCount = total - fullResFrames;
  if (oldCount < 4) return;

  std::deque<FrameSnapshot> condensed;
  int step = 2;
  if (oldCount > 3600) step = 8;
  else if (oldCount > 1800) step = 4;

  for (int i = 0; i < oldCount; i += step) {
    condensed.push_back(std::move(snapshots[i]));
  }

  for (int i = oldCount; i < total; i++) {
    condensed.push_back(std::move(snapshots[i]));
  }

  snapshots = std::move(condensed);
}
