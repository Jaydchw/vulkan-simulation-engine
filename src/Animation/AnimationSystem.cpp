#include "AnimationSystem.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

void AnimationSystem::setRegistry(Registry* reg) {
  registry = reg;
}

void AnimationSystem::reset() {
  if (!registry) return;
  for (auto&& [entity, anim] : registry->allAnimationsMut()) {
    anim.currentTime = 0.0f;
    anim.forward     = true;
    anim.active      = true;

    auto* tc = registry->getComponent<TransformComponent>(entity);
    if (tc && !anim.waypoints.empty()) {
      tc->position = anim.waypoints.front().position;
      tc->rotation = anim.waypoints.front().rotation;
    }
  }
}

void AnimationSystem::update(float dt) {
  if (!registry) return;

  for (auto&& [entity, anim] : registry->allAnimationsMut()) {
    if (!anim.active || anim.waypoints.size() < 2) continue;

    auto* tc = registry->getComponent<TransformComponent>(entity);
    if (!tc) continue;

    const float lastWpTime = anim.waypoints.back().time;

    if (anim.forward) {
      anim.currentTime += dt;
    } else {
      anim.currentTime -= dt;
    }

    switch (anim.pathMode) {
      case PathMode::STOP:
        if (anim.currentTime >= lastWpTime) {
          anim.currentTime = lastWpTime;
          anim.active = false;
        }
        break;

      case PathMode::LOOP:
        if (anim.totalDuration > 0.0f && anim.currentTime >= anim.totalDuration) {
          anim.currentTime -= anim.totalDuration;
          if (anim.currentTime < 0.0f) anim.currentTime = 0.0f;
        }
        break;

      case PathMode::REVERSE:
        if (anim.forward && anim.currentTime >= lastWpTime) {
          anim.currentTime = lastWpTime;
          anim.forward = false;
        } else if (!anim.forward && anim.currentTime <= 0.0f) {
          anim.currentTime = 0.0f;
          anim.forward = true;
        }
        break;
    }

    int segA = 0, segB = 1;
    float localT = 0.0f;

    const auto& wps = anim.waypoints;

    if (anim.pathMode == PathMode::LOOP) {
      const float lastTime = wps.back().time;
      if (anim.currentTime >= lastTime && anim.totalDuration > lastTime) {
        float segLen = anim.totalDuration - lastTime;
        localT = (segLen > 0.0f) ? (anim.currentTime - lastTime) / segLen : 0.0f;
        localT = glm::clamp(localT, 0.0f, 1.0f);
        segA = static_cast<int>(wps.size()) - 1;
        segB = 0;
      } else {
        for (int i = static_cast<int>(wps.size()) - 2; i >= 0; --i) {
          if (anim.currentTime >= wps[i].time) {
            float segLen = wps[i + 1].time - wps[i].time;
            localT = (segLen > 0.0f) ? (anim.currentTime - wps[i].time) / segLen : 1.0f;
            localT = glm::clamp(localT, 0.0f, 1.0f);
            segA = i;
            segB = i + 1;
            break;
          }
        }
      }
    } else {
      for (int i = static_cast<int>(wps.size()) - 2; i >= 0; --i) {
        if (anim.currentTime >= wps[i].time) {
          float segLen = wps[i + 1].time - wps[i].time;
          localT = (segLen > 0.0f) ? (anim.currentTime - wps[i].time) / segLen : 1.0f;
          localT = glm::clamp(localT, 0.0f, 1.0f);
          segA = i;
          segB = i + 1;
          break;
        }
      }
    }

    if (anim.easing == EasingType::SMOOTHSTEP) {
      localT = localT * localT * (3.0f - 2.0f * localT);
    }

    tc->position = glm::mix(wps[segA].position, wps[segB].position, localT);
    tc->rotation = glm::slerp(wps[segA].rotation, wps[segB].rotation, localT);
  }
}
