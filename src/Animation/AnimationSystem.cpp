#include "AnimationSystem.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Util/Debug.h"

void AnimationSystem::setRegistry(Registry* reg) {
  registry = reg;
  Debug::log(Debug::Category::ANIMATION, "AnimationSystem: Registry set");
}

void AnimationSystem::reset() {
  if (!registry) return;
  int resetCount = 0;
  for (auto&& [entity, anim] : registry->allAnimationsMut()) {
    anim.currentTime = 0.0f;
    anim.forward     = true;
    anim.active      = true;

    auto* tc = registry->getComponent<TransformComponent>(entity);
    if (tc && !anim.waypoints.empty()) {
      tc->position = anim.waypoints.front().position;
      tc->rotation = anim.waypoints.front().rotation;
    }
    ++resetCount;
  }
  Debug::log(Debug::Category::ANIMATION, "AnimationSystem: Reset ", resetCount, " animations to initial state");
}

void AnimationSystem::update(float dt) {
  if (!registry) return;

  int activeCount = 0;
  for (auto&& [entity, anim] : registry->allAnimationsMut()) {
    if (!anim.active || anim.waypoints.size() < 2) continue;

    auto* tc = registry->getComponent<TransformComponent>(entity);
    if (!tc) continue;

    ++activeCount;
    const float lastWpTime = anim.waypoints.back().time;
    Debug::logTrace(Debug::Category::ANIMATION,
        "AnimationSystem: entity ", entity,
        " t=", anim.currentTime, " forward=", anim.forward,
        " waypoints=", anim.waypoints.size());

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
          Debug::logVerbose(Debug::Category::ANIMATION,
              "AnimationSystem: entity ", entity, " STOP reached end at t=", anim.currentTime);
        }
        break;

      case PathMode::LOOP:
        if (anim.totalDuration > 0.0f && anim.currentTime >= anim.totalDuration) {
          anim.currentTime -= anim.totalDuration;
          if (anim.currentTime < 0.0f) anim.currentTime = 0.0f;
          Debug::logVerbose(Debug::Category::ANIMATION,
              "AnimationSystem: entity ", entity, " LOOP wrapped at duration=", anim.totalDuration);
        }
        break;

      case PathMode::REVERSE:
        if (anim.forward && anim.currentTime >= lastWpTime) {
          anim.currentTime = lastWpTime;
          anim.forward = false;
          Debug::logVerbose(Debug::Category::ANIMATION,
              "AnimationSystem: entity ", entity, " REVERSE direction -> backward");
        } else if (!anim.forward && anim.currentTime <= 0.0f) {
          anim.currentTime = 0.0f;
          anim.forward = true;
          Debug::logVerbose(Debug::Category::ANIMATION,
              "AnimationSystem: entity ", entity, " REVERSE direction -> forward");
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
    Debug::logTrace(Debug::Category::ANIMATION,
        "AnimationSystem: entity ", entity,
        " seg[", segA, "->", segB, "] localT=", localT,
        " pos=(", tc->position.x, ",", tc->position.y, ",", tc->position.z, ")");
  }
  Debug::logTrace(Debug::Category::ANIMATION,
      "AnimationSystem: update done, active=", activeCount, " dt=", dt);
}
