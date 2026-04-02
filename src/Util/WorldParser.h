#pragma once
#include <string>
#include <vector>

#include "ECS/Registry.h"
#include "Environment/EnvironmentSettings.h"
#include "Resources/MaterialManager.h"
#include "Resources/MeshManager.h"
#include "Resources/ProceduralTexture.h"
#include "Resources/TextureManager.h"

struct WorldSettings {
  glm::vec4 clearColor = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
  float timeSpeed = 1.0f;
  int simulationHz = 60;   // Physics steps per second (step size = 1/Hz)
  int maxFps = 0;          // Render frame rate cap (0 = unlimited)
  EnvironmentSettings environment;
  bool killboxEnabled = true;
  float killboxY = -150.0f;
};

class WorldParser final {
 public:
  WorldParser(MeshManager* mm, RenderMaterialManager* matm, TextureManager* tm);

  bool load(const std::string& filepath, Registry& registry,
            WorldSettings& settings);

  static std::vector<std::string> listWorlds(const std::string& directory);

 private:
  MeshManager* meshManager;
  RenderMaterialManager* materialManager;
  TextureManager* textureManager;

  std::unordered_map<std::string, TextureID> namedTextures;
  std::unordered_map<std::string, RenderMaterialID> namedMaterials;
  std::unordered_map<std::string, MeshID> namedMeshes;

  std::string trim(const std::string& str) const;
  glm::vec3 parseVec3(const std::string& value) const;
  glm::vec4 parseVec4(const std::string& value) const;
  float parseFloat(const std::string& value) const;
  int parseInt(const std::string& value) const;
  bool parseBool(const std::string& value) const;
};
