#include "pch.h"

#include <Collider.h>
#include <PhysicsObject.h>
#include <PhysicsWorld.h>

using namespace jphys;

TEST(Collider, CreateSphere) {
  Collider c = Collider::createSphere(2.5f);
  EXPECT_EQ(c.getType(), ColliderType::Sphere);
  EXPECT_FLOAT_EQ(c.getRadius(), 2.5f);
}

TEST(Collider, CreateAABB) {
  Collider c = Collider::createAABB(glm::vec3(1.0f, 2.0f, 3.0f));
  EXPECT_EQ(c.getType(), ColliderType::AABB);
  EXPECT_FLOAT_EQ(c.getHalfExtents().x, 1.0f);
  EXPECT_FLOAT_EQ(c.getHalfExtents().y, 2.0f);
  EXPECT_FLOAT_EQ(c.getHalfExtents().z, 3.0f);
}

TEST(Collider, CreateInfinitePlane) {
  Collider c = Collider::createPlane(glm::vec3(0.0f, 1.0f, 0.0f));
  EXPECT_EQ(c.getType(), ColliderType::Plane);
  EXPECT_FALSE(c.isFinite());
  EXPECT_FLOAT_EQ(c.getNormal().y, 1.0f);
}

TEST(Collider, CreateFinitePlane) {
  Collider c = Collider::createFinitePlane(glm::vec3(0.0f, 1.0f, 0.0f),
                                            glm::vec3(5.0f, 0.0f, 5.0f));
  EXPECT_EQ(c.getType(), ColliderType::Plane);
  EXPECT_TRUE(c.isFinite());
  EXPECT_FLOAT_EQ(c.getHalfExtents().x, 5.0f);
}

TEST(Collider, PlaneNormalIsNormalized) {
  Collider c = Collider::createPlane(glm::vec3(0.0f, 10.0f, 0.0f));
  float len = glm::length(c.getNormal());
  EXPECT_NEAR(len, 1.0f, 0.0001f);
}

TEST(Collider, DefaultIsSphere) {
  Collider c;
  EXPECT_EQ(c.getType(), ColliderType::Sphere);
  EXPECT_FLOAT_EQ(c.getRadius(), 1.0f);
}

TEST(PhysicsObject, DefaultValues) {
  PhysicsObject obj;
  EXPECT_FLOAT_EQ(obj.getPosition().x, 0.0f);
  EXPECT_FLOAT_EQ(obj.getPosition().y, 0.0f);
  EXPECT_FLOAT_EQ(obj.getPosition().z, 0.0f);
  EXPECT_FLOAT_EQ(obj.getMass(), 1.0f);
  EXPECT_FLOAT_EQ(obj.getRestitution(), 0.5f);
  EXPECT_TRUE(obj.getUseGravity());
  EXPECT_FALSE(obj.isStatic());
}

TEST(PhysicsObject, ConstructWithPositionAndCollider) {
  PhysicsObject obj(glm::vec3(1.0f, 2.0f, 3.0f),
                    Collider::createSphere(5.0f));
  EXPECT_FLOAT_EQ(obj.getPosition().x, 1.0f);
  EXPECT_FLOAT_EQ(obj.getPosition().y, 2.0f);
  EXPECT_FLOAT_EQ(obj.getPosition().z, 3.0f);
  EXPECT_FLOAT_EQ(obj.getCollider().getRadius(), 5.0f);
}

TEST(PhysicsObject, SettersWork) {
  PhysicsObject obj;
  obj.setPosition(glm::vec3(10.0f, 20.0f, 30.0f));
  obj.setVelocity(glm::vec3(1.0f, 0.0f, 0.0f));
  obj.setMass(5.0f);
  obj.setRestitution(0.8f);
  obj.setDamping(0.95f);
  obj.setUseGravity(false);
  obj.setStatic(true);

  EXPECT_FLOAT_EQ(obj.getPosition().x, 10.0f);
  EXPECT_FLOAT_EQ(obj.getVelocity().x, 1.0f);
  EXPECT_FLOAT_EQ(obj.getMass(), 5.0f);
  EXPECT_FLOAT_EQ(obj.getRestitution(), 0.8f);
  EXPECT_FLOAT_EQ(obj.getDamping(), 0.95f);
  EXPECT_FALSE(obj.getUseGravity());
  EXPECT_TRUE(obj.isStatic());
}

TEST(PhysicsWorld, GravityDefault) {
  PhysicsWorld world;
  EXPECT_FLOAT_EQ(world.getGravity().y, -9.81f);
}

