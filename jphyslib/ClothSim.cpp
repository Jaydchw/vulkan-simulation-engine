#include "pch.h"

#include "ClothSim.h"

#include "EnvironmentForces.h"

#include <algorithm>
#include <cmath>
#include <cfloat>

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
  stepDt = dt;
  integrate(dt);
  for (int i = 0; i < solverIterations; ++i) {
    // Impulses are applied on the first iteration only to avoid accumulating
    // the reaction across all solver iterations.
    applyImpulseThisIter = twoWayCoupling && (i == 0);
    satisfyConstraints();
    resolveCollisions(obstacles);
  }
  applyImpulseThisIter = false;
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
      tornSinceLastQuery = true;
      continue;
    }
    float s = c.isBending ? bendingStiffness : structuralStiffness;
    glm::vec3 correction = delta * (s * (dist - c.restLength) / dist);
    a.position += correction * (a.invMass / totalInv);
    b.position -= correction * (b.invMass / totalInv);
  }
}

void ClothSim::resolveCollisions(const std::vector<PhysicsObject*>& obstacles) {
  for (auto* obj : obstacles) {
    if (!obj) continue;
    for (auto& p : particles) {
      if (p.invMass == 0.0f) continue;
      resolveVsObject(p, *obj);
    }
    resolveTrianglesVsObject(*obj);
  }
}

void ClothSim::resolveVsObject(ClothParticle& p, PhysicsObject& obj) {
  switch (obj.getCollider().getType()) {
    case ColliderType::Sphere:   resolveVsSphere(p, obj);   break;
    case ColliderType::AABB:     resolveVsAABB(p, obj);     break;
    case ColliderType::Plane:    resolveVsPlane(p, obj);    break;
    case ColliderType::Cylinder: resolveVsCylinder(p, obj); break;
    case ColliderType::Capsule:  resolveVsCapsule(p, obj);  break;
    default: break;
  }
}

// Applies a physically correct collision impulse between a cloth particle and a
// PhysicsObject. Accounts for the object's angular inertia at the contact point.
// For static objects, degrades gracefully to a one-way velocity cancellation.
void ClothSim::applyContactImpulse(ClothParticle& p, PhysicsObject& obj,
                                    const glm::vec3& n, const glm::vec3& contactPt) {
  if (stepDt < 1e-10f) return;

  // Pre-collision cloth particle velocity (Verlet: m/step, not m/s)
  glm::vec3 vel     = p.position - p.prevPosition;
  float vn_cloth    = glm::dot(vel, n);

  bool canReact     = !obj.isStatic() && obj.getMass() > 0.f;
  float invMassObj  = canReact ? 1.f / obj.getMass() : 0.f;

  // Obstacle surface velocity at the contact point (zero for static)
  glm::vec3 r       = contactPt - obj.getPosition();
  glm::vec3 v_obj   = canReact
                        ? obj.getVelocity() + glm::cross(obj.getAngularVelocity(), r)
                        : glm::vec3(0.f);

  // Relative approach speed in m/s (negative = approaching)
  float vn_rel = vn_cloth / stepDt - glm::dot(v_obj, n);
  if (vn_rel >= 0.f) return;

  // Effective inverse mass including rotational inertia at contact point
  glm::vec3 rn      = glm::cross(r, n);
  float angTerm     = canReact
                        ? glm::dot(rn, obj.getWorldInverseInertiaTensor() * rn)
                        : 0.f;
  float denom       = p.invMass + invMassObj + angTerm;
  if (denom < 1e-10f) return;

  float J = -vn_rel / denom;  // impulse magnitude (restitution = 0)

  // Cloth particle: convert impulse back to Verlet delta-prevPos
  p.prevPosition -= n * (J * p.invMass * stepDt);

  // Obstacle: apply linear and angular impulse
  if (canReact) {
    glm::vec3 imp = -J * n;
    obj.setVelocity(obj.getVelocity() + imp * invMassObj);
    obj.setAngularVelocity(obj.getAngularVelocity() +
                            obj.getWorldInverseInertiaTensor() * glm::cross(r, imp));
    obj.wakeUp();
  }
}

void ClothSim::resolveVsSphere(ClothParticle& p, PhysicsObject& obj) {
  glm::vec3 d = p.position - obj.getPosition();
  float dist = glm::length(d);
  float r = obj.getCollider().getRadius();
  if (dist >= r || dist < 1e-7f) return;
  glm::vec3 n = d / dist;
  glm::vec3 newPos = obj.getPosition() + n * r;
  glm::vec3 correction = newPos - p.position;
  p.prevPosition += correction;
  p.position = newPos;
  if (twoWayCoupling && applyImpulseThisIter) {
    applyContactImpulse(p, obj, n, p.position);
  } else {
    glm::vec3 vel = p.position - p.prevPosition;
    float vn = glm::dot(vel, n);
    if (vn < 0.0f) p.prevPosition += n * vn;
  }
}

