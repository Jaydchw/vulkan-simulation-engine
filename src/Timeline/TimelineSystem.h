#pragma once
#include <deque>
#include <vector>

#include <glm/glm.hpp>

#include "ECS/Entity.h"

class Registry;

struct EntitySnapshot {
  glm::vec3 position;
  glm::vec3 velocity;
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