TEST(PhysicsWorld, AddAndRemoveObjects) {
  PhysicsWorld world;
  PhysicsObject a, b;
  world.addObject(&a);
  world.addObject(&b);
  EXPECT_EQ(world.getObjects().size(), 2u);

  world.removeObject(&a);
  EXPECT_EQ(world.getObjects().size(), 1u);

  world.clear();
  EXPECT_EQ(world.getObjects().size(), 0u);
}

TEST(PhysicsWorld, GravityApplied) {
  PhysicsWorld world;
  world.setGravity(glm::vec3(0.0f, -10.0f, 0.0f));

  PhysicsObject ball(glm::vec3(0.0f, 100.0f, 0.0f),
                     Collider::createSphere(1.0f));
  world.addObject(&ball);

  world.step(1.0f);

  EXPECT_LT(ball.getPosition().y, 100.0f);
  EXPECT_LT(ball.getVelocity().y, 0.0f);
}

TEST(PhysicsWorld, StaticObjectsDoNotMove) {
  PhysicsWorld world;
  world.setGravity(glm::vec3(0.0f, -10.0f, 0.0f));

  PhysicsObject obj(glm::vec3(0.0f, 0.0f, 0.0f),
                    Collider::createPlane(glm::vec3(0.0f, 1.0f, 0.0f)));
  obj.setStatic(true);
  world.addObject(&obj);

  world.step(1.0f);

  EXPECT_FLOAT_EQ(obj.getPosition().y, 0.0f);
}

TEST(PhysicsWorld, NoGravityWhenDisabled) {
  PhysicsWorld world;
  world.setGravity(glm::vec3(0.0f, -10.0f, 0.0f));

  PhysicsObject ball(glm::vec3(0.0f, 50.0f, 0.0f),
                     Collider::createSphere(1.0f));
  ball.setUseGravity(false);
  world.addObject(&ball);

  world.step(1.0f);

  EXPECT_NEAR(ball.getPosition().y, 50.0f, 0.5f);
}

TEST(PhysicsWorld, ZeroDeltaTimeNoOp) {
  PhysicsWorld world;
  PhysicsObject ball(glm::vec3(5.0f, 5.0f, 5.0f),
                     Collider::createSphere(1.0f));
  ball.setVelocity(glm::vec3(100.0f, 0.0f, 0.0f));
  world.addObject(&ball);

  world.step(0.0f);

  EXPECT_FLOAT_EQ(ball.getPosition().x, 5.0f);
}

TEST(PhysicsWorld, NegativeDeltaTimeNoOp) {
  PhysicsWorld world;
  PhysicsObject ball(glm::vec3(5.0f, 5.0f, 5.0f),
                     Collider::createSphere(1.0f));
  ball.setVelocity(glm::vec3(100.0f, 0.0f, 0.0f));
  world.addObject(&ball);

  world.step(-1.0f);

  EXPECT_FLOAT_EQ(ball.getPosition().x, 5.0f);
}

TEST(PhysicsWorld, VelocityMovesObject) {
  PhysicsWorld world;
  world.setGravity(glm::vec3(0.0f));

  PhysicsObject ball(glm::vec3(0.0f), Collider::createSphere(1.0f));
  ball.setVelocity(glm::vec3(10.0f, 0.0f, 0.0f));
  ball.setDamping(1.0f);
  ball.setUseGravity(false);
  world.addObject(&ball);

  world.step(1.0f);

  EXPECT_NEAR(ball.getPosition().x, 10.0f, 0.01f);
}

TEST(PhysicsWorld, AccelerationApplied) {
  PhysicsWorld world;
  world.setGravity(glm::vec3(0.0f));

  PhysicsObject ball(glm::vec3(0.0f), Collider::createSphere(1.0f));
  ball.setAcceleration(glm::vec3(5.0f, 0.0f, 0.0f));
  ball.setDamping(1.0f);
  ball.setUseGravity(false);
  world.addObject(&ball);

  world.step(1.0f);

  EXPECT_GT(ball.getVelocity().x, 0.0f);
  EXPECT_GT(ball.getPosition().x, 0.0f);
}

TEST(PhysicsWorld, DampingReducesVelocity) {
  PhysicsWorld world;
  world.setGravity(glm::vec3(0.0f));

  PhysicsObject ball(glm::vec3(0.0f), Collider::createSphere(1.0f));
  ball.setVelocity(glm::vec3(100.0f, 0.0f, 0.0f));
  ball.setDamping(0.5f);
  ball.setUseGravity(false);
  world.addObject(&ball);

  world.step(1.0f);

  EXPECT_LT(ball.getVelocity().x, 100.0f);
}

