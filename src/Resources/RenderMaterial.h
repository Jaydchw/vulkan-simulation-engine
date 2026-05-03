#pragma once
#include <vulkan/vulkan.h>
#include "vma/vk_mem_alloc.h"

#include <glm/glm.hpp>
#include <string>

#include "Texture.h"

using RenderMaterialID = uint32_t;
constexpr RenderMaterialID INVALID_RENDER_MATERIAL_ID = UINT32_MAX;

struct RenderMaterialProperties {
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

class RenderMaterial final {
 public:
  RenderMaterial() = default;
  RenderMaterial(const RenderMaterial&) = default;
  RenderMaterial& operator=(const RenderMaterial&) = default;
  ~RenderMaterial() = default;

  void reset() {
    properties = RenderMaterialProperties{};
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

  inline void getProperties(RenderMaterialProperties& outProps) const {
    outProps = properties;
  }
  inline void setProperties(const RenderMaterialProperties& props) {
    properties = props;
  }

  inline VkBuffer getPropertiesBuffer() const { return propertiesBuffer; }
  inline void setPropertiesBuffer(VkBuffer buffer) {
    propertiesBuffer = buffer;
  }

  inline VmaAllocation getPropertiesBufferAllocation() const {
    return propertiesBufferAllocation;
  }
  inline void setPropertiesBufferAllocation(VmaAllocation alloc) {
    propertiesBufferAllocation = alloc;
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
  RenderMaterialProperties properties;

  VkBuffer propertiesBuffer = VK_NULL_HANDLE;
  VmaAllocation propertiesBufferAllocation = VK_NULL_HANDLE;
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

class RenderMaterialBuilder final {
 public:
  RenderMaterialBuilder() { material.setProperties(RenderMaterialProperties{}); }

  RenderMaterialBuilder& name(const std::string& n) {
    material.setName(n);
    return *this;
  }

  RenderMaterialBuilder& albedoMap(TextureID id) {
    material.setAlbedoMap(id);
    return *this;
  }

  RenderMaterialBuilder& albedoMap(const std::string& filepath) {
    hasAlbedoTexture = true;
    albedoFilepath = filepath;
    return *this;
  }

  RenderMaterialBuilder& albedoColor(const glm::vec3& color) {
    RenderMaterialProperties props;
    material.getProperties(props);
    props.albedoColor = glm::vec4(color, 1.0f);
    material.setProperties(props);
    return *this;
  }

  RenderMaterialBuilder& albedoColor(float r, float g, float b) {
    RenderMaterialProperties props;
    material.getProperties(props);
    props.albedoColor = glm::vec4(r, g, b, 1.0f);
    material.setProperties(props);
    return *this;
  }

  RenderMaterialBuilder& normalMap(TextureID id) {
    material.setNormalMap(id);
    return *this;
  }

  RenderMaterialBuilder& normalMap(const std::string& filepath) {
    hasNormalTexture = true;
    normalFilepath = filepath;
    return *this;
  }

  RenderMaterialBuilder& roughnessMap(TextureID id) {
    material.setRoughnessMap(id);
    return *this;
  }

  RenderMaterialBuilder& roughnessMap(const std::string& filepath) {
    hasRoughnessTexture = true;
    roughnessFilepath = filepath;
    return *this;
  }

  RenderMaterialBuilder& roughness(float value) {
    RenderMaterialProperties props;
    material.getProperties(props);
    props.roughness = value;
    material.setProperties(props);
    return *this;
  }

  RenderMaterialBuilder& metallicMap(TextureID id) {
    material.setMetallicMap(id);
    return *this;
  }

  RenderMaterialBuilder& metallicMap(const std::string& filepath) {
    hasMetallicTexture = true;
    metallicFilepath = filepath;
    return *this;
  }

  RenderMaterialBuilder& metallic(float value) {
    RenderMaterialProperties props;
    material.getProperties(props);
    props.metallic = value;
    material.setProperties(props);
    return *this;
  }

  RenderMaterialBuilder& emissiveMap(TextureID id) {
    material.setEmissiveMap(id);
    return *this;
  }

  RenderMaterialBuilder& emissiveMap(const std::string& filepath) {
    hasEmissiveTexture = true;
    emissiveFilepath = filepath;
    return *this;
  }

  RenderMaterialBuilder& emissiveIntensity(float value) {
    RenderMaterialProperties props;
    material.getProperties(props);
    props.emissiveIntensity = value;
    material.setProperties(props);
    return *this;
  }

  RenderMaterialBuilder& heightMap(TextureID id) {
    material.setHeightMap(id);
    return *this;
  }

  RenderMaterialBuilder& heightMap(const std::string& filepath) {
    hasHeightTexture = true;
    heightFilepath = filepath;
    return *this;
  }

  RenderMaterialBuilder& heightScale(float value) {
    RenderMaterialProperties props;
    material.getProperties(props);
    props.heightScale = value;
    material.setProperties(props);
    return *this;
  }

  RenderMaterialBuilder& aoMap(TextureID id) {
    material.setAoMap(id);
    return *this;
  }

  RenderMaterialBuilder& aoMap(const std::string& filepath) {
    hasAOTexture = true;
    aoFilepath = filepath;
    return *this;
  }

  RenderMaterialBuilder& transparent(bool enabled = true) {
    material.setIsTransparent(enabled);
    return *this;
  }

  RenderMaterialBuilder& opacity(float value) {
    RenderMaterialProperties props;
    material.getProperties(props);
    props.opacity = value;
    material.setProperties(props);
    return *this;
  }

  RenderMaterialBuilder& indexOfRefraction(float ior) {
    RenderMaterialProperties props;
    material.getProperties(props);
    props.indexOfRefraction = ior;
    material.setProperties(props);
    return *this;
  }

  RenderMaterialBuilder& doubleSided(bool enabled = true) {
    material.setDoubleSided(enabled);
    return *this;
  }

  RenderMaterialBuilder& textureScale(float value) {
    RenderMaterialProperties props;
    material.getProperties(props);
    props.textureScale = value;
    material.setProperties(props);
    return *this;
  }

  RenderMaterial* build() const { return new RenderMaterial(material); }

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

  RenderMaterial material;

  bool hasAlbedoTexture = false;
  bool hasNormalTexture = false;
  bool hasRoughnessTexture = false;
  bool hasMetallicTexture = false;
  bool hasEmissiveTexture = false;
  bool hasHeightTexture = false;
  bool hasAOTexture = false;
};
