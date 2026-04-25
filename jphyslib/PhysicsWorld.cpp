#include "pch.h"

#include "PhysicsWorld.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <future>
#include <limits>
#include <thread>
#include <string>
#include <unordered_map>

#include <glm/gtc/quaternion.hpp>

namespace jphys {

static float getMaxScaleComponent(const PhysicsObject& obj) {
  const glm::vec3 s = glm::abs(obj.getScale());
  return std::max(s.x, std::max(s.y, s.z));
}

static float getBroadphaseRadius(const PhysicsObject& obj) {
  const Collider& c = obj.getCollider();
  const float scaleMax = getMaxScaleComponent(obj);

  switch (c.getType()) {
    case ColliderType::Sphere:
      return c.getRadius() * scaleMax;
    case ColliderType::AABB:
      return glm::length(c.getHalfExtents() * glm::abs(obj.getScale()));
    case ColliderType::Plane:
      if (!c.isFinite()) return std::numeric_limits<float>::infinity();
      return glm::length(c.getHalfExtents() * glm::abs(obj.getScale()));
    case ColliderType::Cylinder: {
      const float r = c.getRadius() * scaleMax;
      const float h = 0.5f * c.getHeight() * scaleMax;
      return std::sqrt(r * r + h * h);
    }
    case ColliderType::Capsule:
      return (0.5f * c.getHeight() + c.getRadius()) * scaleMax;
    case ColliderType::Cone: {
      const float r = c.getRadius() * scaleMax;
      const float h = 0.5f * c.getHeight() * scaleMax;
      return std::sqrt(r * r + h * h);
    }
  }

  return std::numeric_limits<float>::infinity();
}

static bool broadphaseMayCollide(const PhysicsObject& a,
                                 const PhysicsObject& b) {
  const float ra = getBroadphaseRadius(a);
  const float rb = getBroadphaseRadius(b);
  if (!std::isfinite(ra) || !std::isfinite(rb)) return true;

  const glm::vec3 d = a.getPosition() - b.getPosition();
  const float r = ra + rb;
  return glm::dot(d, d) <= r * r;
}

struct GridKey {
  int x;
  int y;
  int z;