TEST(SpherePlane, DetectsCollision) {
  PhysicsObject sphere(glm::vec3(0.0f, 0.5f, 0.0f),
                       Collider::createSphere(1.0f));

  PhysicsObject plane(glm::vec3(0.0f, 0.0f, 0.0f),
                      Collider::createPlane(glm::vec3(0.0f, 1.0f, 0.0f)));
  plane.setStatic(true);

  CollisionResult result = PhysicsWorld::testSpherePlane(sphere, plane);
  EXPECT_TRUE(result.collided);
  EXPECT_GT(result.penetration, 0.0f);
  EXPECT_NEAR(result.normal.y, 1.0f, 0.001f);
}

TEST(SpherePlane, NoCollisionWhenFarAway) {
  PhysicsObject sphere(glm::vec3(0.0f, 10.0f, 0.0f),
                       Collider::createSphere(1.0f));

  PhysicsObject plane(glm::vec3(0.0f, 0.0f, 0.0f),
                      Collider::createPlane(glm::vec3(0.0f, 1.0f, 0.0f)));
  plane.setStatic(true);

  CollisionResult result = PhysicsWorld::testSpherePlane(sphere, plane);
  EXPECT_FALSE(result.collided);
}

TEST(SpherePlane, ExactlyTouchingIsNotCollision) {
  PhysicsObject sphere(glm::vec3(0.0f, 1.0f, 0.0f),
                       Collider::createSphere(1.0f));

  PhysicsObject plane(glm::vec3(0.0f, 0.0f, 0.0f),
                      Collider::createPlane(glm::vec3(0.0f, 1.0f, 0.0f)));
  plane.setStatic(true);

  CollisionResult result = PhysicsWorld::testSpherePlane(sphere, plane);
  EXPECT_FALSE(result.collided);
}

TEST(SpherePlane, PenetrationDepthIsCorrect) {
  PhysicsObject sphere(glm::vec3(0.0f, 0.0f, 0.0f),
                       Collider::createSphere(1.0f));

  PhysicsObject plane(glm::vec3(0.0f, 0.0f, 0.0f),
                      Collider::createPlane(glm::vec3(0.0f, 1.0f, 0.0f)));
  plane.setStatic(true);

  CollisionResult result = PhysicsWorld::testSpherePlane(sphere, plane);
  EXPECT_TRUE(result.collided);
  EXPECT_NEAR(result.penetration, 1.0f, 0.001f);
}

TEST(SpherePlane, FinitePlaneRejectsOutsideSphere) {
  PhysicsObject sphere(glm::vec3(20.0f, 0.5f, 0.0f),
                       Collider::createSphere(1.0f));

  PhysicsObject plane(
      glm::vec3(0.0f, 0.0f, 0.0f),
      Collider::createFinitePlane(glm::vec3(0.0f, 1.0f, 0.0f),
                                   glm::vec3(5.0f, 0.0f, 5.0f)));
  plane.setStatic(true);

  CollisionResult result = PhysicsWorld::testSpherePlane(sphere, plane);
  EXPECT_FALSE(result.collided);
}

TEST(SpherePlane, FinitePlaneDetectsInsideSphere) {
  PhysicsObject sphere(glm::vec3(2.0f, 0.5f, 2.0f),
                       Collider::createSphere(1.0f));

  PhysicsObject plane(
      glm::vec3(0.0f, 0.0f, 0.0f),
      Collider::createFinitePlane(glm::vec3(0.0f, 1.0f, 0.0f),
                                   glm::vec3(5.0f, 0.0f, 5.0f)));
  plane.setStatic(true);

  CollisionResult result = PhysicsWorld::testSpherePlane(sphere, plane);
  EXPECT_TRUE(result.collided);
}

TEST(SpherePlane, AngledPlane) {
  glm::vec3 normal = glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f));
  PhysicsObject sphere(glm::vec3(0.0f, 0.0f, 0.0f),
                       Collider::createSphere(1.0f));

  PhysicsObject plane(glm::vec3(0.0f, 0.0f, 0.0f),
                      Collider::createPlane(normal));
  plane.setStatic(true);

  CollisionResult result = PhysicsWorld::testSpherePlane(sphere, plane);
  EXPECT_TRUE(result.collided);
}

TEST(SphereSphere, DetectsOverlap) {
  PhysicsObject a(glm::vec3(0.0f, 0.0f, 0.0f),
                  Collider::createSphere(1.0f));
  PhysicsObject b(glm::vec3(1.5f, 0.0f, 0.0f),
                  Collider::createSphere(1.0f));

  CollisionResult result = PhysicsWorld::testSphereSphere(a, b);
  EXPECT_TRUE(result.collided);
  EXPECT_NEAR(result.penetration, 0.5f, 0.001f);
}

