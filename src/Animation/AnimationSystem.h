#pragma once
#include "ECS/Registry.h"

class AnimationSystem final {
 public:
  void setRegistry(Registry* reg);
  void update(float dt);
  void reset();

 private:
  Registry* registry = nullptr;
};
