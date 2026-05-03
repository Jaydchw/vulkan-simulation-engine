#pragma once
#include <memory>
#include <unordered_map>
#include <vector>

#include <ClothSim.h>
#include "ECS/Entity.h"
#include "Environment/EnvironmentSettings.h"
#include "Resources/MeshManager.h"

class Registry;
class NetworkManager;
class PhysicsSystem;

class ClothSystem final {
 public:
  ClothSystem() = default;
  ~ClothSystem() = default;
  ClothSystem(const ClothSystem&) = delete;
  ClothSystem& operator=(const ClothSystem&) = delete;

  void setNetworkManager(NetworkManager* nm) { networkManager = nm; }
  void setPhysicsSystem(PhysicsSystem* ps) { physicsSystem = ps; }
  void setEnvironmentSettings(const EnvironmentSettings* settings) { environmentSettings = settings; }

  void setRegistry(Registry* reg, MeshManager* mm);
  void update(float deltaTime);

 private:
  struct ClothEntry {
    std::unique_ptr<jphys::ClothSim> sim;
    MeshID meshID = INVALID_MESH_ID;
    std::vector<Vertex> vertexScratch;
    std::vector<uint16_t> indexScratch;
    bool hasTearing = false;
  };

  Registry* registry = nullptr;
  MeshManager* meshManager = nullptr;
  NetworkManager* networkManager = nullptr;
  PhysicsSystem* physicsSystem = nullptr;
  const EnvironmentSettings* environmentSettings = nullptr;

  std::unordered_map<Entity, ClothEntry> entries;

  void initCloth(Entity e);
  void updateMesh(Entity e, ClothEntry& entry);
  std::vector<Vertex> buildVertices(const jphys::ClothSim& sim);
  std::vector<uint16_t> buildIndices(const jphys::ClothSim& sim);
};