TEST(SphereSphere, NoOverlapWhenFarApart) {
  PhysicsObject a(glm::vec3(0.0f, 0.0f, 0.0f),
                  Collider::createSphere(1.0f));
  PhysicsObject b(glm::vec3(5.0f, 0.0f, 0.0f),
                  Collider::createSphere(1.0f));

  CollisionResult result = PhysicsWorld::testSphereSphere(a, b);
  EXPECT_FALSE(result.collided);
}

TEST(SphereSphere, ExactlyTouchingIsNotCollision) {
  PhysicsObject a(glm::vec3(0.0f, 0.0f, 0.0f),
                  Collider::createSphere(1.0f));
  PhysicsObject b(glm::vec3(2.0f, 0.0f, 0.0f),
                  Collider::createSphere(1.0f));

  CollisionResult result = PhysicsWorld::testSphereSphere(a, b);
  EXPECT_FALSE(result.collided);
}

TEST(SphereSphere, CoincidentSpheresNoCollision) {
  PhysicsObject a(glm::vec3(0.0f, 0.0f, 0.0f),
                  Collider::createSphere(1.0f));
  PhysicsObject b(glm::vec3(0.0f, 0.0f, 0.0f),
                  Collider::createSphere(1.0f));

  CollisionResult result = PhysicsWorld::testSphereSphere(a, b);
  EXPECT_FALSE(result.collided);
}

TEST(SphereSphere, NormalPointsFromBToA) {
  PhysicsObject a(glm::vec3(0.0f, 0.0f, 0.0f),
                  Collider::createSphere(1.0f));
  PhysicsObject b(glm::vec3(1.0f, 0.0f, 0.0f),
                  Collider::createSphere(1.0f));

  CollisionResult result = PhysicsWorld::testSphereSphere(a, b);
  EXPECT_TRUE(result.collided);
  EXPECT_LT(result.normal.x, 0.0f);
}

TEST(SphereSphere, DifferentRadii) {
  PhysicsObject a(glm::vec3(0.0f, 0.0f, 0.0f),
                  Collider::createSphere(2.0f));
  PhysicsObject b(glm::vec3(2.5f, 0.0f, 0.0f),
                  Collider::createSphere(1.0f));

  CollisionResult result = PhysicsWorld::testSphereSphere(a, b);
  EXPECT_TRUE(result.collided);
  EXPECT_NEAR(result.penetration, 0.5f, 0.001f);
}

TEST(SphereSphere, DiagonalOverlap) {
  PhysicsObject a(glm::vec3(0.0f, 0.0f, 0.0f),
                  Collider::createSphere(1.0f));
  PhysicsObject b(glm::vec3(1.0f, 1.0f, 0.0f),
                  Collider::createSphere(1.0f));

  float dist = glm::length(glm::vec3(1.0f, 1.0f, 0.0f));
  CollisionResult result = PhysicsWorld::testSphereSphere(a, b);
  EXPECT_TRUE(result.collided);
  EXPECT_NEAR(result.penetration, 2.0f - dist, 0.001f);
}

TEST(SphereAABB, DetectsOverlap) {
  PhysicsObject sphere(glm::vec3(1.4f, 0.0f, 0.0f),
                       Collider::createSphere(1.0f));

  PhysicsObject box(glm::vec3(0.0f, 0.0f, 0.0f),
                    Collider::createAABB(glm::vec3(1.0f)));
  box.setStatic(true);

  CollisionResult result = PhysicsWorld::testSphereAABB(sphere, box);
  EXPECT_TRUE(result.collided);
  EXPECT_GT(result.penetration, 0.0f);
}

TEST(SphereAABB, NoOverlapWhenFarAway) {
  PhysicsObject sphere(glm::vec3(10.0f, 0.0f, 0.0f),
                       Collider::createSphere(1.0f));

  PhysicsObject box(glm::vec3(0.0f, 0.0f, 0.0f),
                    Collider::createAABB(glm::vec3(1.0f)));
  box.setStatic(true);

  CollisionResult result = PhysicsWorld::testSphereAABB(sphere, box);
  EXPECT_FALSE(result.collided);
}

TEST(SphereAABB, SphereAboveBox) {
  PhysicsObject sphere(glm::vec3(0.0f, 1.4f, 0.0f),
                       Collider::createSphere(1.0f));

  PhysicsObject box(glm::vec3(0.0f, 0.0f, 0.0f),
                    Collider::createAABB(glm::vec3(1.0f)));
  box.setStatic(true);

  CollisionResult result = PhysicsWorld::testSphereAABB(sphere, box);
  EXPECT_TRUE(result.collided);
  EXPECT_NEAR(result.normal.y, 1.0f, 0.01f);
}

