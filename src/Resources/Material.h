#pragma once
#include <vulkan/vulkan.h>

#include <glm/glm.hpp>
#include <string>

#include "Texture.h"

using MaterialID = uint32_t;
constexpr MaterialID INVALID_MATERIAL_ID = 0;

struct MaterialProperties {
  glm::vec4 albedoColor = glm::vec4(1.0f);
  float roughness = 0.5f;
  float metallic = 0.0f;
  float emissiveIntensity = 0.0f;
  float opacity = 1.0f;
  float indexOfRefraction = 1.5f;
  float heightScale = 0.05f;
  float textureScale = 1.0f;
  float padding2 = 0.0f;
};

class Material final {
 public:
  Material() = default;
  Material(const Material&) = default;
  Material& operator=(const Material&) = default;
  ~Material() = default;

  void reset() {
    properties = MaterialProperties{};
    albedoMap = INVALID_TEXTURE_ID;
    normalMap = INVALID_TEXTURE_ID;
    roughnessMap = INVALID_TEXTURE_ID;
    metallicMap = INVALID_TEXTURE_ID;
    emissiveMap = INVALID_TEXTURE_ID;
    heightMap = INVALID_TEXTURE_ID;
    aoMap = INVALID_TEXTURE_ID;
    isTransparent = false;
    doubleSided = false;
  }

  bool isValid() const {
    return properties.opacity >= 0.0f && properties.opacity <= 1.0f;
  }

  inline TextureID getAlbedoMap() const { return albedoMap; }
  inline void setAlbedoMap(TextureID id) { albedoMap = id; }

  inline TextureID getNormalMap() const { return normalMap; }
  inline void setNormalMap(TextureID id) { normalMap = id; }

  inline TextureID getRoughnessMap() const { return roughnessMap; }
  inline void setRoughnessMap(TextureID id) { roughnessMap = id; }

  inline TextureID getMetallicMap() const { return metallicMap; }
  inline void setMetallicMap(TextureID id) { metallicMap = id; }

  inline TextureID getEmissiveMap() const { return emissiveMap; }
  inline void setEmissiveMap(TextureID id) { emissiveMap = id; }

  inline TextureID getHeightMap() const { return heightMap; }
  inline void setHeightMap(TextureID id) { heightMap = id; }

  inline TextureID getAoMap() const { return aoMap; }
  inline void setAoMap(TextureID id) { aoMap = id; }

  inline void getProperties(MaterialProperties& outProps) const {
    outProps = properties;
  }
  inline void setProperties(const MaterialProperties& props) {
    properties = props;
  }

  inline VkBuffer getPropertiesBuffer() const { return propertiesBuffer; }
  inline void setPropertiesBuffer(VkBuffer buffer) {
    propertiesBuffer = buffer;
  }

  inline VkDeviceMemory getPropertiesBufferMemory() const {
    return propertiesBufferMemory;
  }
  inline void setPropertiesBufferMemory(VkDeviceMemory memory) {
    propertiesBufferMemory = memory;
  }

  inline bool getIsTransparent() const { return isTransparent; }
  inline void setIsTransparent(bool value) { isTransparent = value; }

  inline bool getDoubleSided() const { return doubleSided; }
  inline void setDoubleSided(bool value) { doubleSided = value; }

  inline void getName(std::string& outName) const { outName = name; }
  inline void setName(const std::string& newName) { name = newName; }

  inline VkDescriptorSet getDescriptorSet() const { return descriptorSet; }
  inline void setDescriptorSet(VkDescriptorSet set) { descriptorSet = set; }

 private:
  std::string name = "Unnamed Material";
  MaterialProperties properties;

  VkBuffer propertiesBuffer = VK_NULL_HANDLE;
  VkDeviceMemory propertiesBufferMemory = VK_NULL_HANDLE;
  VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

  TextureID albedoMap = INVALID_TEXTURE_ID;
  TextureID normalMap = INVALID_TEXTURE_ID;
  TextureID roughnessMap = INVALID_TEXTURE_ID;
  TextureID metallicMap = INVALID_TEXTURE_ID;
  TextureID emissiveMap = INVALID_TEXTURE_ID;
  TextureID heightMap = INVALID_TEXTURE_ID;
  TextureID aoMap = INVALID_TEXTURE_ID;

  bool isTransparent = false;
  bool doubleSided = false;
};

class MaterialBuilder final {
 public:
  MaterialBuilder() { material.setProperties(MaterialProperties{}); }

  MaterialBuilder& name(const std::string& n) {
    material.setName(n);
    return *this;
  }

  MaterialBuilder& albedoMap(TextureID id) {
    material.setAlbedoMap(id);
    return *this;
  }

  MaterialBuilder& albedoMap(const std::string& filepath) {
    hasAlbedoTexture = true;
    albedoFilepath = filepath;
    return *this;
  }

  MaterialBuilder& albedoColor(const glm::vec3& color) {
    MaterialProperties props;
    material.getProperties(props);
    props.albedoColor = glm::vec4(color, 1.0f);
    material.setProperties(props);
    return *this;
  }

