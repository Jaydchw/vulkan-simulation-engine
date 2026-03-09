#pragma once
#include <deque>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "ECS/Entity.h"

class Registry;

struct EntitySnapshot {
  glm::vec3 position;
  glm::vec3 velocity;
};

using FrameSnapshot = std::unordered_map<Entity, EntitySnapshot>;

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

  // Read-only access to the snapshot deque (for serialization)
  const std::deque<FrameSnapshot>& getSnapshots() const { return snapshots; }

  // Replace all snapshots with the provided frames (for bake loading)
  void loadFromBake(std::vector<FrameSnapshot> frames);

 private:
  Registry* registry = nullptr;

  FrameSnapshot initialSnapshot;
  bool initialSnapshotValid = false;

  std::deque<FrameSnapshot> snapshots;

  FrameSnapshot captureFrame() const;
  void applyFrame(const FrameSnapshot& snap);
};