TEST(SphereAABB, CornerCollision) {
  PhysicsObject sphere(glm::vec3(1.5f, 1.5f, 0.0f),
                       Collider::createSphere(1.0f));

  PhysicsObject box(glm::vec3(0.0f, 0.0f, 0.0f),
                    Collider::createAABB(glm::vec3(1.0f)));
  box.setStatic(true);

  float distToCorner = glm::length(glm::vec3(0.5f, 0.5f, 0.0f));
  CollisionResult result = PhysicsWorld::testSphereAABB(sphere, box);
  EXPECT_EQ(result.collided, distToCorner < 1.0f);
}

TEST(SphereAABB, ScaledBox) {
  PhysicsObject sphere(glm::vec3(2.5f, 0.0f, 0.0f),
                       Collider::createSphere(1.0f));

  PhysicsObject box(glm::vec3(0.0f, 0.0f, 0.0f),
                    Collider::createAABB(glm::vec3(1.0f)));
  box.setScale(glm::vec3(2.0f, 1.0f, 1.0f));
  box.setStatic(true);

  CollisionResult result = PhysicsWorld::testSphereAABB(sphere, box);
  EXPECT_TRUE(result.collided);
}

TEST(SphereAABB, InsideSphereNoCollision) {
  PhysicsObject sphere(glm::vec3(0.0f, 0.0f, 0.0f),
                       Collider::createSphere(0.5f));

  PhysicsObject box(glm::vec3(0.0f, 0.0f, 0.0f),
                    Collider::createAABB(glm::vec3(5.0f)));
  box.setStatic(true);

  CollisionResult result = PhysicsWorld::testSphereAABB(sphere, box);
  EXPECT_FALSE(result.collided);
}

TEST(Resolution, SphereBounceOffPlane) {
  PhysicsWorld world;
  world.setGravity(glm::vec3(0.0f));

  PhysicsObject sphere(glm::vec3(0.0f, 0.5f, 0.0f),
                       Collider::createSphere(1.0f));
  sphere.setVelocity(glm::vec3(0.0f, -10.0f, 0.0f));
  sphere.setRestitution(1.0f);
  sphere.setDamping(1.0f);

  PhysicsObject plane(glm::vec3(0.0f, 0.0f, 0.0f),
                      Collider::createPlane(glm::vec3(0.0f, 1.0f, 0.0f)));
  plane.setStatic(true);

  world.addObject(&sphere);
  world.addObject(&plane);
  world.step(0.0001f);

  EXPECT_GT(sphere.getVelocity().y, 0.0f);
  EXPECT_GE(sphere.getPosition().y, 1.0f - 0.01f);
}

TEST(Resolution, SphereSphereEqualMassBounce) {
  PhysicsWorld world;
  world.setGravity(glm::vec3(0.0f));

  PhysicsObject a(glm::vec3(0.0f, 0.0f, 0.0f),
                  Collider::createSphere(1.0f));
  a.setVelocity(glm::vec3(5.0f, 0.0f, 0.0f));
  a.setMass(1.0f);
  a.setRestitution(1.0f);
  a.setDamping(1.0f);

  PhysicsObject b(glm::vec3(1.5f, 0.0f, 0.0f),
                  Collider::createSphere(1.0f));
  b.setVelocity(glm::vec3(0.0f, 0.0f, 0.0f));
  b.setMass(1.0f);
  b.setRestitution(1.0f);
  b.setDamping(1.0f);

  world.addObject(&a);
  world.addObject(&b);
  world.step(0.0001f);

  EXPECT_NEAR(a.getVelocity().x + b.getVelocity().x, 5.0f, 0.1f);
}

TEST(Resolution, SphereBouncesOffAABB) {
  PhysicsWorld world;
  world.setGravity(glm::vec3(0.0f));

  PhysicsObject sphere(glm::vec3(0.0f, 1.4f, 0.0f),
                       Collider::createSphere(1.0f));
  sphere.setVelocity(glm::vec3(0.0f, -10.0f, 0.0f));
  sphere.setRestitution(1.0f);
  sphere.setDamping(1.0f);

  PhysicsObject box(glm::vec3(0.0f, 0.0f, 0.0f),
                    Collider::createAABB(glm::vec3(5.0f, 1.0f, 5.0f)));
  box.setStatic(true);

  world.addObject(&sphere);
  world.addObject(&box);
  world.step(0.0001f);

  EXPECT_GT(sphere.getVelocity().y, 0.0f);
}

