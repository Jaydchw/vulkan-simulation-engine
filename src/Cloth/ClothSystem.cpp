#include "ClothSystem.h"

#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ECS/Components.h"
#include "ECS/Registry.h"
#include "Network/NetworkManager.h"
#include "Physics/PhysicsSystem.h"

void ClothSystem::setRegistry(Registry* reg, MeshManager* mm) {
  entries.clear();
  registry = reg;
  meshManager = mm;
  if (!registry || !meshManager) return;

  for (Entity e : registry->getEntities()) {
    if (registry->hasComponent<ClothComponent>(e))
      initCloth(e);
  }
}

void ClothSystem::update(float deltaTime) {
  if (!registry || !meshManager || deltaTime <= 0.0f) return;

  const std::vector<jphys::PhysicsObject*>* obstacleList = nullptr;
  std::vector<jphys::PhysicsObject*> emptyList;
  if (physicsSystem)
    obstacleList = &physicsSystem->getWorld().getObjects();
  else
    obstacleList = &emptyList;

  for (auto& [e, entry] : entries) {
    const auto* cloth = registry->getComponent<ClothComponent>(e);
    if (!cloth || !entry.sim) continue;

    if (networkManager && cloth->ownerPeerId != 0 &&
        cloth->ownerPeerId != networkManager->getLocalPeerID())
      continue;

    glm::vec3 combinedWind = cloth->wind;
    if (environmentSettings)
      combinedWind += environmentSettings->wind;
    entry.sim->setWind(combinedWind);
    entry.sim->setWindDrag(environmentSettings ? environmentSettings->windDrag : 0.2f);
    entry.sim->setTearability(cloth->tearability);

    entry.sim->step(deltaTime, *obstacleList);
    updateMesh(e, entry);
  }
}

void ClothSystem::initCloth(Entity e) {
  const auto* cloth = registry->getComponent<ClothComponent>(e);
  const auto* transform = registry->getComponent<TransformComponent>(e);
  if (!cloth || !transform) return;

  glm::vec3 origin = transform->position;

  int resX = (std::max)(cloth->resolutionX, 2);
  int resZ = (std::max)(cloth->resolutionZ, 2);

  glm::quat rot    = glm::quat(glm::radians(cloth->eulerAngles));
  glm::mat3 orient = glm::mat3_cast(rot);

  auto sim = std::make_unique<jphys::ClothSim>(resX, resZ, cloth->width,
                                                cloth->clothHeight, origin, orient);
  sim->setStructuralStiffness(cloth->structuralStiffness);
  sim->setBendingStiffness(cloth->bendingStiffness);
  sim->setDamping(cloth->damping);
  sim->setUseGravity(cloth->useGravity);
  glm::vec3 combinedWind = cloth->wind;
  if (environmentSettings)
    combinedWind += environmentSettings->wind;
  sim->setWind(combinedWind);
  sim->setWindDrag(environmentSettings ? environmentSettings->windDrag : 0.2f);
  sim->setTearability(cloth->tearability);
  sim->setParticleMass(cloth->particleMass);
  sim->setSolverIterations(cloth->solverIterations);

  int sp = (std::max)(cloth->pinSpacing, 1);

  auto pinRow = [&](int z) {
    for (int x = 0; x < resX; x += sp)
      sim->pinParticle(sim->particleIndex(x, z));
  };
  auto pinCol = [&](int x) {
    for (int z = 0; z < resZ; z += sp)
      sim->pinParticle(sim->particleIndex(x, z));
  };

  switch (cloth->hinge) {
    case ClothHinge::TopRow:       pinRow(0);                             break;
    case ClothHinge::BottomRow:    pinRow(resZ - 1);                      break;
    case ClothHinge::LeftCol:      pinCol(0);                             break;
    case ClothHinge::RightCol:     pinCol(resX - 1);                      break;
    case ClothHinge::TopCorners:
      sim->pinParticle(sim->particleIndex(0,       0));
      sim->pinParticle(sim->particleIndex(resX - 1, 0));
      break;
    case ClothHinge::BottomCorners:
      sim->pinParticle(sim->particleIndex(0,       resZ - 1));
      sim->pinParticle(sim->particleIndex(resX - 1, resZ - 1));
      break;
    case ClothHinge::AllCorners:
      sim->pinParticle(sim->particleIndex(0,       0));
      sim->pinParticle(sim->particleIndex(resX - 1, 0));
      sim->pinParticle(sim->particleIndex(0,       resZ - 1));
      sim->pinParticle(sim->particleIndex(resX - 1, resZ - 1));
      break;
    case ClothHinge::None:
    default:
      break;
  }

  auto verts = buildVertices(*sim);
  auto inds  = buildIndices(*sim);
  MeshID mid = meshManager->createDynamicMesh(verts, inds);

  registry->addComponent<MeshComponent>(e, MeshComponent{mid});

  TransformComponent flat = *transform;
  flat.position = glm::vec3(0.0f);
  flat.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  flat.scale    = glm::vec3(1.0f);
  *registry->getComponent<TransformComponent>(e) = flat;

  ClothEntry entry;
  entry.sim          = std::move(sim);
  entry.meshID       = mid;
  entry.vertexScratch = verts;
  entries[e]         = std::move(entry);
}