void ClothSim::resolveVsAABB(ClothParticle& p, PhysicsObject& obj) {
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
  if (twoWayCoupling && applyImpulseThisIter) {
    applyContactImpulse(p, obj, n, p.position);
  } else {
    glm::vec3 vel = p.position - p.prevPosition;
    float vn = glm::dot(vel, n);
    if (vn < 0.0f) p.prevPosition += n * vn;
  }
}

void ClothSim::resolveVsPlane(ClothParticle& p, PhysicsObject& obj) {
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

void ClothSim::resolveVsCylinder(ClothParticle& p, PhysicsObject& obj) {
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
  if (twoWayCoupling && applyImpulseThisIter) {
    applyContactImpulse(p, obj, n, p.position);
  } else {
    glm::vec3 vel = p.position - p.prevPosition;
    float vn = glm::dot(vel, n);
    if (vn < 0.0f) p.prevPosition += n * vn;
  }
}

void ClothSim::resolveVsCapsule(ClothParticle& p, PhysicsObject& obj) {
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
  if (twoWayCoupling && applyImpulseThisIter) {
    applyContactImpulse(p, obj, n, p.position);
  } else {
    glm::vec3 vel = p.position - p.prevPosition;
    float vn = glm::dot(vel, n);
    if (vn < 0.0f) p.prevPosition += n * vn;
  }
}

// ---- triangle-level collision helpers ----

static glm::vec3 closestPtTriangle(const glm::vec3& p,
                                    const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
  glm::vec3 ab = b - a, ac = c - a, ap = p - a;
  float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
  if (d1 <= 0.f && d2 <= 0.f) return a;

  glm::vec3 bp = p - b;
  float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
  if (d3 >= 0.f && d4 <= d3) return b;

  glm::vec3 cp = p - c;
  float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
  if (d6 >= 0.f && d5 <= d6) return c;

  float vc = d1 * d4 - d3 * d2;
  if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f)
    return a + (d1 / (d1 - d3)) * ab;

  float vb = d5 * d2 - d1 * d6;
  if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f)
    return a + (d2 / (d2 - d6)) * ac;

  float va = d3 * d6 - d5 * d4;
  if (va <= 0.f && (d4 - d3) >= 0.f && (d5 - d6) >= 0.f)
    return b + ((d4 - d3) / ((d4 - d3) + (d5 - d6))) * (c - b);

  float inv = 1.f / (va + vb + vc);
  return a + (vb * inv) * ab + (vc * inv) * ac;
}

static void closestPtSegmentSegment(const glm::vec3& p1, const glm::vec3& q1,
                                     const glm::vec3& p2, const glm::vec3& q2,
                                     glm::vec3& c1, glm::vec3& c2) {
  glm::vec3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
  float a = glm::dot(d1, d1), e = glm::dot(d2, d2), f = glm::dot(d2, r);
  float s, t;
  if (a < 1e-10f && e < 1e-10f) {
    s = t = 0.f;
  } else if (a < 1e-10f) {
    s = 0.f; t = glm::clamp(f / e, 0.f, 1.f);
  } else {
    float c = glm::dot(d1, r);
    if (e < 1e-10f) {
      t = 0.f; s = glm::clamp(-c / a, 0.f, 1.f);
    } else {
      float b     = glm::dot(d1, d2);
      float denom = a * e - b * b;
      s = (denom > 1e-10f) ? glm::clamp((b * f - c * e) / denom, 0.f, 1.f) : 0.f;
      t = glm::clamp((b * s + f) / e, 0.f, 1.f);
      s = glm::clamp((b * t - c) / a, 0.f, 1.f);
      t = glm::clamp((b * s + f) / e, 0.f, 1.f);
    }
  }
  c1 = p1 + s * d1;
  c2 = p2 + t * d2;
}

// Pushes the three triangle particles away from the obstacle contact point.
// When reactObj is non-null, a physically correct impulse is applied to the
// obstacle (and velocity-based correction is used for cloth particles).
// When null, falls back to the original one-way velocity cancellation — zero
// overhead on the common path.
static void pushTriangle(ClothParticle& pa, ClothParticle& pb, ClothParticle& pc,
                          const glm::vec3& objPt, const glm::vec3& triPt, float radius,
                          PhysicsObject* reactObj = nullptr, float stepDt = 0.f) {
  glm::vec3 d = triPt - objPt;
  float dist  = glm::length(d);
  if (dist >= radius || dist < 1e-7f) return;

  glm::vec3 n    = d / dist;
  glm::vec3 corr = n * ((radius - dist) / 3.f);

  bool canReact  = reactObj && !reactObj->isStatic()
                   && reactObj->getMass() > 0.f && stepDt > 1e-10f;
  float invMassObj = canReact ? 1.f / reactObj->getMass() : 0.f;

  auto applyPush = [&](ClothParticle& p) {
    if (p.invMass == 0.f) return;
    p.prevPosition += corr;
    p.position     += corr;

    glm::vec3 vel = p.position - p.prevPosition;  // pre-collision Verlet velocity
    float vn      = glm::dot(vel, n);

    if (canReact) {
      glm::vec3 r      = p.position - reactObj->getPosition();
      glm::vec3 v_obj  = reactObj->getVelocity() + glm::cross(reactObj->getAngularVelocity(), r);
      float vn_rel     = vn / stepDt - glm::dot(v_obj, n);
      if (vn_rel < 0.f) {
        glm::vec3 rn = glm::cross(r, n);
        float angTerm = glm::dot(rn, reactObj->getWorldInverseInertiaTensor() * rn);
        float denom   = p.invMass + invMassObj + angTerm;
        if (denom > 1e-10f) {
          float J      = -vn_rel / denom;
          p.prevPosition -= n * (J * p.invMass * stepDt);
          glm::vec3 imp = -J * n;
          reactObj->setVelocity(reactObj->getVelocity() + imp * invMassObj);
          reactObj->setAngularVelocity(reactObj->getAngularVelocity() +
                                        reactObj->getWorldInverseInertiaTensor() * glm::cross(r, imp));
          reactObj->wakeUp();
        }
      }
    } else {
      if (vn < 0.f) p.prevPosition += n * vn;
    }
  };
  applyPush(pa); applyPush(pb); applyPush(pc);
}