TEST(Resolution, PartialRestitutionLosesEnergy) {
  PhysicsWorld world;
  world.setGravity(glm::vec3(0.0f));

  PhysicsObject sphere(glm::vec3(0.0f, 0.5f, 0.0f),
                       Collider::createSphere(1.0f));
  sphere.setVelocity(glm::vec3(0.0f, -10.0f, 0.0f));
  sphere.setRestitution(0.5f);
  sphere.setDamping(1.0f);

  PhysicsObject plane(glm::vec3(0.0f, 0.0f, 0.0f),
                      Collider::createPlane(glm::vec3(0.0f, 1.0f, 0.0f)));
  plane.setStatic(true);

  world.addObject(&sphere);
  world.addObject(&plane);
  world.step(0.0001f);

  EXPECT_GT(sphere.getVelocity().y, 0.0f);
  EXPECT_LT(sphere.getVelocity().y, 10.0f);
}

TEST(Resolution, ZeroRestitutionStopsObject) {
  PhysicsWorld world;
  world.setGravity(glm::vec3(0.0f));

  PhysicsObject sphere(glm::vec3(0.0f, 0.5f, 0.0f),
                       Collider::createSphere(1.0f));
  sphere.setVelocity(glm::vec3(0.0f, -10.0f, 0.0f));
  sphere.setRestitution(0.0f);
  sphere.setDamping(1.0f);

  PhysicsObject plane(glm::vec3(0.0f, 0.0f, 0.0f),
                      Collider::createPlane(glm::vec3(0.0f, 1.0f, 0.0f)));
  plane.setStatic(true);

  world.addObject(&sphere);
  world.addObject(&plane);
  world.step(0.0001f);

  EXPECT_NEAR(sphere.getVelocity().y, 0.0f, 0.01f);
}

TEST(Resolution, HeavyObjectPushesLightObject) {
  PhysicsWorld world;
  world.setGravity(glm::vec3(0.0f));

  PhysicsObject heavy(glm::vec3(0.0f, 0.0f, 0.0f),
                      Collider::createSphere(1.0f));
  heavy.setVelocity(glm::vec3(10.0f, 0.0f, 0.0f));
  heavy.setMass(100.0f);
  heavy.setRestitution(1.0f);
  heavy.setDamping(1.0f);

  PhysicsObject light(glm::vec3(1.5f, 0.0f, 0.0f),
                      Collider::createSphere(1.0f));
  light.setMass(1.0f);
  light.setRestitution(1.0f);
  light.setDamping(1.0f);

  world.addObject(&heavy);
  world.addObject(&light);
  world.step(0.0001f);

  EXPECT_GT(light.getVelocity().x, heavy.getVelocity().x);
}

TEST(Intersection, TwoSpheresOverlapping) {
  PhysicsObject a(glm::vec3(0.0f), Collider::createSphere(2.0f));
  PhysicsObject b(glm::vec3(3.0f, 0.0f, 0.0f), Collider::createSphere(2.0f));

  CollisionResult result = PhysicsWorld::testSphereSphere(a, b);
  EXPECT_TRUE(result.collided);
  EXPECT_NEAR(result.penetration, 1.0f, 0.001f);
}

TEST(Intersection, SphereInsideLargerSphere) {
  PhysicsObject small(glm::vec3(0.0f), Collider::createSphere(1.0f));
  PhysicsObject large(glm::vec3(0.5f, 0.0f, 0.0f),
                      Collider::createSphere(5.0f));

  CollisionResult result = PhysicsWorld::testSphereSphere(small, large);
  EXPECT_TRUE(result.collided);
}

TEST(Intersection, SphereBelowPlane) {
  PhysicsObject sphere(glm::vec3(0.0f, -5.0f, 0.0f),
                       Collider::createSphere(1.0f));
  PhysicsObject plane(glm::vec3(0.0f, 0.0f, 0.0f),
                      Collider::createPlane(glm::vec3(0.0f, 1.0f, 0.0f)));

  CollisionResult result = PhysicsWorld::testSpherePlane(sphere, plane);
  EXPECT_TRUE(result.collided);
  EXPECT_NEAR(result.penetration, 6.0f, 0.001f);
}

TEST(Intersection, SphereEdgeOfAABB) {
  PhysicsObject sphere(glm::vec3(1.8f, 0.0f, 0.0f),
                       Collider::createSphere(1.0f));
  PhysicsObject box(glm::vec3(0.0f), Collider::createAABB(glm::vec3(1.0f)));

  CollisionResult result = PhysicsWorld::testSphereAABB(sphere, box);
  EXPECT_TRUE(result.collided);
  EXPECT_NEAR(result.penetration, 0.2f, 0.01f);
}

TEST(Intersection, MultipleSpheresSamePosition) {
  PhysicsObject a(glm::vec3(0.0f), Collider::createSphere(1.0f));
  PhysicsObject b(glm::vec3(0.0f), Collider::createSphere(1.0f));
  PhysicsObject c(glm::vec3(0.0f), Collider::createSphere(1.0f));

  CollisionResult ab = PhysicsWorld::testSphereSphere(a, b);
  CollisionResult ac = PhysicsWorld::testSphereSphere(a, c);
  EXPECT_FALSE(ab.collided);
  EXPECT_FALSE(ac.collided);
}

