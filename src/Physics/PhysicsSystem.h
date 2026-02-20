#pragma once
#include <glm/glm.hpp>
#include <deque>
#include <unordered_map>
#include <vector>

#include "ECS/Entity.h"

class Registry;

struct EntitySnapshot {
  glm::vec3 position;
  glm::vec3 velocity;
};

using FrameSnapshot = std::unordered_map<Entity, EntitySnapshot>;

class PhysicsSystem final {
 public:
  PhysicsSystem();

  void setRegistry(Registry* reg);
  void update(float deltaTime);

  void saveInitialSnapshot();
  void restoreInitialSnapshot();
  bool hasInitialSnapshot() const { return initialSnapshotValid; }

  void saveSnapshot();
  void restoreSnapshot(int index);
  void truncateAfter(int index);
  void clearSnapshots();
  int getSnapshotCount() const;

  void setGravity(const glm::vec3& g) { gravity = g; }
  glm::vec3 getGravity() const { return gravity; }

 private:
  Registry* registry = nullptr;
  glm::vec3 gravity = glm::vec3(0.0f, -9.81f, 0.0f);

  FrameSnapshot initialSnapshot;
  bool initialSnapshotValid = false;

  std::deque<FrameSnapshot> snapshots;

  int fullResFrames = 600;
  int condensePassInterval = 300;
  int framesSinceCondense = 0;
  void condenseOldFrames();

  void integrate(float deltaTime);
  void resolveCollisions();

  FrameSnapshot captureFrame() const;
  void applyFrame(const FrameSnapshot& snap);
};
