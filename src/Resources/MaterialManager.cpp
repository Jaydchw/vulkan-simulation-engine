#include "RenderMaterialManager.h"

#include <array>
#include <fstream>
#include <functional>
#include <sstream>

#include "Util/Debug.h"

RenderMaterialManager::RenderMaterialManager(RenderDevice* rd, TextureManager* tm)
    : mtlFilepathToID(),
      materialNameToID(),
      materials(),
      renderDevice(rd),
      textureManager(tm),
      descriptorSetLayout(VK_NULL_HANDLE),
      descriptorPool(VK_NULL_HANDLE),
      defaultRenderMaterialID(0) {
  Debug::log(Debug::Category::MATERIALS, "RenderMaterialManager: Constructor called");
}

RenderMaterialManager::~RenderMaterialManager() {
  try {
    Debug::log(Debug::Category::MATERIALS,
               "RenderMaterialManager: Destructor called");
    cleanup();
  } catch (...) {
  }
}

void RenderMaterialManager::init(VkDescriptorSetLayout descSetLayout) {
  descriptorSetLayout = descSetLayout;

  const VkDevice device = renderDevice->getDevice();
  std::array<VkDescriptorPoolSize, 2> poolSizes{};
  poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  poolSizes[0].descriptorCount = 1000;
  poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSizes[1].descriptorCount = 7000;

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();
  poolInfo.maxSets = 1000;

  if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS)
    throw std::runtime_error("Failed to create material descriptor pool!");

  createDefaultMaterial();
}

void RenderMaterialManager::resetForNewScene() {
  const VkDevice device = renderDevice->getDevice();
  for (auto& mat : materials) {
    VkBuffer buf = mat->getPropertiesBuffer();
    VmaAllocation alloc = mat->getPropertiesBufferAllocation();
    if (buf != VK_NULL_HANDLE) renderDevice->destroyBuffer(buf, alloc);
  }
  materials.clear();
  mtlFilepathToID.clear();
  materialNameToID.clear();
  vkResetDescriptorPool(device, descriptorPool, 0);
  createDefaultMaterial();
}

RenderMaterialID RenderMaterialManager::registerMaterial(RenderMaterial* material) {
  if (!material) {
    Debug::log(Debug::Category::MATERIALS,
               "RenderMaterialManager: Attempted to register null material!");
    return defaultRenderMaterialID;
  }

  std::string matName;
  material->getName(matName);
  Debug::log(Debug::Category::MATERIALS,
             "RenderMaterialManager: Registering material '", matName, "'");

  if (material->getAlbedoMap() == INVALID_TEXTURE_ID) {
    material->setAlbedoMap(textureManager->getDefaultWhite());
    Debug::log(Debug::Category::MATERIALS,
               "  - Using default white for albedo map");
  }
  if (material->getNormalMap() == INVALID_TEXTURE_ID) {
    material->setNormalMap(textureManager->getDefaultNormal());
    Debug::log(Debug::Category::MATERIALS,
               "  - Using default normal for normal map");
  }
  if (material->getRoughnessMap() == INVALID_TEXTURE_ID) {
    material->setRoughnessMap(textureManager->getDefaultWhite());
    Debug::log(Debug::Category::MATERIALS,
               "  - Using default white for roughness map");
  }
  if (material->getMetallicMap() == INVALID_TEXTURE_ID) {
    material->setMetallicMap(textureManager->getDefaultBlack());
    Debug::log(Debug::Category::MATERIALS,
               "  - Using default black for metallic map");
  }
  if (material->getEmissiveMap() == INVALID_TEXTURE_ID) {
    material->setEmissiveMap(textureManager->getDefaultBlack());
    Debug::log(Debug::Category::MATERIALS,
               "  - Using default black for emissive map");
  }
  if (material->getHeightMap() == INVALID_TEXTURE_ID) {
    material->setHeightMap(textureManager->getDefaultBlack());
    Debug::log(Debug::Category::MATERIALS,
               "  - Using default black for height map");
  }
  if (material->getAoMap() == INVALID_TEXTURE_ID) {
    material->setAoMap(textureManager->getDefaultWhite());
    Debug::log(Debug::Category::MATERIALS,
               "  - Using default white for AO map");
  }

  createDescriptorSet(material);

  const RenderMaterialID id = static_cast<RenderMaterialID>(materials.size());
  materials.push_back(std::unique_ptr<RenderMaterial>(material));

  Debug::log(Debug::Category::MATERIALS,
             "RenderMaterialManager: Successfully registered material '", matName,
             "' with ID: ", id);

  return id;
}