TEST(Simulation, BallDropOntoPlane) {
  PhysicsWorld world;
  world.setGravity(glm::vec3(0.0f, -9.81f, 0.0f));

  PhysicsObject ball(glm::vec3(0.0f, 10.0f, 0.0f),
                     Collider::createSphere(1.0f));
  ball.setRestitution(0.8f);
  ball.setDamping(0.99f);

  PhysicsObject ground(glm::vec3(0.0f, 0.0f, 0.0f),
                       Collider::createPlane(glm::vec3(0.0f, 1.0f, 0.0f)));
  ground.setStatic(true);

  world.addObject(&ball);
  world.addObject(&ground);

  for (int i = 0; i < 1000; i++) {
    world.step(0.016f);
  }

  EXPECT_GE(ball.getPosition().y, 0.9f);
  EXPECT_NEAR(ball.getVelocity().y, 0.0f, 2.0f);
}

TEST(Simulation, TwoSpheresHeadOn) {
  PhysicsWorld world;
  world.setGravity(glm::vec3(0.0f));

  PhysicsObject a(glm::vec3(-5.0f, 0.0f, 0.0f),
                  Collider::createSphere(1.0f));
  a.setVelocity(glm::vec3(10.0f, 0.0f, 0.0f));
  a.setRestitution(1.0f);
  a.setDamping(1.0f);
  a.setUseGravity(false);

  PhysicsObject b(glm::vec3(5.0f, 0.0f, 0.0f),
                  Collider::createSphere(1.0f));
  b.setVelocity(glm::vec3(-10.0f, 0.0f, 0.0f));
  b.setRestitution(1.0f);
  b.setDamping(1.0f);
  b.setUseGravity(false);

  world.addObject(&a);
  world.addObject(&b);

  for (int i = 0; i < 100; i++) {
    world.step(0.016f);
  }

  EXPECT_LT(a.getPosition().x, -4.0f);
  EXPECT_GT(b.getPosition().x, 4.0f);
}

