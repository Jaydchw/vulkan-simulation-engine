#pragma once
#include <string>
#include <vector>

#include "ECS/Registry.h"
#include "Resources/MaterialManager.h"
#include "Resources/MeshManager.h"
#include "Resources/ProceduralTexture.h"
#include "Resources/TextureManager.h"

struct WorldSettings {
  glm::vec4 clearColor = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
  float timeSpeed = 1.0f;
};

class WorldParser final {
 public:
  WorldParser(MeshManager* mm, MaterialManager* matm, TextureManager* tm);

  bool load(const std::string& filepath, Registry& registry,
            WorldSettings& settings);

  static std::vector<std::string> listWorlds(const std::string& directory);

 private:
  MeshManager* meshManager;
  MaterialManager* materialManager;
  TextureManager* textureManager;

  std::unordered_map<std::string, TextureID> namedTextures;
  std::unordered_map<std::string, MaterialID> namedMaterials;
  std::unordered_map<std::string, MeshID> namedMeshes;

  std::string trim(const std::string& str) const;
  glm::vec3 parseVec3(const std::string& value) const;
  glm::vec4 parseVec4(const std::string& value) const;
  float parseFloat(const std::string& value) const;
  int parseInt(const std::string& value) const;
  bool parseBool(const std::string& value) const;
};