  bool operator==(const GridKey& o) const {
    return x == o.x && y == o.y && z == o.z;
  }
};

struct GridKeyHash {
  size_t operator()(const GridKey& k) const {
    const size_t hx = std::hash<int>{}(k.x);
    const size_t hy = std::hash<int>{}(k.y);
    const size_t hz = std::hash<int>{}(k.z);
    return hx ^ (hy * 0x9e3779b1u) ^ (hz * 0x85ebca6bu);
  }
};

// Pair names indexed by (typeA * 6 + typeB).
// ColliderType: Sphere=0, AABB=1, Plane=2, Cylinder=3, Capsule=4, Cone=5
static const char* const kCollisionPairNames[36] = {
  "Sphere / Sphere",     // 0
  "Sphere / AABB",       // 1
  "Sphere / Plane",      // 2
  "Sphere / Cylinder",   // 3
  "Sphere / Capsule",    // 4
  "Sphere / Cone",       // 5
  "AABB / Sphere",       // 6
  "AABB / AABB",         // 7
  "AABB / Plane",        // 8
  "AABB / Cylinder",     // 9
  "AABB / Capsule",      // 10
  nullptr,               // 11
  nullptr,               // 12 (Plane/Sphere — planes are always B after swap)
  nullptr,               // 13
  nullptr,               // 14
  nullptr,               // 15
  nullptr,               // 16
  nullptr,               // 17
  "Cylinder / Sphere",   // 18
  "Cylinder / AABB",     // 19
  "Cylinder / Plane",    // 20
  "Cylinder / Cylinder", // 21
  "Cylinder / Capsule",  // 22
  nullptr,               // 23
  "Capsule / Sphere",    // 24
  "Capsule / AABB",      // 25
  "Capsule / Plane",     // 26
  "Capsule / Cylinder",  // 27
  "Capsule / Capsule",   // 28
  nullptr,               // 29
  "Cone / Sphere",       // 30
  nullptr,               // 31
  "Cone / Plane",        // 32
  nullptr,               // 33
  nullptr,               // 34
  nullptr,               // 35
};

struct ContactPair {
  PhysicsObject* a;
  PhysicsObject* b;
  CollisionResult result;
};

static uint64_t makePairKey(uint32_t a, uint32_t b) {
  const uint32_t lo = std::min(a, b);
  const uint32_t hi = std::max(a, b);
  return (static_cast<uint64_t>(lo) << 32) | static_cast<uint64_t>(hi);
}

static void decodePairKey(uint64_t k, uint32_t& a, uint32_t& b) {
  a = static_cast<uint32_t>(k >> 32);
  b = static_cast<uint32_t>(k & 0xffffffffu);
}

PhysicsWorld::PhysicsWorld() {}

void PhysicsWorld::addObject(PhysicsObject* obj) {
  if (obj) {
    objects.push_back(obj);
  }
}

void PhysicsWorld::removeObject(PhysicsObject* obj) {
  objects.erase(std::remove(objects.begin(), objects.end(), obj),
                objects.end());
}

void PhysicsWorld::clear() { objects.clear(); }

void PhysicsWorld::step(float deltaTime) {
  if (deltaTime <= 0.0f) return;
  integrate(deltaTime);
  resolveCollisions();
}

void PhysicsWorld::integrate(float deltaTime) {
  for (auto* obj : objects) {
    if (!obj || obj->isStatic() || obj->isAsleep()) continue;

    glm::vec3 vel = obj->getVelocity();

    if (obj->getUseGravity()) {
      vel += gravity * deltaTime;
    }

    vel += obj->getAcceleration() * deltaTime;
    const float dampFactor = std::pow(obj->getDamping(), deltaTime);
    vel *= dampFactor;

    obj->setVelocity(vel);
    obj->setPosition(obj->getPosition() + vel * deltaTime);

    {
      glm::mat3 Iinv = obj->getWorldInverseInertiaTensor();
      glm::vec3 alpha = Iinv * obj->getTorque();
      glm::vec3 omega = obj->getAngularVelocity() + alpha * deltaTime;
      omega *= dampFactor;
      obj->setAngularVelocity(omega);

      const float omegaLen = glm::length(omega);
      const float angle = omegaLen * deltaTime;
      if (angle > 0.0001f) {
        glm::vec3 axis = omega / omegaLen;
        glm::quat dq = glm::angleAxis(angle, axis);
        obj->setOrientation(glm::normalize(dq * obj->getOrientation()));
      }
    }
    obj->clearTorque();
    obj->tickSleep(deltaTime, sleepLinearThreshSq, sleepAngularThreshSq, sleepDelay);
  }
}

long long PhysicsWorld::getLastCollisionChecks() const {
  long long total = 0;
  for (const auto& p : lastCollisionStats) total += p.checks;
  return total;
}

long long PhysicsWorld::getLastCollisionsResolved() const {
  long long total = 0;
  for (const auto& p : lastCollisionStats) total += p.resolved;
  return total;
}

void PhysicsWorld::recordCollision(int pairIdx, bool resolved) {
  const char* name = kCollisionPairNames[pairIdx];
  if (!name) return;
  if (pairStatIndex[pairIdx] < 0) {
    pairStatIndex[pairIdx] = static_cast<int>(lastCollisionStats.size());
    lastCollisionStats.push_back({name, 1, resolved ? 1LL : 0LL});
  } else {
    auto& stat = lastCollisionStats[pairStatIndex[pairIdx]];
    ++stat.checks;
    if (resolved) ++stat.resolved;
  }
}

void PhysicsWorld::resolveCollisions() {
  lastCollisionStats.clear();
  std::fill(pairStatIndex, pairStatIndex + 36, -1);

  std::vector<PhysicsObject*> activeObjects;
  activeObjects.reserve(objects.size());
  for (PhysicsObject* obj : objects) {
    if (obj) activeObjects.push_back(obj);
  }
  if (activeObjects.size() < 2) return;

  // Dynamic cell size: 2x the median broadphase radius, clamped to [2, 64].
  float gridCellSize = 8.0f;
  {
    std::vector<float> radii;
    radii.reserve(activeObjects.size());
    for (const auto* obj : activeObjects) {
      const float r = getBroadphaseRadius(*obj);
      if (std::isfinite(r) && r > 0.0f) radii.push_back(r);
    }
    if (!radii.empty()) {
      const size_t mid = radii.size() / 2;
      std::nth_element(radii.begin(), radii.begin() + mid, radii.end());
      gridCellSize = std::max(2.0f, std::min(64.0f, radii[mid] * 2.0f));
    }
  }
  const float invCellSize = 1.0f / gridCellSize;

  std::unordered_map<GridKey, std::vector<uint32_t>, GridKeyHash> grid;
  grid.reserve(activeObjects.size() * 2);

  std::vector<uint32_t> globalObjects;
  globalObjects.reserve(activeObjects.size());

  for (uint32_t i = 0; i < static_cast<uint32_t>(activeObjects.size()); ++i) {
    const PhysicsObject& obj = *activeObjects[i];
    const float r = getBroadphaseRadius(obj);
    if (!std::isfinite(r)) {
      globalObjects.push_back(i);
      continue;
    }

    const glm::vec3 p = obj.getPosition();
    const glm::vec3 bmin = p - glm::vec3(r);
    const glm::vec3 bmax = p + glm::vec3(r);

    const int minX = static_cast<int>(std::floor(bmin.x * invCellSize));
    const int minY = static_cast<int>(std::floor(bmin.y * invCellSize));
    const int minZ = static_cast<int>(std::floor(bmin.z * invCellSize));
    const int maxX = static_cast<int>(std::floor(bmax.x * invCellSize));
    const int maxY = static_cast<int>(std::floor(bmax.y * invCellSize));
    const int maxZ = static_cast<int>(std::floor(bmax.z * invCellSize));

    // If the object spans too many cells it would flood the grid; treat as global.
    static constexpr int kMaxCellSpan = 16;
    if ((maxX - minX) > kMaxCellSpan || (maxY - minY) > kMaxCellSpan ||
        (maxZ - minZ) > kMaxCellSpan) {
      globalObjects.push_back(i);
      continue;
    }

    for (int x = minX; x <= maxX; ++x) {
      for (int y = minY; y <= maxY; ++y) {
        for (int z = minZ; z <= maxZ; ++z) {
          grid[{x, y, z}].push_back(i);
        }
      }
    }
  }

  std::vector<const std::vector<uint32_t>*> buckets;
  buckets.reserve(grid.size());
  for (auto& kv : grid) {
    if (kv.second.size() > 1) buckets.push_back(&kv.second);
  }

  // Returns candidate pairs (may contain duplicates; caller deduplicates).
  auto buildPairsForRange = [&](size_t begin, size_t end) {
    std::vector<uint64_t> local;
    for (size_t bi = begin; bi < end; ++bi) {
      const auto& bucket = *buckets[bi];
      for (size_t i = 0; i < bucket.size(); ++i) {
        PhysicsObject* a = activeObjects[bucket[i]];
        for (size_t j = i + 1; j < bucket.size(); ++j) {
          PhysicsObject* b = activeObjects[bucket[j]];
          if (a->isStatic() && b->isStatic()) continue;
          local.push_back(makePairKey(bucket[i], bucket[j]));
        }
      }
    }
    return local;
  };

  std::vector<uint64_t> candidatePairs;
  if (!buckets.empty()) {
    const unsigned int hw = std::thread::hardware_concurrency();
    const unsigned int workerCount = std::max(1u, std::min<unsigned int>(
        hw > 1 ? hw - 1 : 1, static_cast<unsigned int>(buckets.size())));

    if (workerCount > 1 && buckets.size() >= 16) {
      std::vector<std::future<std::vector<uint64_t>>> jobs;
      jobs.reserve(workerCount);
      const size_t chunk = (buckets.size() + workerCount - 1) / workerCount;
      for (unsigned int w = 0; w < workerCount; ++w) {
        const size_t begin = static_cast<size_t>(w) * chunk;
        if (begin >= buckets.size()) break;
        const size_t end = std::min(begin + chunk, buckets.size());
        jobs.push_back(std::async(std::launch::async, buildPairsForRange, begin, end));
      }
      for (auto& job : jobs) {
        auto local = job.get();
        candidatePairs.insert(candidatePairs.end(), local.begin(), local.end());
      }
    } else {
      candidatePairs = buildPairsForRange(0, buckets.size());
    }
  }

  for (uint32_t gi : globalObjects) {
    for (uint32_t i = 0; i < static_cast<uint32_t>(activeObjects.size()); ++i) {
      if (i == gi) continue;
      PhysicsObject* a = activeObjects[gi];
      PhysicsObject* b = activeObjects[i];
      if (a->isStatic() && b->isStatic()) continue;
      candidatePairs.push_back(makePairKey(gi, i));
    }
  }

  // Deduplicate: sort + unique is more cache-friendly than unordered_set.
  std::sort(candidatePairs.begin(), candidatePairs.end());
  candidatePairs.erase(
      std::unique(candidatePairs.begin(), candidatePairs.end()),
      candidatePairs.end());

  std::vector<ContactPair> contacts;
  contacts.reserve(candidatePairs.size());

  auto handleCollision = [&](PhysicsObject* a, PhysicsObject* b, CollisionResult r, int idx) {
    recordCollision(idx, r.collided);
    if (!r.collided) return;
    a->wakeUp();
    if (!b->isStatic()) b->wakeUp();
    contacts.push_back({a, b, r});
  };

  for (uint64_t pairKey : candidatePairs) {
    uint32_t ia = 0;
    uint32_t ib = 0;
    decodePairKey(pairKey, ia, ib);

    PhysicsObject* objA = activeObjects[ia];
    PhysicsObject* objB = activeObjects[ib];

    if (objA->isStatic() && !objB->isStatic())
      std::swap(objA, objB);

    if (!broadphaseMayCollide(*objA, *objB)) continue;

    const ColliderType typeA = objA->getCollider().getType();
    const ColliderType typeB = objB->getCollider().getType();
    const int pairIdx = static_cast<int>(typeA) * 6 + static_cast<int>(typeB);

    if (typeA == ColliderType::Sphere && typeB == ColliderType::Plane) {
      handleCollision(objA, objB, testSpherePlane(*objA, *objB), pairIdx);
    } else if (typeA == ColliderType::Sphere && typeB == ColliderType::Sphere) {
      handleCollision(objA, objB, testSphereSphere(*objA, *objB), pairIdx);
    } else if (typeA == ColliderType::Sphere && typeB == ColliderType::AABB) {
      handleCollision(objA, objB, testSphereAABB(*objA, *objB), pairIdx);
    } else if (typeA == ColliderType::Sphere && typeB == ColliderType::Cylinder) {
      handleCollision(objA, objB, testSphereCylinder(*objA, *objB), pairIdx);
    } else if (typeA == ColliderType::Sphere && typeB == ColliderType::Capsule) {
      auto r = testCapsuleSphere(*objB, *objA);
      if (r.collided) r.normal = -r.normal;
      handleCollision(objA, objB, r, pairIdx);
    } else if (typeA == ColliderType::Sphere && typeB == ColliderType::Cone) {
      auto r = testConeSphere(*objB, *objA);
      if (r.collided) r.normal = -r.normal;
      handleCollision(objA, objB, r, pairIdx);
    } else if (typeA == ColliderType::AABB && typeB == ColliderType::Sphere) {
      auto r = testSphereAABB(*objB, *objA);
      if (r.collided) r.normal = -r.normal;
      handleCollision(objA, objB, r, pairIdx);
    } else if (typeA == ColliderType::AABB && typeB == ColliderType::AABB) {
      handleCollision(objA, objB, testAABBAABB(*objA, *objB), pairIdx);
    } else if (typeA == ColliderType::AABB && typeB == ColliderType::Plane) {
      handleCollision(objA, objB, testAABBPlane(*objA, *objB), pairIdx);
    } else if (typeA == ColliderType::AABB && typeB == ColliderType::Cylinder) {
      handleCollision(objA, objB, testAABBCylinder(*objA, *objB), pairIdx);
    } else if (typeA == ColliderType::AABB && typeB == ColliderType::Capsule) {
      auto r = testCapsuleAABB(*objB, *objA);
      if (r.collided) r.normal = -r.normal;
      handleCollision(objA, objB, r, pairIdx);
    } else if (typeA == ColliderType::Cylinder && typeB == ColliderType::Sphere) {
      auto r = testSphereCylinder(*objB, *objA);
      if (r.collided) r.normal = -r.normal;
      handleCollision(objA, objB, r, pairIdx);
    } else if (typeA == ColliderType::Cylinder && typeB == ColliderType::AABB) {
      auto r = testAABBCylinder(*objB, *objA);
      if (r.collided) r.normal = -r.normal;
      handleCollision(objA, objB, r, pairIdx);
    } else if (typeA == ColliderType::Cylinder && typeB == ColliderType::Plane) {
      handleCollision(objA, objB, testCylinderPlane(*objA, *objB), pairIdx);
    } else if (typeA == ColliderType::Cylinder && typeB == ColliderType::Cylinder) {
      handleCollision(objA, objB, testCylinderCylinder(*objA, *objB), pairIdx);
    } else if (typeA == ColliderType::Cylinder && typeB == ColliderType::Capsule) {
      auto r = testCapsuleCylinder(*objB, *objA);
      if (r.collided) r.normal = -r.normal;
      handleCollision(objA, objB, r, pairIdx);
    } else if (typeA == ColliderType::Capsule && typeB == ColliderType::Sphere) {
      handleCollision(objA, objB, testCapsuleSphere(*objA, *objB), pairIdx);
    } else if (typeA == ColliderType::Capsule && typeB == ColliderType::AABB) {
      handleCollision(objA, objB, testCapsuleAABB(*objA, *objB), pairIdx);
    } else if (typeA == ColliderType::Capsule && typeB == ColliderType::Plane) {
      handleCollision(objA, objB, testCapsulePlane(*objA, *objB), pairIdx);
    } else if (typeA == ColliderType::Capsule && typeB == ColliderType::Cylinder) {
      handleCollision(objA, objB, testCapsuleCylinder(*objA, *objB), pairIdx);
    } else if (typeA == ColliderType::Capsule && typeB == ColliderType::Capsule) {
      handleCollision(objA, objB, testCapsuleCapsule(*objA, *objB), pairIdx);
    } else if (typeA == ColliderType::Cone && typeB == ColliderType::Plane) {
      handleCollision(objA, objB, testConePlane(*objA, *objB), pairIdx);
    } else if (typeA == ColliderType::Cone && typeB == ColliderType::Sphere) {
      handleCollision(objA, objB, testConeSphere(*objA, *objB), pairIdx);
    }
  }

  for (auto& cp : contacts) {
    applyPositionCorrection(*cp.a, *cp.b, cp.result);
  }

  for (int iter = 0; iter < solverIterations; ++iter) {
    for (auto& cp : contacts) {
      applyVelocityImpulse(*cp.a, *cp.b, cp.result);
    }
  }
}

CollisionResult PhysicsWorld::testSphereSphere(const PhysicsObject& a,
                                               const PhysicsObject& b) {
  CollisionResult result;

  glm::vec3 diff = a.getPosition() - b.getPosition();
  float dist = glm::length(diff);
  float minDist = a.getCollider().getRadius() + b.getCollider().getRadius();

  if (dist < minDist && dist > 0.0001f) {
    result.collided = true;
    result.normal = diff / dist;
    result.penetration = minDist - dist;
    result.contactPoint =
        a.getPosition() - result.normal * a.getCollider().getRadius();
  }

  return result;
}

CollisionResult PhysicsWorld::testSpherePlane(const PhysicsObject& sphere,
                                              const PhysicsObject& plane) {
  CollisionResult result;

  glm::vec3 normal = plane.getCollider().getNormal();
  glm::vec3 relPos = sphere.getPosition() - plane.getPosition();
  float dist = glm::dot(relPos, normal);
  float penetration = sphere.getCollider().getRadius() - dist;

  if (penetration > 0.0f) {
    if (plane.getCollider().isFinite()) {
      glm::vec3 contact = sphere.getPosition() - normal * dist;
      glm::vec3 localContact = contact - plane.getPosition();
      glm::vec3 he = plane.getCollider().getHalfExtents() * plane.getScale();
      glm::vec3 absN = glm::abs(normal);
      glm::vec3 tangent1, tangent2;
      if (absN.y > 0.5f) {
        tangent1 = glm::vec3(1.0f, 0.0f, 0.0f);
        tangent2 = glm::vec3(0.0f, 0.0f, 1.0f);
      } else if (absN.x > 0.5f) {
        tangent1 = glm::vec3(0.0f, 1.0f, 0.0f);
        tangent2 = glm::vec3(0.0f, 0.0f, 1.0f);
      } else {
        tangent1 = glm::vec3(1.0f, 0.0f, 0.0f);
        tangent2 = glm::vec3(0.0f, 1.0f, 0.0f);
      }
      float proj1 = glm::dot(localContact, tangent1);
      float proj2 = glm::dot(localContact, tangent2);
      float limit1 = glm::dot(he, glm::abs(tangent1));
      float limit2 = glm::dot(he, glm::abs(tangent2));
      if (std::abs(proj1) > limit1 || std::abs(proj2) > limit2) {
        return result;
      }
    }

    result.collided = true;
    result.normal = normal;
    result.penetration = penetration;
    result.contactPoint = sphere.getPosition() - normal * dist;
  }

  return result;
}

CollisionResult PhysicsWorld::testSphereAABB(const PhysicsObject& sphere,
                                             const PhysicsObject& aabb) {
  CollisionResult result;

  glm::vec3 boxMin = aabb.getPosition() -
                     aabb.getCollider().getHalfExtents() * aabb.getScale();
  glm::vec3 boxMax = aabb.getPosition() +
                     aabb.getCollider().getHalfExtents() * aabb.getScale();
  glm::vec3 closest = glm::clamp(sphere.getPosition(), boxMin, boxMax);
  glm::vec3 diff = sphere.getPosition() - closest;
  float dist = glm::length(diff);

  if (dist < sphere.getCollider().getRadius() && dist > 0.0001f) {
    result.collided = true;
    result.normal = diff / dist;
    result.penetration = sphere.getCollider().getRadius() - dist;
    result.contactPoint = closest;
  }

  return result;
}


CollisionResult PhysicsWorld::testCylinderPlane(const PhysicsObject& cylinder,
                                                const PhysicsObject& plane) {
  CollisionResult result;

  glm::vec3 n = plane.getCollider().getNormal();
  glm::vec3 relPos = cylinder.getPosition() - plane.getPosition();

  glm::mat3 R = glm::mat3_cast(cylinder.getOrientation());
  glm::vec3 axis = R[2];

  float r = cylinder.getCollider().getRadius();
  float halfH = cylinder.getCollider().getHeight() * 0.5f;

  float axisDot = glm::dot(axis, n);
  float radialExtent = r * std::sqrt(std::max(0.0f, 1.0f - axisDot * axisDot));
  float extent = std::abs(axisDot) * halfH + radialExtent;

  float dist = glm::dot(relPos, n);
  float penetration = extent - dist;

  if (penetration > 0.0f) {
    if (plane.getCollider().isFinite()) {
      glm::vec3 contact = cylinder.getPosition() - n * dist;
      glm::vec3 localContact = contact - plane.getPosition();
      glm::vec3 he = plane.getCollider().getHalfExtents() * plane.getScale();
      glm::vec3 absN = glm::abs(n);
      glm::vec3 tangent1, tangent2;
      if (absN.y > 0.5f) {
        tangent1 = glm::vec3(1, 0, 0);
        tangent2 = glm::vec3(0, 0, 1);
      } else if (absN.x > 0.5f) {
        tangent1 = glm::vec3(0, 1, 0);
        tangent2 = glm::vec3(0, 0, 1);
      } else {
        tangent1 = glm::vec3(1, 0, 0);
        tangent2 = glm::vec3(0, 1, 0);
      }
      float proj1 = glm::dot(localContact, tangent1);
      float proj2 = glm::dot(localContact, tangent2);
      float lim1 = glm::dot(he, glm::abs(tangent1));
      float lim2 = glm::dot(he, glm::abs(tangent2));
      if (std::abs(proj1) > lim1 + r || std::abs(proj2) > lim2 + r)
        return result;
    }
    result.collided = true;
    result.normal = n;
    result.penetration = penetration;
    result.contactPoint = cylinder.getPosition() - n * dist;
  }
  return result;
}

void PhysicsWorld::applyPositionCorrection(PhysicsObject& a, PhysicsObject& b,
                                           const CollisionResult& result) {
  const glm::vec3 n  = result.normal;
  const bool bStatic = b.isStatic();

  if (bStatic) {
    a.setPosition(a.getPosition() + n * result.penetration);
  } else {
    const float totalMass = a.getMass() + b.getMass();
    if (totalMass < 1e-10f) return;
    a.setPosition(a.getPosition() + n * result.penetration * (b.getMass() / totalMass));
    b.setPosition(b.getPosition() - n * result.penetration * (a.getMass() / totalMass));
  }
}

void PhysicsWorld::applyVelocityImpulse(PhysicsObject& a, PhysicsObject& b,
                                        const CollisionResult& result) {
  if (a.getMass() <= 0.0f) return;
  const glm::vec3 n  = result.normal;
  const bool bStatic = b.isStatic();

  const glm::vec3 rA = result.contactPoint - a.getPosition();
  const glm::vec3 rB = result.contactPoint - b.getPosition();

  const glm::vec3 vA = a.getVelocity() + glm::cross(a.getAngularVelocity(), rA);
  const glm::vec3 vB = bStatic ? glm::vec3(0.0f)
                                : b.getVelocity() + glm::cross(b.getAngularVelocity(), rB);
  const glm::vec3 relVel = vA - vB;

  const float relVelN = glm::dot(relVel, n);
  if (relVelN >= 0.0f) return;

  const glm::mat3& IinvA = a.getWorldInverseInertiaTensor();
  const float invMA = 1.0f / a.getMass();
  const float invMB = bStatic ? 0.0f : 1.0f / b.getMass();

  auto angTerm = [](const glm::mat3& Iinv, const glm::vec3& r, const glm::vec3& d) {
    return glm::dot(d, glm::cross(Iinv * glm::cross(r, d), r));
  };

  float denomN = invMA + invMB + angTerm(IinvA, rA, n);
  if (!bStatic) denomN += angTerm(b.getWorldInverseInertiaTensor(), rB, n);
  if (denomN < 1e-10f) return;

  const float e  = std::min(a.getRestitution(), bStatic ? a.getRestitution() : b.getRestitution());
  const float jN = -(1.0f + e) * relVelN / denomN;
  const glm::vec3 impN = n * jN;

  a.setVelocity(a.getVelocity() + impN * invMA);
  a.setAngularVelocity(a.getAngularVelocity() + IinvA * glm::cross(rA, impN));
  if (!bStatic) {
    const glm::mat3& IinvB = b.getWorldInverseInertiaTensor();
    b.setVelocity(b.getVelocity() - impN * invMB);
    b.setAngularVelocity(b.getAngularVelocity() - IinvB * glm::cross(rB, impN));
  }

  const glm::vec3 tangVel = relVel - n * relVelN;
  const float tangSpeed = glm::length(tangVel);
  if (tangSpeed < 1e-4f) return;

  const glm::vec3 t = tangVel / tangSpeed;
  float denomT = invMA + invMB + angTerm(IinvA, rA, t);
  if (!bStatic) denomT += angTerm(b.getWorldInverseInertiaTensor(), rB, t);
  if (denomT < 1e-10f) return;

  const float mu = std::sqrt(a.getFriction() * (bStatic ? a.getFriction() : b.getFriction()));
  const float maxFric = mu * std::abs(jN);
  const float jT = std::max(-maxFric, std::min(maxFric, -tangSpeed / denomT));
  const glm::vec3 impT = t * jT;

  a.setVelocity(a.getVelocity() + impT * invMA);
  a.setAngularVelocity(a.getAngularVelocity() + IinvA * glm::cross(rA, impT));
  if (!bStatic) {
    const glm::mat3& IinvB = b.getWorldInverseInertiaTensor();
    b.setVelocity(b.getVelocity() - impT * invMB);
    b.setAngularVelocity(b.getAngularVelocity() - IinvB * glm::cross(rB, impT));
  }
}

// ── Closest points on two line segments ──────────────────────────────────────
static std::pair<glm::vec3, glm::vec3> closestSegmentPoints(
    const glm::vec3& p1, const glm::vec3& d1, float h1,
    const glm::vec3& p2, const glm::vec3& d2, float h2) {
  glm::vec3 r  = p1 - p2;
  float     b  = glm::dot(d1, d2);
  float     f  = glm::dot(d2, r);
  float     c  = glm::dot(d1, r);
  float     denom = 1.0f - b * b;
  float     s, t;
  if (std::abs(denom) < 1e-6f) {
    s = 0.0f;
    t = std::max(-h2, std::min(h2, f));
  } else {
    s = std::max(-h1, std::min(h1, (b * f - c) / denom));
    t = b * s + f;
    if (t < -h2) {
      t = -h2;
      s = std::max(-h1, std::min(h1, (b * t - c)));
    } else if (t > h2) {
      t = h2;
      s = std::max(-h1, std::min(h1, (b * t - c)));
    }
  }
  return {p1 + d1 * s, p2 + d2 * t};
}

CollisionResult PhysicsWorld::testAABBPlane(const PhysicsObject& aabb,
                                             const PhysicsObject& plane) {
  CollisionResult result;
  glm::vec3 n          = plane.getCollider().getNormal();
  glm::vec3 relPos     = aabb.getPosition() - plane.getPosition();
  float     projCenter = glm::dot(relPos, n);
  glm::vec3 he         = aabb.getCollider().getHalfExtents() * aabb.getScale();
  float     effRadius  = std::abs(he.x * n.x) + std::abs(he.y * n.y) + std::abs(he.z * n.z);
  float     penetration = effRadius - projCenter;

  if (penetration > 0.0f) {
    if (plane.getCollider().isFinite()) {
      glm::vec3 contact      = aabb.getPosition() - n * projCenter;
      glm::vec3 localContact = contact - plane.getPosition();
      glm::vec3 phe          = plane.getCollider().getHalfExtents() * plane.getScale();
      glm::vec3 absN         = glm::abs(n);
      glm::vec3 t1, t2;
      if (absN.y > 0.5f)      { t1 = {1,0,0}; t2 = {0,0,1}; }
      else if (absN.x > 0.5f) { t1 = {0,1,0}; t2 = {0,0,1}; }
      else                    { t1 = {1,0,0}; t2 = {0,1,0}; }
      if (std::abs(glm::dot(localContact, t1)) > glm::dot(phe, glm::abs(t1)) + glm::dot(he, glm::abs(t1)) ||
          std::abs(glm::dot(localContact, t2)) > glm::dot(phe, glm::abs(t2)) + glm::dot(he, glm::abs(t2)))
        return result;
    }
    result.collided      = true;
    result.normal        = n;
    result.penetration   = penetration;
    result.contactPoint  = aabb.getPosition() - n * projCenter;
  }
  return result;
}

CollisionResult PhysicsWorld::testAABBAABB(const PhysicsObject& a,
                                            const PhysicsObject& b) {
  CollisionResult result;
  glm::vec3 heA  = a.getCollider().getHalfExtents() * a.getScale();
  glm::vec3 heB  = b.getCollider().getHalfExtents() * b.getScale();
  glm::vec3 diff = a.getPosition() - b.getPosition();

  float ox = (heA.x + heB.x) - std::abs(diff.x);
  float oy = (heA.y + heB.y) - std::abs(diff.y);
  float oz = (heA.z + heB.z) - std::abs(diff.z);
  if (ox <= 0.0f || oy <= 0.0f || oz <= 0.0f) return result;

  glm::vec3 normal;
  float     penetration;
  if (ox <= oy && ox <= oz) {
    penetration = ox;
    normal      = {diff.x > 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f};
  } else if (oy <= ox && oy <= oz) {
    penetration = oy;
    normal      = {0.0f, diff.y > 0.0f ? 1.0f : -1.0f, 0.0f};
  } else {
    penetration = oz;
    normal      = {0.0f, 0.0f, diff.z > 0.0f ? 1.0f : -1.0f};
  }

  result.collided     = true;
  result.normal       = normal;
  result.penetration  = penetration;
  result.contactPoint = (a.getPosition() + b.getPosition()) * 0.5f;
  return result;
}

CollisionResult PhysicsWorld::testSphereCylinder(const PhysicsObject& sphere,
                                                  const PhysicsObject& cylinder) {
  CollisionResult result;
  glm::mat3 R      = glm::mat3_cast(cylinder.getOrientation());
  glm::vec3 axis   = R[2];
  glm::vec3 relPos = sphere.getPosition() - cylinder.getPosition();
  float r_cyl      = cylinder.getCollider().getRadius();
  float halfH      = cylinder.getCollider().getHeight() * 0.5f;
  float r_sph      = sphere.getCollider().getRadius();

  float     axDist   = glm::dot(relPos, axis);
  glm::vec3 radVec   = relPos - axis * axDist;
  float     radDist  = glm::length(radVec);

  bool inAxial  = axDist >= -halfH && axDist <= halfH;
  bool inRadial = radDist <= r_cyl;

  if (inAxial && inRadial) {
    float sideDepth = r_cyl - radDist;
    float capDepth  = halfH - std::abs(axDist);
    if (sideDepth < capDepth) {
      glm::vec3 dir = (radDist > 1e-4f) ? (radVec / radDist) : glm::vec3(1,0,0);
      result = {true, dir, sideDepth + r_sph,
                cylinder.getPosition() + axis * axDist + dir * r_cyl};
    } else {
      float     s   = axDist > 0 ? 1.0f : -1.0f;
      result = {true, axis * s, capDepth + r_sph,
                cylinder.getPosition() + axis * halfH * s + radVec};
    }
    return result;
  }

  glm::vec3 closestPoint;
  if (inAxial && !inRadial) {
    glm::vec3 dir  = radVec / radDist;
    closestPoint   = cylinder.getPosition() + axis * axDist + dir * r_cyl;
  } else if (!inAxial && inRadial) {
    float s        = axDist > 0 ? 1.0f : -1.0f;
    closestPoint   = cylinder.getPosition() + axis * halfH * s + radVec;
  } else {
    float     s   = axDist > 0 ? 1.0f : -1.0f;
    glm::vec3 dir = (radDist > 1e-4f) ? (radVec / radDist) : glm::vec3(1,0,0);
    closestPoint  = cylinder.getPosition() + axis * halfH * s + dir * r_cyl;
  }

  glm::vec3 toSph = sphere.getPosition() - closestPoint;
  float     dist  = glm::length(toSph);
  if (dist < r_sph && dist > 1e-4f) {
    result.collided     = true;
    result.normal       = toSph / dist;
    result.penetration  = r_sph - dist;
    result.contactPoint = closestPoint;
  }
  return result;
}

CollisionResult PhysicsWorld::testCylinderCylinder(const PhysicsObject& a,
                                                    const PhysicsObject& b) {
  CollisionResult result;
  glm::mat3 Ra = glm::mat3_cast(a.getOrientation());
  glm::mat3 Rb = glm::mat3_cast(b.getOrientation());
  glm::vec3 axA = Ra[2], axB = Rb[2];
  float rA     = a.getCollider().getRadius(), halfA = a.getCollider().getHeight() * 0.5f;
  float rB     = b.getCollider().getRadius(), halfB = b.getCollider().getHeight() * 0.5f;

  auto [cA, cB] = closestSegmentPoints(a.getPosition(), axA, halfA,
                                        b.getPosition(), axB, halfB);
  glm::vec3 diff = cA - cB;
  float     dist = glm::length(diff);
  float     minD = rA + rB;

  if (dist < minD && dist > 1e-4f) {
    result.collided     = true;
    result.normal       = diff / dist;
    result.penetration  = minD - dist;
    result.contactPoint = (cA + cB) * 0.5f;
  }
  return result;
}

CollisionResult PhysicsWorld::testAABBCylinder(const PhysicsObject& aabb,
                                                const PhysicsObject& cylinder) {
  CollisionResult result;
  glm::mat3 R    = glm::mat3_cast(cylinder.getOrientation());
  glm::vec3 axis = R[2];
  float r_cyl    = cylinder.getCollider().getRadius();
  float halfH    = cylinder.getCollider().getHeight() * 0.5f;
  glm::vec3 he   = aabb.getCollider().getHalfExtents() * aabb.getScale();

  glm::vec3 relPos = aabb.getPosition() - cylinder.getPosition();
  float t = std::max(-halfH, std::min(halfH, glm::dot(relPos, axis)));
  glm::vec3 axisPoint  = cylinder.getPosition() + axis * t;
  glm::vec3 aabbMin    = aabb.getPosition() - he;
  glm::vec3 aabbMax    = aabb.getPosition() + he;
  glm::vec3 closestBox = glm::clamp(axisPoint, aabbMin, aabbMax);
  glm::vec3 diff       = axisPoint - closestBox;
  float     dist       = glm::length(diff);

  if (dist < r_cyl) {
    glm::vec3 normal;
    float     pen;
    if (dist > 1e-4f) {
      normal = -diff / dist;
      pen    = r_cyl - dist;
    } else {
      glm::vec3 d  = axisPoint - aabb.getPosition();
      glm::vec3 ov = he - glm::abs(d);
      if (ov.x <= ov.y && ov.x <= ov.z) {
        normal = {d.x > 0.0f ? -1.0f : 1.0f, 0, 0};
        pen    = ov.x + r_cyl;
      } else if (ov.y <= ov.x && ov.y <= ov.z) {
        normal = {0, d.y > 0.0f ? -1.0f : 1.0f, 0};
        pen    = ov.y + r_cyl;
      } else {
        normal = {0, 0, d.z > 0.0f ? -1.0f : 1.0f};
        pen    = ov.z + r_cyl;
      }
    }
    result.collided     = true;
    result.normal       = normal;
    result.penetration  = pen;
    result.contactPoint = closestBox;
  }
  return result;
}

// ── Capsule helpers ───────────────────────────────────────────────────────────
static glm::vec3 capsuleAxis(const PhysicsObject& cap) {
  return glm::mat3_cast(cap.getOrientation())[1];
}
static float capsuleHalfCyl(const PhysicsObject& cap) {
  return std::max(0.0f, cap.getCollider().getHeight() * 0.5f - cap.getCollider().getRadius());
}

CollisionResult PhysicsWorld::testCapsulePlane(const PhysicsObject& capsule,
                                                const PhysicsObject& plane) {
  CollisionResult result;
  glm::vec3 ax   = capsuleAxis(capsule);
  float     hc   = capsuleHalfCyl(capsule);
  float     r    = capsule.getCollider().getRadius();
  glm::vec3 n    = plane.getCollider().getNormal();
  glm::vec3 p1   = capsule.getPosition() + ax * hc;
  glm::vec3 p2   = capsule.getPosition() - ax * hc;
  float     d1   = glm::dot(p1 - plane.getPosition(), n);
  float     d2   = glm::dot(p2 - plane.getPosition(), n);
  float     minD = std::min(d1, d2);
  float     pen  = r - minD;

  if (pen > 0.0f) {
    if (plane.getCollider().isFinite()) {
      glm::vec3 contact = capsule.getPosition() -
                          n * glm::dot(capsule.getPosition() - plane.getPosition(), n);
      glm::vec3 lc  = contact - plane.getPosition();
      glm::vec3 phe = plane.getCollider().getHalfExtents() * plane.getScale();
      glm::vec3 absN = glm::abs(n);
      glm::vec3 t1, t2;
      if (absN.y > 0.5f)      { t1 = {1,0,0}; t2 = {0,0,1}; }
      else if (absN.x > 0.5f) { t1 = {0,1,0}; t2 = {0,0,1}; }
      else                    { t1 = {1,0,0}; t2 = {0,1,0}; }
      if (std::abs(glm::dot(lc, t1)) > glm::dot(phe, glm::abs(t1)) + r ||
          std::abs(glm::dot(lc, t2)) > glm::dot(phe, glm::abs(t2)) + r)
        return result;
    }
    glm::vec3 lowestPt = (d1 < d2) ? p1 : p2;
    result.collided     = true;
    result.normal       = n;
    result.penetration  = pen;
    result.contactPoint = lowestPt - n * minD;
  }
  return result;
}

CollisionResult PhysicsWorld::testCapsuleSphere(const PhysicsObject& capsule,
                                                 const PhysicsObject& sphere) {
  CollisionResult result;
  glm::vec3 ax  = capsuleAxis(capsule);
  float     hc  = capsuleHalfCyl(capsule);
  float     rC  = capsule.getCollider().getRadius();
  float     rS  = sphere.getCollider().getRadius();

  glm::vec3 relPos = sphere.getPosition() - capsule.getPosition();
  float     t      = std::max(-hc, std::min(hc, glm::dot(relPos, ax)));
  glm::vec3 closest = capsule.getPosition() + ax * t;
  glm::vec3 diff   = sphere.getPosition() - closest;
  float     dist   = glm::length(diff);
  float     minD   = rC + rS;

  if (dist < minD && dist > 1e-4f) {
    result.collided     = true;
    result.normal       = diff / dist;
    result.penetration  = minD - dist;
    result.contactPoint = closest + (diff / dist) * rC;
  }
  return result;
}

CollisionResult PhysicsWorld::testCapsuleAABB(const PhysicsObject& capsule,
                                               const PhysicsObject& aabb) {
  CollisionResult result;
  glm::vec3 ax  = capsuleAxis(capsule);
  float     hc  = capsuleHalfCyl(capsule);
  float     r   = capsule.getCollider().getRadius();
  glm::vec3 he  = aabb.getCollider().getHalfExtents() * aabb.getScale();
  glm::vec3 aabbMin = aabb.getPosition() - he;
  glm::vec3 aabbMax = aabb.getPosition() + he;

  glm::vec3 relPos  = aabb.getPosition() - capsule.getPosition();
  float     t       = std::max(-hc, std::min(hc, glm::dot(relPos, ax)));
  glm::vec3 axPt    = capsule.getPosition() + ax * t;
  glm::vec3 closest = glm::clamp(axPt, aabbMin, aabbMax);
  glm::vec3 diff    = axPt - closest;
  float     dist    = glm::length(diff);

  if (dist < r) {
    glm::vec3 normal;
    float     pen;
    if (dist > 1e-4f) {
      normal = diff / dist;
      pen    = r - dist;
    } else {
      glm::vec3 d  = axPt - aabb.getPosition();
      glm::vec3 ov = he - glm::abs(d);
      if (ov.x <= ov.y && ov.x <= ov.z) {
        normal = {d.x > 0.0f ? 1.0f : -1.0f, 0, 0};
        pen    = ov.x + r;
      } else if (ov.y <= ov.x && ov.y <= ov.z) {
        normal = {0, d.y > 0.0f ? 1.0f : -1.0f, 0};
        pen    = ov.y + r;
      } else {
        normal = {0, 0, d.z > 0.0f ? 1.0f : -1.0f};
        pen    = ov.z + r;
      }
    }
    result.collided     = true;
    result.normal       = normal;
    result.penetration  = pen;
    result.contactPoint = closest;
  }
  return result;
}

CollisionResult PhysicsWorld::testCapsuleCylinder(const PhysicsObject& capsule,
                                                   const PhysicsObject& cylinder) {
  CollisionResult result;
  glm::vec3 axC = capsuleAxis(capsule);
  float     hc  = capsuleHalfCyl(capsule);
  float     rC  = capsule.getCollider().getRadius();
  glm::mat3 Rcyl = glm::mat3_cast(cylinder.getOrientation());
  glm::vec3 axCyl = Rcyl[2];
  float     hCyl  = cylinder.getCollider().getHeight() * 0.5f;
  float     rCyl  = cylinder.getCollider().getRadius();

  auto [pCap, pCyl] = closestSegmentPoints(capsule.getPosition(), axC, hc,
                                            cylinder.getPosition(), axCyl, hCyl);
  glm::vec3 diff = pCap - pCyl;
  float     dist = glm::length(diff);
  float     minD = rC + rCyl;

  if (dist < minD && dist > 1e-4f) {
    result.collided     = true;
    result.normal       = diff / dist;
    result.penetration  = minD - dist;
    result.contactPoint = pCyl + (diff / dist) * rCyl;
  }
  return result;
}

CollisionResult PhysicsWorld::testCapsuleCapsule(const PhysicsObject& a,
                                                  const PhysicsObject& b) {
  CollisionResult result;
  glm::vec3 axA = capsuleAxis(a), axB = capsuleAxis(b);
  float     hA  = capsuleHalfCyl(a), hB  = capsuleHalfCyl(b);
  float     rA  = a.getCollider().getRadius(), rB = b.getCollider().getRadius();

  auto [pA, pB] = closestSegmentPoints(a.getPosition(), axA, hA,
                                        b.getPosition(), axB, hB);
  glm::vec3 diff = pA - pB;
  float     dist = glm::length(diff);
  float     minD = rA + rB;

  if (dist < minD && dist > 1e-4f) {
    result.collided     = true;
    result.normal       = diff / dist;
    result.penetration  = minD - dist;
    result.contactPoint = (pA + pB) * 0.5f;
  }
  return result;
}

CollisionResult PhysicsWorld::testConePlane(const PhysicsObject& cone,
                                             const PhysicsObject& plane) {
  CollisionResult result;
  glm::mat3 R    = glm::mat3_cast(cone.getOrientation());
  glm::vec3 axis = R[1];
  glm::vec3 n    = plane.getCollider().getNormal();
  float     r    = cone.getCollider().getRadius();
  float     halfH = cone.getCollider().getHeight() * 0.5f;

  glm::vec3 apex       = cone.getPosition() + axis * halfH;
  glm::vec3 baseCenter = cone.getPosition() - axis * halfH;
  float     dApex      = glm::dot(apex - plane.getPosition(), n);
  float     dBase      = glm::dot(baseCenter - plane.getPosition(), n);
  float     axN        = glm::dot(axis, n);
  float     radN       = std::sqrt(std::max(0.0f, 1.0f - axN * axN));
  float     dRimMin    = dBase - radN * r;
  float     minD       = std::min(dApex, dRimMin);
  float     pen        = -minD;

  if (pen > 0.0f) {
    if (plane.getCollider().isFinite()) {
      glm::vec3 contact = cone.getPosition() -
                          n * glm::dot(cone.getPosition() - plane.getPosition(), n);
      glm::vec3 lc  = contact - plane.getPosition();
      glm::vec3 phe = plane.getCollider().getHalfExtents() * plane.getScale();
      glm::vec3 absN = glm::abs(n);
      glm::vec3 t1, t2;
      if (absN.y > 0.5f)      { t1 = {1,0,0}; t2 = {0,0,1}; }
      else if (absN.x > 0.5f) { t1 = {0,1,0}; t2 = {0,0,1}; }
      else                    { t1 = {1,0,0}; t2 = {0,1,0}; }
      if (std::abs(glm::dot(lc, t1)) > glm::dot(phe, glm::abs(t1)) + r ||
          std::abs(glm::dot(lc, t2)) > glm::dot(phe, glm::abs(t2)) + r)
        return result;
    }
    result.collided     = true;
    result.normal       = n;
    result.penetration  = pen;
    result.contactPoint =
        cone.getPosition() - n * glm::dot(cone.getPosition() - plane.getPosition(), n);
  }
  return result;
}

CollisionResult PhysicsWorld::testConeSphere(const PhysicsObject& cone,
                                              const PhysicsObject& sphere) {
  CollisionResult result;
  glm::mat3 R    = glm::mat3_cast(cone.getOrientation());
  glm::vec3 axis = R[1];
  float     r    = cone.getCollider().getRadius();
  float     h    = cone.getCollider().getHeight();
  float     halfH = h * 0.5f;
  float     rS   = sphere.getCollider().getRadius();

  glm::vec3 relPos = sphere.getPosition() - cone.getPosition();
  float     axDist = glm::dot(relPos, axis);
  glm::vec3 radVec = relPos - axis * axDist;
  float     radDist = glm::length(radVec);

  float clampedAx  = std::max(-halfH, std::min(halfH, axDist));
  float t          = (clampedAx + halfH) / h;
  float rAtT       = r * (1.0f - t);
  glm::vec3 radDir = (radDist > 1e-4f) ? (radVec / radDist) : glm::vec3(1,0,0);
  glm::vec3 closestOnSurface = cone.getPosition() + axis * clampedAx +
                                radDir * std::min(radDist, rAtT);

  glm::vec3 toSph = sphere.getPosition() - closestOnSurface;
  float     dist  = glm::length(toSph);
  if (dist < rS && dist > 1e-4f) {
    result.collided     = true;
    result.normal       = toSph / dist;
    result.penetration  = rS - dist;
    result.contactPoint = closestOnSurface;
  }
  return result;
}

}  // namespace jphys
