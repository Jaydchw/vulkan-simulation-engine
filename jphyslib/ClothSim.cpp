#include "pch.h"

#include "ClothSim.h"

#include "EnvironmentForces.h"

#include <algorithm>
#include <cmath>

namespace jphys {

ClothSim::ClothSim(int resX, int resZ, float width, float height,
                   const glm::vec3& origin, glm::mat3 orientation)
    : resX(resX), resZ(resZ) {
  particles.resize(resX * resZ);

  for (int z = 0; z < resZ; ++z) {
    for (int x = 0; x < resX; ++x) {
      float fx = (resX > 1) ? static_cast<float>(x) / (resX - 1) : 0.5f;
      float fz = (resZ > 1) ? static_cast<float>(z) / (resZ - 1) : 0.5f;
      glm::vec3 local((fx - 0.5f) * width, -fz * height, 0.0f);
      glm::vec3 pos = origin + orientation * local;
      int idx = particleIndex(x, z);
      particles[idx].position     = pos;
      particles[idx].prevPosition = pos;
      particles[idx].invMass      = 1.0f;
    }
  }

  auto addConstraint = [&](int a, int b, bool bending) {
    float len = glm::length(particles[b].position - particles[a].position);
    constraints.push_back({a, b, len, bending, false});
  };

  for (int z = 0; z < resZ; ++z) {
    for (int x = 0; x < resX; ++x) {
      if (x + 1 < resX) addConstraint(particleIndex(x, z), particleIndex(x + 1, z), false);
      if (z + 1 < resZ) addConstraint(particleIndex(x, z), particleIndex(x, z + 1), false);
      if (x + 1 < resX && z + 1 < resZ) {
        addConstraint(particleIndex(x, z),     particleIndex(x + 1, z + 1), false);
        addConstraint(particleIndex(x + 1, z), particleIndex(x,     z + 1), false);
      }
      if (x + 2 < resX) addConstraint(particleIndex(x, z), particleIndex(x + 2, z), true);
      if (z + 2 < resZ) addConstraint(particleIndex(x, z), particleIndex(x, z + 2), true);
    }
  }
}

void ClothSim::setParticleMass(float m) {
  float inv = (m > 0.0f) ? 1.0f / m : 0.0f;
  for (auto& p : particles) {
    if (p.invMass != 0.0f) p.invMass = inv;
  }
}

void ClothSim::pinParticle(int index) {
  if (index >= 0 && index < static_cast<int>(particles.size()))
    particles[index].invMass = 0.0f;
}

void ClothSim::step(float dt, const std::vector<PhysicsObject*>& obstacles) {
  integrate(dt);
  for (int i = 0; i < solverIterations; ++i) {
    satisfyConstraints();
    resolveCollisions(obstacles);
  }
}

void ClothSim::integrate(float dt) {
  float maxVelSq = maxVelocity * maxVelocity;
  float dt2 = dt * dt;

  for (auto& p : particles) {
    if (p.invMass == 0.0f) continue;
    glm::vec3 vel = (p.position - p.prevPosition) * damping;
    glm::vec3 accel(0.0f);
    if (useGravity) accel += gravity;
    accel += computeWindAcceleration(wind, vel, windDrag);
    float velSq = glm::dot(vel, vel);
    if (velSq > maxVelSq) vel *= maxVelocity / std::sqrt(velSq);
    p.prevPosition = p.position;
    p.position += vel + accel * dt2;
  }
}

void ClothSim::satisfyConstraints() {
  for (auto& c : constraints) {
    if (c.broken) continue;
    ClothParticle& a = particles[c.a];
    ClothParticle& b = particles[c.b];
    float totalInv = a.invMass + b.invMass;
    if (totalInv == 0.0f) continue;
    glm::vec3 delta = b.position - a.position;
    float dist = glm::length(delta);
    if (dist < 1e-7f) continue;
    if (tearability > 1.0f && !c.isBending && dist > c.restLength * tearability) {
      c.broken = true;
      continue;
    }
    float s = c.isBending ? bendingStiffness : structuralStiffness;
    glm::vec3 correction = delta * (s * (dist - c.restLength) / dist);
    a.position += correction * (a.invMass / totalInv);
    b.position -= correction * (b.invMass / totalInv);
  }
}

void ClothSim::resolveCollisions(const std::vector<PhysicsObject*>& obstacles) {
  for (const auto* obj : obstacles) {
    if (!obj) continue;
    for (auto& p : particles) {
      if (p.invMass == 0.0f) continue;
      resolveVsObject(p, *obj);
    }
  }
}

void ClothSim::resolveVsObject(ClothParticle& p, const PhysicsObject& obj) {
  switch (obj.getCollider().getType()) {
    case ColliderType::Sphere:   resolveVsSphere(p, obj);   break;
    case ColliderType::AABB:     resolveVsAABB(p, obj);     break;
    case ColliderType::Plane:    resolveVsPlane(p, obj);    break;
    case ColliderType::Cylinder: resolveVsCylinder(p, obj); break;
    case ColliderType::Capsule:  resolveVsCapsule(p, obj);  break;
    default: break;
  }
}

void ClothSim::resolveVsSphere(ClothParticle& p, const PhysicsObject& obj) {
  glm::vec3 d = p.position - obj.getPosition();
  float dist = glm::length(d);
  float r = obj.getCollider().getRadius();
  if (dist >= r || dist < 1e-7f) return;
  glm::vec3 n = d / dist;
  glm::vec3 newPos = obj.getPosition() + n * r;
  glm::vec3 correction = newPos - p.position;
  p.prevPosition += correction;
  p.position = newPos;
  glm::vec3 vel = p.position - p.prevPosition;
  float vn = glm::dot(vel, n);
  if (vn < 0.0f) p.prevPosition += n * vn;
}

void ClothSim::resolveVsAABB(ClothParticle& p, const PhysicsObject& obj) {
  const glm::vec3& he = obj.getCollider().getHalfExtents();
  const glm::vec3& c  = obj.getPosition();
  glm::vec3 d = p.position - c;

  if (std::abs(d.x) >= he.x || std::abs(d.y) >= he.y || std::abs(d.z) >= he.z)
    return;

  float px = he.x - std::abs(d.x);
  float py = he.y - std::abs(d.y);
  float pz = he.z - std::abs(d.z);

  glm::vec3 n(0.0f);
  glm::vec3 newPos = p.position;
  if (px <= py && px <= pz) {
    n = glm::vec3(d.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
    newPos.x = c.x + (d.x >= 0.0f ? he.x : -he.x);
  } else if (py <= pz) {
    n = glm::vec3(0.0f, d.y >= 0.0f ? 1.0f : -1.0f, 0.0f);
    newPos.y = c.y + (d.y >= 0.0f ? he.y : -he.y);
  } else {
    n = glm::vec3(0.0f, 0.0f, d.z >= 0.0f ? 1.0f : -1.0f);
    newPos.z = c.z + (d.z >= 0.0f ? he.z : -he.z);
  }
  glm::vec3 correction = newPos - p.position;
  p.prevPosition += correction;
  p.position = newPos;
  glm::vec3 vel = p.position - p.prevPosition;
  float vn = glm::dot(vel, n);
  if (vn < 0.0f) p.prevPosition += n * vn;
}

void ClothSim::resolveVsPlane(ClothParticle& p, const PhysicsObject& obj) {
  const glm::vec3& n = obj.getCollider().getNormal();
  float d = glm::dot(p.position - obj.getPosition(), n);
  if (d >= 0.0f) return;
  glm::vec3 correction = n * (-d);
  p.prevPosition += correction;
  p.position += correction;
  glm::vec3 vel = p.position - p.prevPosition;
  float vn = glm::dot(vel, n);
  if (vn < 0.0f) p.prevPosition += n * vn;
}

void ClothSim::resolveVsCylinder(ClothParticle& p, const PhysicsObject& obj) {
  const glm::vec3& center = obj.getPosition();
  float r     = obj.getCollider().getRadius();
  float halfH = obj.getCollider().getHeight() * 0.5f;

  float dy = p.position.y - center.y;
  if (std::abs(dy) > halfH) return;

  glm::vec2 d2d(p.position.x - center.x, p.position.z - center.z);
  float dist2d = glm::length(d2d);
  if (dist2d >= r || dist2d < 1e-7f) return;

  glm::vec2 n2d = d2d / dist2d;
  glm::vec3 n(n2d.x, 0.0f, n2d.y);
  glm::vec3 newPos = p.position;
  newPos.x = center.x + n2d.x * r;
  newPos.z = center.z + n2d.y * r;
  glm::vec3 correction = newPos - p.position;
  p.prevPosition += correction;
  p.position = newPos;
  glm::vec3 vel = p.position - p.prevPosition;
  float vn = glm::dot(vel, n);
  if (vn < 0.0f) p.prevPosition += n * vn;
}

void ClothSim::resolveVsCapsule(ClothParticle& p, const PhysicsObject& obj) {
  const glm::vec3& center = obj.getPosition();
  float r     = obj.getCollider().getRadius();
  float halfH = obj.getCollider().getHeight() * 0.5f;

  glm::vec3 a  = center - glm::vec3(0.0f, halfH, 0.0f);
  glm::vec3 ab = glm::vec3(0.0f, halfH * 2.0f, 0.0f);
  float t = glm::clamp(glm::dot(p.position - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
  glm::vec3 closest = a + ab * t;

  glm::vec3 d = p.position - closest;
  float dist = glm::length(d);
  if (dist >= r || dist < 1e-7f) return;

  glm::vec3 n = d / dist;
  glm::vec3 newPos = closest + n * r;
  glm::vec3 correction = newPos - p.position;
  p.prevPosition += correction;
  p.position = newPos;
  glm::vec3 vel = p.position - p.prevPosition;
  float vn = glm::dot(vel, n);
  if (vn < 0.0f) p.prevPosition += n * vn;
}

}  // namespace jphys
