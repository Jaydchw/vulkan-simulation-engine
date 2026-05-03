#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "ECS/Registry.h"
#include "Physics/PhysicsMaterialManager.h"
#include "Resources/RenderMaterialManager.h"
#include "Resources/MeshManager.h"
#include "Resources/RenderMaterial.h"

struct FBWorldSettings {
    bool gravityOn = true;
    std::string name;
    std::string description;
    std::unordered_map<uint32_t, uint8_t> entityOwners;
};

class FBSceneLoader {
public:
    FBSceneLoader(MeshManager* mm, RenderMaterialManager* matm, PhysicsMaterialManager* physMatManager);

    bool load(const std::string& filepath, Registry& registry, FBWorldSettings& settings);
    static std::vector<std::string> listScenes(const std::string& directory);

private:
    MeshManager*            meshManager;
    RenderMaterialManager*  renderMaterialManager;
    PhysicsMaterialManager* physMatManager;

    Entity buildObject(Registry& registry,
                       const std::string& name,
                       glm::vec3 position,
                       glm::vec3 eulerDeg,
                       glm::vec3 scale,
                       uint8_t   shapeType,
                       float     sphereRadius,
                       glm::vec3 cuboidSize,
                       float     capsRadius,
                       float     capsHeight,
                       float     cylRadius,
                       float     cylHeight,
                       glm::vec3 planeNormal,
                       uint8_t   behaviourType,
                       glm::vec3 linearVel,
                       glm::vec3 angularVelDeg,
                       bool      gravityOn,
                       const std::string& materialName) const;

    Entity buildContainerObject(Registry& registry,
                               const std::string& name,
                               glm::vec3 position,
                               glm::vec3 eulerDeg,
                               glm::vec3 scale,
                               uint8_t   shapeType,
                               glm::vec3 cuboidSize,
                               float     cylRadius,
                               float     cylHeight,
                               const std::string& materialName) const;

    void buildSpawner(Registry& registry,
                      const std::string& name,
                      float     startTime,
                      float     spawnInterval,
                      int       maxSpawns,
                      glm::vec3 spawnPos,
                      float     posRandomness,
                      glm::vec3 avgLinVel,
                      bool      gravityOn,
                      const std::string& materialName,
                      uint8_t   spawnerShape,
                      float     rMin, float rMax,
                      float     hMin, float hMax,
                      glm::vec3 sMin, glm::vec3 sMax,
                      glm::quat spawnRot,
                      glm::vec3 avgAngVel,
                      float     angVelRandomness,
                      bool      burstMode,
                      uint8_t   ownerMode,
                      bool      useBoxSpawn,
                      glm::vec3 boxMin,
                      glm::vec3 boxMax) const;

    bool loadBinary(const std::string& filepath, Registry& registry, FBWorldSettings& settings);
    bool loadJSON  (const std::string& filepath, Registry& registry, FBWorldSettings& settings);

    RenderMaterialID getOrCreateRenderMaterial(const std::string& materialName) const;

    mutable std::unordered_map<std::string, RenderMaterialID> renderMatCache;
};