  MaterialBuilder& albedoColor(float r, float g, float b) {
    MaterialProperties props;
    material.getProperties(props);
    props.albedoColor = glm::vec4(r, g, b, 1.0f);
    material.setProperties(props);
    return *this;
  }

  MaterialBuilder& normalMap(TextureID id) {
    material.setNormalMap(id);
    return *this;
  }

  MaterialBuilder& normalMap(const std::string& filepath) {
    hasNormalTexture = true;
    normalFilepath = filepath;
    return *this;
  }

  MaterialBuilder& roughnessMap(TextureID id) {
    material.setRoughnessMap(id);
    return *this;
  }

  MaterialBuilder& roughnessMap(const std::string& filepath) {
    hasRoughnessTexture = true;
    roughnessFilepath = filepath;
    return *this;
  }

  MaterialBuilder& roughness(float value) {
    MaterialProperties props;
    material.getProperties(props);
    props.roughness = value;
    material.setProperties(props);
    return *this;
  }

  MaterialBuilder& metallicMap(TextureID id) {
    material.setMetallicMap(id);
    return *this;
  }

  MaterialBuilder& metallicMap(const std::string& filepath) {
    hasMetallicTexture = true;
    metallicFilepath = filepath;
    return *this;
  }

  MaterialBuilder& metallic(float value) {
    MaterialProperties props;
    material.getProperties(props);
    props.metallic = value;
    material.setProperties(props);
    return *this;
  }

  MaterialBuilder& emissiveMap(TextureID id) {
    material.setEmissiveMap(id);
    return *this;
  }

  MaterialBuilder& emissiveMap(const std::string& filepath) {
    hasEmissiveTexture = true;
    emissiveFilepath = filepath;
    return *this;
  }

  MaterialBuilder& emissiveIntensity(float value) {
    MaterialProperties props;
    material.getProperties(props);
    props.emissiveIntensity = value;
    material.setProperties(props);
    return *this;
  }

  MaterialBuilder& heightMap(TextureID id) {
    material.setHeightMap(id);
    return *this;
  }

  MaterialBuilder& heightMap(const std::string& filepath) {
    hasHeightTexture = true;
    heightFilepath = filepath;
    return *this;
  }

  MaterialBuilder& heightScale(float value) {
    MaterialProperties props;
    material.getProperties(props);
    props.heightScale = value;
    material.setProperties(props);
    return *this;
  }

  MaterialBuilder& aoMap(TextureID id) {
    material.setAoMap(id);
    return *this;
  }

  MaterialBuilder& aoMap(const std::string& filepath) {
    hasAOTexture = true;
    aoFilepath = filepath;
    return *this;
  }

  MaterialBuilder& transparent(bool enabled = true) {
    material.setIsTransparent(enabled);
    return *this;
  }

  MaterialBuilder& opacity(float value) {
    MaterialProperties props;
    material.getProperties(props);
    props.opacity = value;
    material.setProperties(props);
    return *this;
  }

  MaterialBuilder& indexOfRefraction(float ior) {
    MaterialProperties props;
    material.getProperties(props);
    props.indexOfRefraction = ior;
    material.setProperties(props);
    return *this;
  }

  MaterialBuilder& doubleSided(bool enabled = true) {
    material.setDoubleSided(enabled);
    return *this;
  }

  MaterialBuilder& textureScale(float value) {
    MaterialProperties props;
    material.getProperties(props);
    props.textureScale = value;
    material.setProperties(props);
    return *this;
  }

  Material* build() const { return new Material(material); }

  inline bool getHasAlbedoTexture() const { return hasAlbedoTexture; }
  inline void getAlbedoFilepath(std::string& out) const {
    out = albedoFilepath;
  }

  inline bool getHasNormalTexture() const { return hasNormalTexture; }
  inline void getNormalFilepath(std::string& out) const {
    out = normalFilepath;
  }

  inline bool getHasRoughnessTexture() const { return hasRoughnessTexture; }
  inline void getRoughnessFilepath(std::string& out) const {
    out = roughnessFilepath;
  }

  inline bool getHasMetallicTexture() const { return hasMetallicTexture; }
  inline void getMetallicFilepath(std::string& out) const {
    out = metallicFilepath;
  }

  inline bool getHasEmissiveTexture() const { return hasEmissiveTexture; }
  inline void getEmissiveFilepath(std::string& out) const {
    out = emissiveFilepath;
  }

  inline bool getHasHeightTexture() const { return hasHeightTexture; }
  inline void getHeightFilepath(std::string& out) const {
    out = heightFilepath;
  }

  inline bool getHasAOTexture() const { return hasAOTexture; }
  inline void getAoFilepath(std::string& out) const { out = aoFilepath; }

 private:
  std::string albedoFilepath;
  std::string normalFilepath;
  std::string roughnessFilepath;
  std::string metallicFilepath;
  std::string emissiveFilepath;
  std::string heightFilepath;
  std::string aoFilepath;

  Material material;

  bool hasAlbedoTexture = false;
  bool hasNormalTexture = false;
  bool hasRoughnessTexture = false;
  bool hasMetallicTexture = false;
  bool hasEmissiveTexture = false;
  bool hasHeightTexture = false;
  bool hasAOTexture = false;
};