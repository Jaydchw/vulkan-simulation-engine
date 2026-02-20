#include "WorldParser.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ECS/EntityBuilder.h"
#include "Util/Debug.h"

WorldParser::WorldParser(MeshManager* mm, MaterialManager* matm,
                         TextureManager* tm)
    : meshManager(mm), materialManager(matm), textureManager(tm) {}

std::vector<std::string> WorldParser::listWorlds(
    const std::string& directory) {
  std::vector<std::string> results;
  std::error_code ec;
  if (!std::filesystem::exists(directory, ec)) return results;
  for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
    if (entry.is_regular_file() && entry.path().extension() == ".world") {
      results.push_back(entry.path().string());
    }
  }
  std::sort(results.begin(), results.end());
  return results;
}

bool WorldParser::load(const std::string& filepath, Registry& registry,
                       WorldSettings& settings) {
  std::ifstream file(filepath);
  if (!file.is_open()) {
    Debug::log(Debug::Category::MAIN, "WorldParser: Failed to open: ",
               filepath);
    return false;
  }

  Debug::log(Debug::Category::MAIN, "WorldParser: Loading world: ", filepath);

  namedTextures.clear();
  namedMaterials.clear();
  namedMeshes.clear();

  settings = WorldSettings{};

  std::string line;
  int lineNum = 0;

  while (std::getline(file, line)) {
    lineNum++;
    line = trim(line);

    if (line.empty() || line[0] == '#') continue;

    if (line == "BeginSettings") {
      while (std::getline(file, line)) {
        lineNum++;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line == "EndSettings") break;

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (key == "ClearColor")
          settings.clearColor = parseVec4(val);
        else if (key == "TimeSpeed")
          settings.timeSpeed = parseFloat(val);
      }
      continue;
    }

    if (line == "BeginTexture") {
      std::string texName;
      std::string texType = "solid";
      glm::vec3 colorA(1.0f);
      glm::vec3 colorB(0.0f);
      uint32_t size = 256;
      uint32_t divisions = 8;
      uint32_t stripeCount = 8;
      bool vertical = false;

      while (std::getline(file, line)) {
        lineNum++;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line == "EndTexture") break;

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (key == "Name")
          texName = val;
        else if (key == "Type")
          texType = val;
        else if (key == "Color" || key == "ColorA")
          colorA = parseVec3(val);
        else if (key == "ColorB")
          colorB = parseVec3(val);
        else if (key == "Size")
          size = static_cast<uint32_t>(parseInt(val));
        else if (key == "Divisions")
          divisions = static_cast<uint32_t>(parseInt(val));
        else if (key == "StripeCount")
          stripeCount = static_cast<uint32_t>(parseInt(val));
        else if (key == "Vertical")
          vertical = parseBool(val);
      }

      if (texName.empty()) continue;

      TextureID texID = INVALID_TEXTURE_ID;
      if (texType == "solid")
        texID = ProceduralTexture::solid(textureManager, colorA,
                                         std::max(size, 4u));
      else if (texType == "checker")
        texID = ProceduralTexture::checker(textureManager, colorA, colorB, size,
                                           divisions);
      else if (texType == "gradient")
        texID = ProceduralTexture::linearGradient(textureManager, colorA,
                                                  colorB, size, vertical);
      else if (texType == "radial")
        texID = ProceduralTexture::radialGradient(textureManager, colorA,
                                                  colorB, size);
      else if (texType == "stripe")
        texID = ProceduralTexture::stripe(textureManager, colorA, colorB, size,
                                          stripeCount, vertical);

      if (texID != INVALID_TEXTURE_ID) {
        namedTextures[texName] = texID;
        Debug::log(Debug::Category::MAIN,
                   "WorldParser: Created texture '", texName, "'");
      }
      continue;
    }

    if (line == "BeginMaterial") {
      std::string matName;
      glm::vec3 albedoColor(1.0f);
      float roughness = 0.5f;
      float metallic = 0.0f;
      float textureScale = 1.0f;
      std::string albedoTexture;

      while (std::getline(file, line)) {
        lineNum++;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line == "EndMaterial") break;

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (key == "Name")
          matName = val;
        else if (key == "AlbedoColor")
          albedoColor = parseVec3(val);
        else if (key == "Roughness")
          roughness = parseFloat(val);
        else if (key == "Metallic")
          metallic = parseFloat(val);
        else if (key == "TextureScale")
          textureScale = parseFloat(val);
        else if (key == "AlbedoTexture")
          albedoTexture = val;
      }

      if (matName.empty()) continue;

      MaterialBuilder builder;
      builder.name(matName);
      builder.albedoColor(albedoColor);
      builder.roughness(roughness);
      builder.metallic(metallic);
      builder.textureScale(textureScale);

      if (!albedoTexture.empty()) {
        auto it = namedTextures.find(albedoTexture);
        if (it != namedTextures.end()) {
          builder.albedoMap(it->second);
        }
      }

      namedMaterials[matName] =
          materialManager->registerMaterial(builder);
      Debug::log(Debug::Category::MAIN,
                 "WorldParser: Created material '", matName, "'");
      continue;
    }

    if (line == "BeginMesh") {
      std::string meshName;
      std::string meshType = "cube";
      float sizeVal = 1.0f;
      float width = 1.0f;
      float height = 1.0f;
      float radius = 1.0f;
      uint32_t segments = 32;

      while (std::getline(file, line)) {
        lineNum++;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line == "EndMesh") break;

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (key == "Name")
          meshName = val;
        else if (key == "Type")
          meshType = val;
        else if (key == "Size")
          sizeVal = parseFloat(val);
        else if (key == "Width")
          width = parseFloat(val);
        else if (key == "Height")
          height = parseFloat(val);
        else if (key == "Radius")
          radius = parseFloat(val);
        else if (key == "Segments")
          segments = static_cast<uint32_t>(parseInt(val));
      }

      if (meshName.empty()) continue;

      MeshID meshID = INVALID_MESH_ID;
      if (meshType == "cube")
        meshID = meshManager->createCube(sizeVal);
      else if (meshType == "sphere")
        meshID = meshManager->createSphere(radius, segments);
      else if (meshType == "plane")
        meshID = meshManager->createPlane(width, height);
      else if (meshType == "cylinder")
        meshID = meshManager->createCylinder(radius, height, segments);

      if (meshID != INVALID_MESH_ID) {
        namedMeshes[meshName] = meshID;
        Debug::log(Debug::Category::MAIN,
                   "WorldParser: Created mesh '", meshName, "'");
      }
      continue;
    }

    if (line == "BeginObject") {
      std::string objName = "Unnamed";
      glm::vec3 position(0.0f);
      glm::vec3 eulerRotation(0.0f);
      glm::vec3 objScale(1.0f);
      std::string meshRef;
      std::string materialRef;
      bool visible = true;
      bool hasPhysics = false;
      glm::vec3 velocity(0.0f);
      float mass = 1.0f;
      float restitution = 0.5f;
      float physicsDamping = 0.99f;
      bool useGravity = true;
      bool hasCollider = false;
      std::string colliderType;
      float colliderRadius = 1.0f;
      glm::vec3 colliderHalfExtents(0.5f);
      glm::vec3 colliderNormal(0.0f, 1.0f, 0.0f);

      while (std::getline(file, line)) {
        lineNum++;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line == "EndObject") break;

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (key == "Name")
          objName = val;
        else if (key == "Position")
          position = parseVec3(val);
        else if (key == "Rotation")
          eulerRotation = parseVec3(val);
        else if (key == "Scale")
          objScale = parseVec3(val);
        else if (key == "Mesh")
          meshRef = val;
        else if (key == "Material")
          materialRef = val;
        else if (key == "Visible")
          visible = parseBool(val);
        else if (key == "Physics") {
          hasPhysics = parseBool(val);
        } else if (key == "Velocity")
          velocity = parseVec3(val);
        else if (key == "Mass") {
          hasPhysics = true;
          mass = parseFloat(val);
        } else if (key == "Restitution") {
          hasPhysics = true;
          restitution = parseFloat(val);
        } else if (key == "Damping") {
          hasPhysics = true;
          physicsDamping = parseFloat(val);
        } else if (key == "UseGravity") {
          hasPhysics = true;
          useGravity = parseBool(val);
        } else if (key == "Collider") {
          hasCollider = true;
          colliderType = val;
        } else if (key == "ColliderRadius") {
          hasCollider = true;
          colliderRadius = parseFloat(val);
        } else if (key == "ColliderHalfExtents") {
          hasCollider = true;
          colliderHalfExtents = parseVec3(val);
        } else if (key == "ColliderNormal") {
          hasCollider = true;
          colliderNormal = parseVec3(val);
        }
      }

      EntityBuilder builder;
      builder.name(objName);
      builder.position(position);
      if (eulerRotation != glm::vec3(0.0f))
        builder.rotationEuler(eulerRotation);
      builder.scale(objScale);
      builder.visible(visible);

      auto meshIt = namedMeshes.find(meshRef);
      if (meshIt != namedMeshes.end()) builder.mesh(meshIt->second);

      auto matIt = namedMaterials.find(materialRef);
      if (matIt != namedMaterials.end()) builder.material(matIt->second);

      if (hasPhysics) {
        builder.mass(mass);
        builder.restitution(restitution);
        builder.damping(physicsDamping);
        builder.useGravity(useGravity);
        if (velocity != glm::vec3(0.0f))
          builder.velocity(velocity);
      }

      if (hasCollider) {
        if (colliderType == "sphere")
          builder.sphereCollider(colliderRadius);
        else if (colliderType == "box")
          builder.boxCollider(colliderHalfExtents);
        else if (colliderType == "plane") {
          if (colliderHalfExtents != glm::vec3(0.5f))
            builder.planeCollider(colliderNormal, colliderHalfExtents);
          else
            builder.planeCollider(colliderNormal);
        }
      }

      builder.build(registry);
      continue;
    }

    if (line == "BeginLight") {
      std::string lightName = "Unnamed Light";
      std::string lightType = "point";
      glm::vec3 position(0.0f);
      glm::vec3 direction(0.0f, -1.0f, 0.0f);
      glm::vec3 color(1.0f);
      float intensity = 1.0f;
      float constant = 1.0f;
      float linear = 0.09f;
      float quadratic = 0.032f;
      bool shadows = true;

      while (std::getline(file, line)) {
        lineNum++;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line == "EndLight") break;

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (key == "Name")
          lightName = val;
        else if (key == "Type")
          lightType = val;
        else if (key == "Position")
          position = parseVec3(val);
        else if (key == "Direction")
          direction = parseVec3(val);
        else if (key == "Color")
          color = parseVec3(val);
        else if (key == "Intensity")
          intensity = parseFloat(val);
        else if (key == "Constant")
          constant = parseFloat(val);
        else if (key == "Linear")
          linear = parseFloat(val);
        else if (key == "Quadratic")
          quadratic = parseFloat(val);
        else if (key == "CastsShadows")
          shadows = parseBool(val);
      }

      EntityBuilder builder;
      builder.name(lightName);
      builder.position(position);

      if (lightType == "sun") {
        builder.lightType(LightType::Sun);
        builder.direction(direction);
      } else {
        builder.lightType(LightType::Point);
      }

      builder.color(color);
      builder.intensity(intensity);
      builder.attenuation(constant, linear, quadratic);
      builder.castsShadows(shadows);
      builder.build(registry);
      continue;
    }
  }

  Debug::log(Debug::Category::MAIN, "WorldParser: Finished loading world: ",
             filepath);
  return true;
}

std::string WorldParser::trim(const std::string& str) const {
  const size_t first = str.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return "";
  const size_t last = str.find_last_not_of(" \t\r\n");
  return str.substr(first, last - first + 1);
}

glm::vec3 WorldParser::parseVec3(const std::string& value) const {
  glm::vec3 result(0.0f);
  std::istringstream ss(value);
  char comma;
  ss >> result.x >> comma >> result.y >> comma >> result.z;
  return result;
}

glm::vec4 WorldParser::parseVec4(const std::string& value) const {
  glm::vec4 result(0.0f);
  std::istringstream ss(value);
  char comma;
  ss >> result.x >> comma >> result.y >> comma >> result.z >> comma >> result.w;
  return result;
}

float WorldParser::parseFloat(const std::string& value) const {
  try {
    return std::stof(value);
  } catch (...) {
    return 0.0f;
  }
}

int WorldParser::parseInt(const std::string& value) const {
  try {
    return std::stoi(value);
  } catch (...) {
    return 0;
  }
}

bool WorldParser::parseBool(const std::string& value) const {
  std::string v = value;
  std::transform(v.begin(), v.end(), v.begin(), ::tolower);
  return v == "true" || v == "1" || v == "yes";
}