void ClothSystem::updateMesh(Entity e, ClothEntry& entry) {
  if (entry.meshID == INVALID_MESH_ID) return;
  const auto& particles = entry.sim->getParticles();
  int resX   = entry.sim->getResolutionX();
  int resZ   = entry.sim->getResolutionZ();
  int vCount = resX * resZ;

  auto& verts = entry.vertexScratch;
  verts.resize(vCount);

  std::vector<glm::vec3> normals(vCount, glm::vec3(0.0f));

  for (int z = 0; z < resZ - 1; ++z) {
    for (int x = 0; x < resX - 1; ++x) {
      int i00 = entry.sim->particleIndex(x,     z    );
      int i10 = entry.sim->particleIndex(x + 1, z    );
      int i01 = entry.sim->particleIndex(x,     z + 1);
      int i11 = entry.sim->particleIndex(x + 1, z + 1);

      glm::vec3 v00 = particles[i00].position;
      glm::vec3 v10 = particles[i10].position;
      glm::vec3 v01 = particles[i01].position;
      glm::vec3 v11 = particles[i11].position;

      glm::vec3 n1 = glm::cross(v01 - v00, v10 - v00);
      glm::vec3 n2 = glm::cross(v10 - v11, v01 - v11);

      normals[i00] += n1;
      normals[i01] += n1 + n2;
      normals[i10] += n1 + n2;
      normals[i11] += n2;
    }
  }

  for (int i = 0; i < vCount; ++i) {
    int x  = i % resX;
    int z  = i / resX;
    float fx = (resX > 1) ? static_cast<float>(x) / (resX - 1) : 0.5f;
    float fz = (resZ > 1) ? static_cast<float>(z) / (resZ - 1) : 0.5f;

    glm::vec3 n = (glm::length(normals[i]) > 1e-7f)
                      ? glm::normalize(normals[i])
                      : glm::vec3(0.0f, 0.0f, 1.0f);

    verts[i].pos      = particles[i].position;
    verts[i].normal   = n;
    verts[i].texCoord = glm::vec2(fx, fz);
    verts[i].color    = glm::vec3(1.0f);
  }

  meshManager->updateDynamicMeshVertices(entry.meshID, verts);
}

std::vector<Vertex> ClothSystem::buildVertices(const jphys::ClothSim& sim) {
  const auto& particles = sim.getParticles();
  int resX   = sim.getResolutionX();
  int resZ   = sim.getResolutionZ();
  int vCount = resX * resZ;

  std::vector<Vertex> verts(vCount);
  for (int i = 0; i < vCount; ++i) {
    int x  = i % resX;
    int z  = i / resX;
    float fx = (resX > 1) ? static_cast<float>(x) / (resX - 1) : 0.5f;
    float fz = (resZ > 1) ? static_cast<float>(z) / (resZ - 1) : 0.5f;
    verts[i].pos      = particles[i].position;
    verts[i].normal   = glm::vec3(0.0f, 0.0f, 1.0f);
    verts[i].texCoord = glm::vec2(fx, fz);
    verts[i].color    = glm::vec3(1.0f);
  }
  return verts;
}

std::vector<uint16_t> ClothSystem::buildIndices(const jphys::ClothSim& sim) {
  int resX = sim.getResolutionX();
  int resZ = sim.getResolutionZ();
  std::vector<uint16_t> inds;
  inds.reserve(static_cast<size_t>((resX - 1) * (resZ - 1) * 6));

  for (int z = 0; z < resZ - 1; ++z) {
    for (int x = 0; x < resX - 1; ++x) {
      uint16_t i00 = static_cast<uint16_t>(sim.particleIndex(x,     z    ));
      uint16_t i10 = static_cast<uint16_t>(sim.particleIndex(x + 1, z    ));
      uint16_t i01 = static_cast<uint16_t>(sim.particleIndex(x,     z + 1));
      uint16_t i11 = static_cast<uint16_t>(sim.particleIndex(x + 1, z + 1));
      inds.push_back(i00); inds.push_back(i01); inds.push_back(i10);
      inds.push_back(i10); inds.push_back(i01); inds.push_back(i11);
    }
  }
  return inds;
}