RenderMaterialID RenderMaterialManager::registerMaterial(const RenderMaterialBuilder& builder) {
  RenderMaterial* const material = builder.build();

  std::string path;

  if (builder.getHasAlbedoTexture()) {
    builder.getAlbedoFilepath(path);
    material->setAlbedoMap(textureManager->load(path, TextureType::sRGB));
  }
  if (builder.getHasNormalTexture()) {
    builder.getNormalFilepath(path);
    material->setNormalMap(textureManager->load(path, TextureType::Linear));
  }
  if (builder.getHasRoughnessTexture()) {
    builder.getRoughnessFilepath(path);
    material->setRoughnessMap(textureManager->load(path, TextureType::Linear));
  }
  if (builder.getHasMetallicTexture()) {
    builder.getMetallicFilepath(path);
    material->setMetallicMap(textureManager->load(path, TextureType::Linear));
  }
  if (builder.getHasEmissiveTexture()) {
    builder.getEmissiveFilepath(path);
    material->setEmissiveMap(textureManager->load(path, TextureType::sRGB));
  }
  if (builder.getHasHeightTexture()) {
    builder.getHeightFilepath(path);
    material->setHeightMap(textureManager->load(path, TextureType::Linear));
  }
  if (builder.getHasAOTexture()) {
    builder.getAoFilepath(path);
    material->setAoMap(textureManager->load(path, TextureType::Linear));
  }

  return registerMaterial(material);
}

RenderMaterial* RenderMaterialManager::getMaterial(RenderMaterialID id) {
  if (id >= materials.size()) {
    Debug::log(Debug::Category::MATERIALS,
               "RenderMaterialManager: Invalid material ID requested: ", id,
               ", returning default");
    return materials[defaultRenderMaterialID].get();
  }
  return materials[id].get();
}

const RenderMaterial* RenderMaterialManager::getMaterial(RenderMaterialID id) const {
  if (id >= materials.size()) {
    Debug::log(Debug::Category::MATERIALS,
               "RenderMaterialManager: Invalid material ID requested (const): ", id,
               ", returning default");
    return materials[defaultRenderMaterialID].get();
  }
  return materials[id].get();
}

void RenderMaterialManager::updateRenderMaterialProperties(
    RenderMaterialID id, const RenderMaterialProperties& properties) {
  if (id >= materials.size()) {
    Debug::log(Debug::Category::MATERIALS,
               "RenderMaterialManager: Cannot update invalid material ID: ", id);
    return;
  }

  Debug::log(Debug::Category::MATERIALS,
             "RenderMaterialManager: Updating properties for material ID: ", id);

  materials[id]->setProperties(properties);
  updateDescriptorSet(materials[id].get());

  Debug::log(Debug::Category::MATERIALS,
             "RenderMaterialManager: Successfully updated material ID: ", id);
}

void RenderMaterialManager::cleanup() {
  const VkDevice device = renderDevice->getDevice();
  for (auto& mat : materials) {
    VkBuffer buf = mat->getPropertiesBuffer();
    VmaAllocation alloc = mat->getPropertiesBufferAllocation();
    if (buf != VK_NULL_HANDLE) renderDevice->destroyBuffer(buf, alloc);
  }
  materials.clear();
  if (descriptorPool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    descriptorPool = VK_NULL_HANDLE;
  }
}

