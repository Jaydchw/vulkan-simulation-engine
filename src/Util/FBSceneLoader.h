#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "ECS/Registry.h"
#include "Resources/MaterialManager.h"
#include "Resources/MeshManager.h"

struct FBWorldSettings {
    bool gravityOn = true;
    std::string name;
    std::string description;
    // Explicit ownership overrides loaded from SimulatedObject::owner fields.
    // Maps entity ID → peer ID (1-4). Applied after auto round-robin assignment.
    std::unordered_map<uint32_t, uint8_t> entityOwners;
};

class FBSceneLoader {
public:
    FBSceneLoader(MeshManager* mm, MaterialManager* matm);

    bool load(const std::string& filepath, Registry& registry, FBWorldSettings& settings);
    static std::vector<std::string> listScenes(const std::string& directory);

private:
    MeshManager* meshManager;
    MaterialManager* materialManager;

    std::unordered_map<std::string, MaterialID> namedMaterials;
    std::unordered_map<std::string, float>      materialDensities;

    struct Interaction {
        float restitution     = 0.5f;
        float staticFriction  = 0.4f;
        float dynamicFriction = 0.3f;
    };
    std::unordered_map<std::string, Interaction> interactions;

    Interaction     findInteraction(const std::string& matA, const std::string& matB) const;
    static std::string interactionKey(const std::string& a, const std::string& b);

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
};