static void triVsSphere(ClothParticle& pa, ClothParticle& pb, ClothParticle& pc,
                         const glm::vec3& center, float radius,
                         PhysicsObject* reactObj = nullptr, float stepDt = 0.f) {
  glm::vec3 triPt = closestPtTriangle(center, pa.position, pb.position, pc.position);
  pushTriangle(pa, pb, pc, center, triPt, radius, reactObj, stepDt);
}

static void triVsCapsule(ClothParticle& pa, ClothParticle& pb, ClothParticle& pc,
                          const glm::vec3& capA, const glm::vec3& capB, float radius,
                          PhysicsObject* reactObj = nullptr, float stepDt = 0.f) {
  glm::vec3 bestAxis = capA, bestTri = pa.position;
  float bestDist2 = FLT_MAX;

  auto check = [&](const glm::vec3& axisPt, const glm::vec3& triPt) {
    float d2 = glm::dot(axisPt - triPt, axisPt - triPt);
    if (d2 < bestDist2) { bestDist2 = d2; bestAxis = axisPt; bestTri = triPt; }
  };

  check(capA, closestPtTriangle(capA, pa.position, pb.position, pc.position));
  check(capB, closestPtTriangle(capB, pa.position, pb.position, pc.position));

  glm::vec3 c1, c2;
  closestPtSegmentSegment(capA, capB, pa.position, pb.position, c1, c2); check(c1, c2);
  closestPtSegmentSegment(capA, capB, pb.position, pc.position, c1, c2); check(c1, c2);
  closestPtSegmentSegment(capA, capB, pc.position, pa.position, c1, c2); check(c1, c2);

  pushTriangle(pa, pb, pc, bestAxis, bestTri, radius, reactObj, stepDt);
}

void ClothSim::resolveTrianglesVsObject(PhysicsObject& obj) {
  ColliderType type = obj.getCollider().getType();
  if (type != ColliderType::Sphere && type != ColliderType::Capsule) return;

  const glm::vec3& center = obj.getPosition();
  float r                 = obj.getCollider().getRadius();

  glm::vec3 broadCenter = center;
  float broadR2         = r * r;

  glm::vec3 capA, capB;
  if (type == ColliderType::Capsule) {
    float halfH = obj.getCollider().getHeight() * 0.5f;
    capA        = center - glm::vec3(0.f, halfH, 0.f);
    capB        = center + glm::vec3(0.f, halfH, 0.f);
    float br    = halfH + r;
    broadR2     = br * br;
  }

  // Pass the obstacle for reaction only on the first solver iteration.
  PhysicsObject* reactObj = (twoWayCoupling && applyImpulseThisIter) ? &obj : nullptr;

  for (int z = 0; z < resZ - 1; ++z) {
    for (int x = 0; x < resX - 1; ++x) {
      ClothParticle& p00 = particles[particleIndex(x,     z    )];
      ClothParticle& p10 = particles[particleIndex(x + 1, z    )];
      ClothParticle& p01 = particles[particleIndex(x,     z + 1)];
      ClothParticle& p11 = particles[particleIndex(x + 1, z + 1)];

      glm::vec3 qMin = glm::min(glm::min(p00.position, p10.position),
                                 glm::min(p01.position, p11.position));
      glm::vec3 qMax = glm::max(glm::max(p00.position, p10.position),
                                 glm::max(p01.position, p11.position));
      glm::vec3 diff = glm::clamp(broadCenter, qMin, qMax) - broadCenter;
      if (glm::dot(diff, diff) > broadR2) continue;

      if (type == ColliderType::Sphere) {
        triVsSphere(p00, p10, p01, center, r, reactObj, stepDt);
        triVsSphere(p10, p11, p01, center, r, reactObj, stepDt);
      } else {
        triVsCapsule(p00, p10, p01, capA, capB, r, reactObj, stepDt);
        triVsCapsule(p10, p11, p01, capA, capB, r, reactObj, stepDt);
      }
    }
  }
}

}  // namespace jphys
