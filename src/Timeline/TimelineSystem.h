#pragma once
#include <deque>
#include <unordered_map>

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

 private:
  Registry* registry = nullptr;

  FrameSnapshot initialSnapshot;
  bool initialSnapshotValid = false;

  std::deque<FrameSnapshot> snapshots;

  int fullResFrames = 600;
  int condensePassInterval = 300;
  int framesSinceCondense = 0;
  void condenseOldFrames();

  FrameSnapshot captureFrame() const;
  void applyFrame(const FrameSnapshot& snap);
};