void RenderMaterialManager::createDescriptorSet(RenderMaterial* material) const {
  std::string matName;
  material->getName(matName);
  Debug::log(Debug::Category::MATERIALS,
             "RenderMaterialManager: Creating descriptor set for material '", matName,
             "'");

  const VkDevice device = renderDevice->getDevice();
  const VkDeviceSize bufferSize = sizeof(RenderMaterialProperties);

  VkBuffer buffer;
  VmaAllocation allocation;
  void* data;

  renderDevice->createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             buffer, allocation, &data);

  material->setPropertiesBuffer(buffer);
  material->setPropertiesBufferAllocation(allocation);

  RenderMaterialProperties props;
  material->getProperties(props);
  memcpy(data, &props, bufferSize);

  Debug::log(Debug::Category::MATERIALS, "  - Created properties buffer");

  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = descriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &descriptorSetLayout;

  VkDescriptorSet set;
  if (vkAllocateDescriptorSets(device, &allocInfo, &set) != VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate descriptor set for material!");
  }

  material->setDescriptorSet(set);

  Debug::log(Debug::Category::MATERIALS, "  - Allocated descriptor set");

  updateDescriptorSet(material);

  Debug::log(Debug::Category::MATERIALS, "  - Updated descriptor set bindings");
}

void RenderMaterialManager::updateDescriptorSet(const RenderMaterial* material) const {
  const VkDevice device = renderDevice->getDevice();

  VkDescriptorBufferInfo bufferInfo{};
  bufferInfo.buffer = material->getPropertiesBuffer();
  bufferInfo.offset = 0;
  bufferInfo.range = sizeof(RenderMaterialProperties);

  std::array<VkDescriptorImageInfo, 7> imageInfos{};

  imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfos[0].imageView =
      textureManager->getImageView(material->getAlbedoMap());
  imageInfos[0].sampler = textureManager->getSampler(material->getAlbedoMap());

  imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfos[1].imageView =
      textureManager->getImageView(material->getNormalMap());
  imageInfos[1].sampler = textureManager->getSampler(material->getNormalMap());

  imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfos[2].imageView =
      textureManager->getImageView(material->getRoughnessMap());
  imageInfos[2].sampler =
      textureManager->getSampler(material->getRoughnessMap());

  imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfos[3].imageView =
      textureManager->getImageView(material->getMetallicMap());
  imageInfos[3].sampler =
      textureManager->getSampler(material->getMetallicMap());

  imageInfos[4].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfos[4].imageView =
      textureManager->getImageView(material->getEmissiveMap());
  imageInfos[4].sampler =
      textureManager->getSampler(material->getEmissiveMap());

  imageInfos[5].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfos[5].imageView =
      textureManager->getImageView(material->getHeightMap());
  imageInfos[5].sampler = textureManager->getSampler(material->getHeightMap());

  imageInfos[6].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfos[6].imageView = textureManager->getImageView(material->getAoMap());
  imageInfos[6].sampler = textureManager->getSampler(material->getAoMap());

  std::array<VkWriteDescriptorSet, 8> descriptorWrites{};

  descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrites[0].dstSet = material->getDescriptorSet();
  descriptorWrites[0].dstBinding = 0;
  descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  descriptorWrites[0].descriptorCount = 1;
  descriptorWrites[0].pBufferInfo = &bufferInfo;

  for (size_t i = 0; i < 7; i++) {
    descriptorWrites[i + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[i + 1].dstSet = material->getDescriptorSet();
    descriptorWrites[i + 1].dstBinding = static_cast<uint32_t>(i + 1);
    descriptorWrites[i + 1].descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[i + 1].descriptorCount = 1;
    descriptorWrites[i + 1].pImageInfo = &imageInfos[i];
  }

  vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()),
                         descriptorWrites.data(), 0, nullptr);
}