TEST(Torque, PerpendicularForceProducesTorque) {
  PhysicsObject obj;
  obj.applyForceAtPoint(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
  EXPECT_NEAR(obj.getTorque().z, 1.0f, 0.001f);
  EXPECT_NEAR(obj.getTorque().x, 0.0f, 0.001f);
  EXPECT_NEAR(obj.getTorque().y, 0.0f, 0.001f);
}

TEST(Torque, ParallelForceProducesNoTorque) {
  PhysicsObject obj;
  obj.applyForceAtPoint(glm::vec3(5.0f, 0.0f, 0.0f), glm::vec3(2.0f, 0.0f, 0.0f));
  EXPECT_NEAR(glm::length(obj.getTorque()), 0.0f, 0.001f);
}

TEST(Torque, MagnitudeScalesWithDistance) {
  PhysicsObject near_obj, far_obj;
  near_obj.applyForceAtPoint(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
  far_obj.applyForceAtPoint(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(2.0f, 0.0f, 0.0f));
  EXPECT_NEAR(glm::length(far_obj.getTorque()), 2.0f * glm::length(near_obj.getTorque()), 0.001f);
}

TEST(Torque, AccumulatesAngularVelocity) {
  PhysicsWorld world;
  world.setGravity(glm::vec3(0.0f));

  PhysicsObject obj(glm::vec3(0.0f), Collider::createSphere(1.0f));
  obj.setUseGravity(false);
  obj.setDamping(1.0f);
  obj.addTorque(glm::vec3(0.0f, 1.0f, 0.0f));
  world.addObject(&obj);
  world.step(1.0f);

  EXPECT_GT(obj.getAngularVelocity().y, 0.0f);
}

TEST(Torque, ClearedAfterStep) {
  PhysicsWorld world;
  world.setGravity(glm::vec3(0.0f));

  PhysicsObject obj(glm::vec3(0.0f), Collider::createSphere(1.0f));
  obj.setUseGravity(false);
  obj.addTorque(glm::vec3(1.0f, 0.0f, 0.0f));
  world.addObject(&obj);
  world.step(0.016f);

  EXPECT_NEAR(glm::length(obj.getTorque()), 0.0f, 0.001f);
}

TEST(SphereInertia, FormulaIsCorrect) {
  PhysicsObject obj(glm::vec3(0.0f), Collider::createSphere(1.0f));
  obj.setMass(1.0f);
  EXPECT_NEAR(obj.getMomentOfInertia(), 0.4f, 0.001f);
}

TEST(SphereInertia, HeavierSphereSlowerAngularAccel) {
  PhysicsWorld worldLight, worldHeavy;
  worldLight.setGravity(glm::vec3(0.0f));
  worldHeavy.setGravity(glm::vec3(0.0f));

  PhysicsObject light(glm::vec3(0.0f), Collider::createSphere(1.0f));
  light.setMass(1.0f);
  light.setUseGravity(false);
  light.setDamping(1.0f);
  light.addTorque(glm::vec3(0.0f, 1.0f, 0.0f));

  PhysicsObject heavy(glm::vec3(0.0f), Collider::createSphere(1.0f));
  heavy.setMass(4.0f);
  heavy.setUseGravity(false);
  heavy.setDamping(1.0f);
  heavy.addTorque(glm::vec3(0.0f, 1.0f, 0.0f));

  worldLight.addObject(&light);
  worldHeavy.addObject(&heavy);
  worldLight.step(1.0f);
  worldHeavy.step(1.0f);

  EXPECT_GT(light.getAngularVelocity().y, heavy.getAngularVelocity().y);
}

TEST(SphereInertia, AngularVelocityMatchesTauOverI) {
  PhysicsWorld world;
  world.setGravity(glm::vec3(0.0f));

  PhysicsObject obj(glm::vec3(0.0f), Collider::createSphere(1.0f));
  obj.setMass(1.0f);
  obj.setUseGravity(false);
  obj.setDamping(1.0f);
  obj.addTorque(glm::vec3(0.0f, 1.0f, 0.0f));
  world.addObject(&obj);
  world.step(1.0f);

  EXPECT_NEAR(obj.getAngularVelocity().y, 2.5f, 0.01f);
}

TEST(CylinderInertia, BodySpaceValuesCorrect) {
  PhysicsObject obj(glm::vec3(0.0f), Collider::createCylinder(1.0f, 3.0f));
  obj.setMass(1.0f);
  glm::vec3 bodyInvI = obj.getBodyInverseInertia();
  EXPECT_NEAR(bodyInvI.x, 1.0f, 0.001f);
  EXPECT_NEAR(bodyInvI.z, 2.0f, 0.001f);
}

TEST(CylinderInertia, IdentityOrientationWorldMatchesBody) {
  PhysicsObject obj(glm::vec3(0.0f), Collider::createCylinder(1.0f, 3.0f));
  obj.setMass(1.0f);
  glm::mat3 worldInvI = obj.getWorldInverseInertiaTensor();
  glm::vec3 bodyInvI = obj.getBodyInverseInertia();
  EXPECT_NEAR(worldInvI[0][0], bodyInvI.x, 0.001f);
  EXPECT_NEAR(worldInvI[1][1], bodyInvI.y, 0.001f);
  EXPECT_NEAR(worldInvI[2][2], bodyInvI.z, 0.001f);
}

TEST(CylinderInertia, AlongAxisFasterThanPerp) {
  PhysicsWorld worldAlong, worldPerp;
  worldAlong.setGravity(glm::vec3(0.0f));
  worldPerp.setGravity(glm::vec3(0.0f));

  PhysicsObject cylAlong(glm::vec3(0.0f), Collider::createCylinder(1.0f, 3.0f));
  cylAlong.setMass(1.0f);
  cylAlong.setUseGravity(false);
  cylAlong.setDamping(1.0f);
  cylAlong.addTorque(glm::vec3(0.0f, 0.0f, 1.0f));

  PhysicsObject cylPerp(glm::vec3(0.0f), Collider::createCylinder(1.0f, 3.0f));
  cylPerp.setMass(1.0f);
  cylPerp.setUseGravity(false);
  cylPerp.setDamping(1.0f);
  cylPerp.addTorque(glm::vec3(1.0f, 0.0f, 0.0f));

  worldAlong.addObject(&cylAlong);
  worldPerp.addObject(&cylPerp);
  worldAlong.step(1.0f);
  worldPerp.step(1.0f);

  EXPECT_GT(glm::length(cylAlong.getAngularVelocity()),
            glm::length(cylPerp.getAngularVelocity()));
}

TEST(CylinderInertia, NinetyDegRotationChangesResponse) {
  PhysicsObject cyl(glm::vec3(0.0f), Collider::createCylinder(1.0f, 3.0f));
  cyl.setMass(1.0f);
  glm::quat rot90x = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
  cyl.setOrientation(rot90x);
  glm::mat3 worldInvI = cyl.getWorldInverseInertiaTensor();
  glm::vec3 bodyInvI = cyl.getBodyInverseInertia();
  EXPECT_NEAR(worldInvI[2][2], bodyInvI.x, 0.001f);
}