RenderMaterialID RenderMaterialManager::loadFromMTL(const std::string& mtlFilepath) {
  auto it = mtlFilepathToID.find(mtlFilepath);
  if (it != mtlFilepathToID.end()) {
    Debug::log(Debug::Category::MATERIALS,
               "RenderMaterialManager: MTL already loaded: ", mtlFilepath,
               " (ID: ", it->second, ")");
    return it->second;
  }

  Debug::log(Debug::Category::MATERIALS,
             "RenderMaterialManager: Loading MTL: ", mtlFilepath);

  std::ifstream file(mtlFilepath);
  if (!file.is_open()) {
    Debug::log(Debug::Category::MATERIALS,
               "RenderMaterialManager: Failed to open MTL file: ", mtlFilepath,
               ", returning default material");
    return defaultRenderMaterialID;
  }

  std::string baseDir =
      mtlFilepath.substr(0, mtlFilepath.find_last_of("/\\") + 1);
  std::vector<RenderMaterialID> loadedMaterials;
  RenderMaterial* currentMaterial = nullptr;
  std::string currentMaterialName;

  auto extractFilename = [](const std::string& path) {
    const size_t lastSlash = path.find_last_of("/\\");
    return (lastSlash != std::string::npos) ? path.substr(lastSlash + 1) : path;
  };

  auto finalizeMaterial = [&](RenderMaterial* mat, const std::string& name) {
    if (!mat) return;
    auto nameIt = materialNameToID.find(name);
    if (nameIt != materialNameToID.end()) {
      Debug::log(Debug::Category::MATERIALS, "  - Material '", name,
                 "' already exists with ID: ", nameIt->second, ", reusing");
      loadedMaterials.push_back(nameIt->second);
      delete mat;
    } else {
      const RenderMaterialID id = registerMaterial(mat);
      loadedMaterials.push_back(id);
      materialNameToID[name] = id;
      mtlFilepathToID[mtlFilepath + "::" + name] = id;
    }
  };

  std::string line;
  while (std::getline(file, line)) {
    std::istringstream iss(line);
    std::string prefix;
    iss >> prefix;

    if (prefix == "newmtl") {
      finalizeMaterial(currentMaterial, currentMaterialName);
      iss >> currentMaterialName;

      auto nameIt = materialNameToID.find(currentMaterialName);
      if (nameIt != materialNameToID.end()) {
        Debug::log(Debug::Category::MATERIALS, "  - Material '",
                   currentMaterialName,
                   "' already exists, will reuse ID: ", nameIt->second);
        loadedMaterials.push_back(nameIt->second);
        currentMaterial = nullptr;
        continue;
      }

      currentMaterial = new RenderMaterial();
      currentMaterial->setName(currentMaterialName);
      Debug::log(Debug::Category::MATERIALS,
                 "  - Found material: ", currentMaterialName);

    } else if (currentMaterial) {
      RenderMaterialProperties props;
      currentMaterial->getProperties(props);

      if (prefix == "Ns") {
        float ns;
        iss >> ns;
        props.roughness = 1.0f - (ns / 1000.0f);
        currentMaterial->setProperties(props);
      } else if (prefix == "Ka") {
      } else if (prefix == "Kd") {
        float r, g, b;
        iss >> r >> g >> b;
        props.albedoColor = glm::vec4(r, g, b, 1.0f);
        currentMaterial->setProperties(props);
      } else if (prefix == "Ks") {
        float r, g, b;
        iss >> r >> g >> b;
        const float specular = (r + g + b) / 3.0f;
        props.metallic = specular;
        currentMaterial->setProperties(props);
      } else if (prefix == "Ke") {
        float r, g, b;
        iss >> r >> g >> b;
        const float emission = (r + g + b) / 3.0f;
        props.emissiveIntensity = emission;
        currentMaterial->setProperties(props);
      } else if (prefix == "Ni") {
        float ior;
        iss >> ior;
        props.indexOfRefraction = ior;
        currentMaterial->setProperties(props);
      } else if (prefix == "d") {
        float opacity;
        iss >> opacity;
        props.opacity = opacity;
        currentMaterial->setProperties(props);
        if (opacity < 1.0f) {
          currentMaterial->setIsTransparent(true);
        }
      } else if (prefix == "illum") {
      } else if (prefix == "map_Kd") {
        std::string texPath;
        std::getline(iss, texPath);
        texPath = texPath.substr(texPath.find_first_not_of(" \t"));
        std::string filename = extractFilename(texPath);
        std::string fullPath = baseDir + "textures/" + filename;
        const TextureID texID =
            textureManager->load(fullPath, TextureType::sRGB);
        currentMaterial->setAlbedoMap(texID);
        Debug::log(Debug::Category::MATERIALS,
                   "    - Loaded albedo texture: ", fullPath);
      } else if (prefix == "map_Ks") {
        std::string texPath;
        std::getline(iss, texPath);
        texPath = texPath.substr(texPath.find_first_not_of(" \t"));
        std::string filename = extractFilename(texPath);
        std::string fullPath = baseDir + "textures/" + filename;
        const TextureID texID =
            textureManager->load(fullPath, TextureType::Linear);
        currentMaterial->setMetallicMap(texID);
        Debug::log(Debug::Category::MATERIALS,
                   "    - Loaded specular texture: ", fullPath);
      } else if (prefix == "map_Bump" || prefix == "bump") {
        std::string texPath;
        std::getline(iss, texPath);
        texPath = texPath.substr(texPath.find_first_not_of(" \t"));
        std::string filename = extractFilename(texPath);
        std::string fullPath = baseDir + "textures/" + filename;
        const TextureID texID =
            textureManager->load(fullPath, TextureType::Linear);
        currentMaterial->setNormalMap(texID);
        Debug::log(Debug::Category::MATERIALS,
                   "    - Loaded normal texture: ", fullPath);
      } else if (prefix == "map_d") {
        std::string texPath;
        std::getline(iss, texPath);
        texPath = texPath.substr(texPath.find_first_not_of(" \t"));
        std::string filename = extractFilename(texPath);
        std::string fullPath = baseDir + "textures/" + filename;
        textureManager->load(fullPath, TextureType::Linear);
        Debug::log(Debug::Category::MATERIALS,
                   "    - Found opacity texture: ", fullPath);
        currentMaterial->setIsTransparent(true);
      }
    }
  }

  finalizeMaterial(currentMaterial, currentMaterialName);

  file.close();

  if (!loadedMaterials.empty()) {
    mtlFilepathToID[mtlFilepath] = loadedMaterials[0];
    Debug::log(Debug::Category::MATERIALS, "RenderMaterialManager: Loaded ",
               loadedMaterials.size(),
               " materials from MTL, returning first material ID: ",
               loadedMaterials[0]);
    return loadedMaterials[0];
  }

  Debug::log(Debug::Category::MATERIALS,
             "RenderMaterialManager: No materials found in MTL, returning default");
  return defaultRenderMaterialID;
}

void RenderMaterialManager::createDefaultMaterial() {
  Debug::log(Debug::Category::MATERIALS,
             "RenderMaterialManager: Creating default material");

  RenderMaterial* const defaultMat = new RenderMaterial();
  defaultMat->setName("Default Material");

  RenderMaterialProperties props;
  defaultMat->getProperties(props);
  props.albedoColor = glm::vec4(0.7f, 0.7f, 0.7f, 1.0f);
  props.roughness = 0.5f;
  props.metallic = 0.0f;
  defaultMat->setProperties(props);

  defaultRenderMaterialID = registerMaterial(defaultMat);

  Debug::log(
      Debug::Category::MATERIALS,
      "RenderMaterialManager: Default material created with ID: ", defaultRenderMaterialID);
}