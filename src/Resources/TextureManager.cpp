#define STB_IMAGE_IMPLEMENTATION
#include "TextureManager.h"

#include <stdexcept>

#include "Util/Debug.h"
#include "../stb_image.h"

TextureManager::TextureManager(VkDevice dev, VkPhysicalDevice physDev,
                               VkCommandPool cmdPool, VkQueue gfxQueue)
    : device(dev),
      physicalDevice(physDev),
      commandPool(cmdPool),
      graphicsQueue(gfxQueue),
      defaultWhiteTexture(0),
      defaultNormalTexture(0),
      defaultBlackTexture(0) {
  Debug::log(Debug::Category::TEXTURE, "TextureManager: Constructor called");

  textures.push_back(TextureData{});

  const std::array<unsigned char, 4> whitePixel = {255, 255, 255, 255};
  defaultWhiteTexture =
      createDefaultTexture(whitePixel.data(), 1, 1, TextureType::sRGB);
  Debug::log(Debug::Category::TEXTURE,
             "TextureManager: Created default white texture (ID: ",
             defaultWhiteTexture, ")");

  const std::array<unsigned char, 4> normalPixel = {128, 128, 255, 255};
  defaultNormalTexture =
      createDefaultTexture(normalPixel.data(), 1, 1, TextureType::Linear);
  Debug::log(Debug::Category::TEXTURE,
             "TextureManager: Created default normal texture (ID: ",
             defaultNormalTexture, ")");

  const std::array<unsigned char, 4> blackPixel = {0, 0, 0, 255};
  defaultBlackTexture =
      createDefaultTexture(blackPixel.data(), 1, 1, TextureType::sRGB);
  Debug::log(Debug::Category::TEXTURE,
             "TextureManager: Created default black texture (ID: ",
             defaultBlackTexture, ")");

  Debug::log(Debug::Category::TEXTURE,
             "TextureManager: Initialization complete");
}

TextureManager::~TextureManager() noexcept {
  try {
    Debug::log(Debug::Category::TEXTURE, "TextureManager: Destructor called");
    cleanup();
  } catch (...) {
  }
}

TextureID TextureManager::load(const TextureCreateInfo& createInfo) {
  auto it = filepathToID.find(createInfo.filepath);
  if (it != filepathToID.end()) {
    Debug::log(Debug::Category::TEXTURE,
               "TextureManager: Texture already loaded: ", createInfo.filepath,
               " (ID: ", it->second, ")");
    return it->second;
  }

  Debug::log(Debug::Category::TEXTURE,
             "TextureManager: Loading new texture: ", createInfo.filepath);

  return createTexture(createInfo);
}

TextureID TextureManager::load(const std::string& filepath, TextureType type) {
  TextureCreateInfo createInfo;
  createInfo.filepath = filepath;
  createInfo.type = type;
  return load(createInfo);
}

VkImageView TextureManager::getImageView(TextureID id) const {
  if (id >= textures.size()) {
    Debug::log(Debug::Category::TEXTURE,
               "TextureManager: Invalid texture ID for getImageView: ", id,
               ", returning default white");
    return textures[defaultWhiteTexture].imageView;
  }
  return textures[id].imageView;
}

VkSampler TextureManager::getSampler(TextureID id) const {
  if (id >= textures.size()) {
    Debug::log(Debug::Category::TEXTURE,
               "TextureManager: Invalid texture ID for getSampler: ", id,
               ", returning default white");
    return textures[defaultWhiteTexture].sampler;
  }
  return textures[id].sampler;
}

void TextureManager::cleanup() {
  Debug::log(Debug::Category::TEXTURE, "TextureManager: Cleaning up ",
             textures.size(), " textures");

  for (auto& texture : textures) {
    if (texture.sampler != VK_NULL_HANDLE) {
      vkDestroySampler(device, texture.sampler, nullptr);
    }
    if (texture.imageView != VK_NULL_HANDLE) {
      vkDestroyImageView(device, texture.imageView, nullptr);
    }
    if (texture.image != VK_NULL_HANDLE) {
      vkDestroyImage(device, texture.image, nullptr);
    }
    if (texture.memory != VK_NULL_HANDLE) {
      vkFreeMemory(device, texture.memory, nullptr);
    }
  }
  textures.clear();

  Debug::log(Debug::Category::TEXTURE, "TextureManager: Cleanup complete");
}

void TextureManager::createStagingBuffer(
    VkDeviceSize size, VkBuffer& stagingBuffer,
    VkDeviceMemory& stagingBufferMemory) const {
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer);

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(device, stagingBuffer, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = findMemoryType(
      memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  vkAllocateMemory(device, &allocInfo, nullptr, &stagingBufferMemory);
  vkBindBufferMemory(device, stagingBuffer, stagingBufferMemory, 0);
}

TextureID TextureManager::createTexture(const TextureCreateInfo& createInfo) {
  Debug::log(
      Debug::Category::TEXTURE,
      "TextureManager: Creating texture from file: ", createInfo.filepath);

  int texWidth, texHeight, texChannels;
  stbi_uc* const pixels = stbi_load(createInfo.filepath.c_str(), &texWidth,
                                    &texHeight, &texChannels, STBI_rgb_alpha);

  if (!pixels) {
    Debug::log(Debug::Category::TEXTURE,
               "TextureManager: Failed to load texture file: ",
               createInfo.filepath, ", returning default white");
    return defaultWhiteTexture;
  }

  Debug::log(Debug::Category::TEXTURE, "  - Dimensions: ", texWidth, "x",
             texHeight, ", channels: ", texChannels);

  const VkDeviceSize imageSize = static_cast<VkDeviceSize>(texWidth) *
                                 static_cast<VkDeviceSize>(texHeight) * 4;
  const uint32_t mipLevels =
      createInfo.generateMipmaps
          ? static_cast<uint32_t>(
                std::floor(std::log2(std::max(texWidth, texHeight)))) +
                1
          : 1;

  Debug::log(Debug::Category::TEXTURE, "  - Mip levels: ", mipLevels);

  VkBuffer stagingBuffer;
  VkDeviceMemory stagingBufferMemory;

  createStagingBuffer(imageSize, stagingBuffer, stagingBufferMemory);

  void* data;
  vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
  memcpy(data, pixels, static_cast<size_t>(imageSize));
  vkUnmapMemory(device, stagingBufferMemory);

  stbi_image_free(pixels);

  const VkFormat format = (createInfo.type == TextureType::sRGB)
                              ? VK_FORMAT_R8G8B8A8_SRGB
                              : VK_FORMAT_R8G8B8A8_UNORM;

  TextureData textureData;
  createImage(texWidth, texHeight, mipLevels, format, VK_IMAGE_TILING_OPTIMAL,
              VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                  VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textureData.image,
              textureData.memory);

  transitionImageLayout(textureData.image, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels);
  copyBufferToImage(stagingBuffer, textureData.image, texWidth, texHeight);

  if (createInfo.generateMipmaps) {
    Debug::log(Debug::Category::TEXTURE, "  - Generating mipmaps");
    generateMipmaps(textureData.image, texWidth, texHeight, mipLevels);
  } else {
    transitionImageLayout(textureData.image,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevels);
  }

  vkDestroyBuffer(device, stagingBuffer, nullptr);
  vkFreeMemory(device, stagingBufferMemory, nullptr);

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = textureData.image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = mipLevels;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  if (vkCreateImageView(device, &viewInfo, nullptr, &textureData.imageView) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create texture image view!");
  }

  textureData.sampler =
      createSampler(createInfo.filter, createInfo.wrap, mipLevels);
  textureData.width = texWidth;
  textureData.height = texHeight;
  textureData.mipLevels = mipLevels;
  textureData.format = format;

  TextureID id = static_cast<TextureID>(textures.size());
  textures.push_back(std::move(textureData));
  filepathToID[createInfo.filepath] = id;

  Debug::log(Debug::Category::TEXTURE,
             "TextureManager: Successfully created texture (ID: ", id, ")");

  return id;
}

TextureID TextureManager::createDefaultTexture(const unsigned char* pixelData,
                                               uint32_t width, uint32_t height,
                                               TextureType type) {
  Debug::log(Debug::Category::TEXTURE,
             "TextureManager: Creating default texture ", width, "x", height);

  const VkDeviceSize imageSize =
      static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4;

  VkBuffer stagingBuffer;
  VkDeviceMemory stagingBufferMemory;

  createStagingBuffer(imageSize, stagingBuffer, stagingBufferMemory);

  void* data;
  vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
  memcpy(data, pixelData, static_cast<size_t>(imageSize));
  vkUnmapMemory(device, stagingBufferMemory);

  const VkFormat format = (type == TextureType::sRGB)
                              ? VK_FORMAT_R8G8B8A8_SRGB
                              : VK_FORMAT_R8G8B8A8_UNORM;

  TextureData textureData;
  createImage(width, height, 1, format, VK_IMAGE_TILING_OPTIMAL,
              VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textureData.image,
              textureData.memory);

  transitionImageLayout(textureData.image, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);
  copyBufferToImage(stagingBuffer, textureData.image, width, height);
  transitionImageLayout(textureData.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);

  vkDestroyBuffer(device, stagingBuffer, nullptr);
  vkFreeMemory(device, stagingBufferMemory, nullptr);

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = textureData.image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  vkCreateImageView(device, &viewInfo, nullptr, &textureData.imageView);

  textureData.sampler =
      createSampler(TextureFilter::Linear, TextureWrap::Repeat, 1);
  textureData.width = width;
  textureData.height = height;
  textureData.mipLevels = 1;
  textureData.format = format;

  TextureID id = static_cast<TextureID>(textures.size());
  textures.push_back(std::move(textureData));

  Debug::log(Debug::Category::TEXTURE,
             "TextureManager: Default texture created (ID: ", id, ")");

  return id;
}

void TextureManager::createImage(uint32_t width, uint32_t height,
                                 uint32_t mipLevels, VkFormat format,
                                 VkImageTiling tiling, VkImageUsageFlags usage,
                                 VkMemoryPropertyFlags properties,
                                 VkImage& image,
                                 VkDeviceMemory& imageMemory) const {
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = width;
  imageInfo.extent.height = height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = mipLevels;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = tiling;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create image!");
  }

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(device, image, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex =
      findMemoryType(memRequirements.memoryTypeBits, properties);

  if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate image memory!");
  }

  vkBindImageMemory(device, image, imageMemory, 0);
}

uint32_t TextureManager::findMemoryType(
    uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags &
                                    properties) == properties) {
      return i;
    }
  }

  throw std::runtime_error("Failed to find suitable memory type!");
}

void TextureManager::transitionImageLayout(VkImage image,
                                           VkImageLayout oldLayout,
                                           VkImageLayout newLayout,
                                           uint32_t mipLevels) const {
  VkCommandBuffer const commandBuffer = beginSingleTimeCommands();

  VkImageMemoryBarrier2 barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = mipLevels;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
      newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    barrier.srcAccessMask = 0;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
  } else {
    throw std::invalid_argument("Unsupported layout transition!");
  }

  VkDependencyInfo dependencyInfo{};
  dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dependencyInfo.imageMemoryBarrierCount = 1;
  dependencyInfo.pImageMemoryBarriers = &barrier;

  vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

  endSingleTimeCommands(commandBuffer);
}

void TextureManager::copyBufferToImage(VkBuffer buffer, VkImage image,
                                       uint32_t width, uint32_t height) const {
  VkCommandBuffer const commandBuffer = beginSingleTimeCommands();

  VkBufferImageCopy region{};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageOffset = {0, 0, 0};
  region.imageExtent = {width, height, 1};

  vkCmdCopyBufferToImage(commandBuffer, buffer, image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  endSingleTimeCommands(commandBuffer);
}

void TextureManager::generateMipmaps(VkImage image, uint32_t width,
                                     uint32_t height,
                                     uint32_t mipLevels) const {
  VkCommandBuffer const commandBuffer = beginSingleTimeCommands();

  int32_t mipWidth = width;
  int32_t mipHeight = height;

  for (uint32_t i = 1; i < mipLevels; i++) {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    barrier.subresourceRange.baseMipLevel = i - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

    VkImageBlit blit{};
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.mipLevel = i - 1;
    blit.srcSubresource.baseArrayLayer = 0;
    blit.srcSubresource.layerCount = 1;
    blit.dstOffsets[0] = {0, 0, 0};
    blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1,
                          mipHeight > 1 ? mipHeight / 2 : 1, 1};
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.mipLevel = i;
    blit.dstSubresource.baseArrayLayer = 0;
    blit.dstSubresource.layerCount = 1;

    vkCmdBlitImage(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                   VK_FILTER_LINEAR);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;

    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

    if (mipWidth > 1) mipWidth /= 2;
    if (mipHeight > 1) mipHeight /= 2;
  }

  VkImageMemoryBarrier2 barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  barrier.image = image;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = mipLevels - 1;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;
  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
  barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
  barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
  barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;

  VkDependencyInfo dependencyInfo{};
  dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dependencyInfo.imageMemoryBarrierCount = 1;
  dependencyInfo.pImageMemoryBarriers = &barrier;

  vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

  endSingleTimeCommands(commandBuffer);
}

VkSampler TextureManager::createSampler(TextureFilter filter, TextureWrap wrap,
                                        uint32_t mipLevels) const {
  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter =
      (filter == TextureFilter::Linear) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
  samplerInfo.minFilter =
      (filter == TextureFilter::Linear) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;

  VkSamplerAddressMode addressMode;
  switch (wrap) {
    case TextureWrap::Repeat:
      addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
      break;
    case TextureWrap::ClampToEdge:
      addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      break;
    case TextureWrap::MirroredRepeat:
      addressMode = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
      break;
    default:
      addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  }

  samplerInfo.addressModeU = addressMode;
  samplerInfo.addressModeV = addressMode;
  samplerInfo.addressModeW = addressMode;
  samplerInfo.anisotropyEnable = VK_TRUE;
  samplerInfo.maxAnisotropy = 16.0f;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.minLod = 0.0f;
  samplerInfo.maxLod = static_cast<float>(mipLevels);
  samplerInfo.mipLodBias = 0.0f;

  VkSampler sampler;
  if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create texture sampler!");
  }

  return sampler;
}

void TextureManager::recreateSamplers(VkFilter magFilter, VkFilter minFilter) {
  Debug::log(Debug::Category::TEXTURE,
             "TextureManager: Recreating all samplers with new filter mode");

  vkDeviceWaitIdle(device);

  for (auto& texture : textures) {
    if (texture.sampler != VK_NULL_HANDLE) {
      vkDestroySampler(device, texture.sampler, nullptr);

      VkSamplerCreateInfo samplerInfo{};
      samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
      samplerInfo.magFilter = magFilter;
      samplerInfo.minFilter = minFilter;
      samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
      samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
      samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
      samplerInfo.anisotropyEnable = VK_TRUE;
      samplerInfo.maxAnisotropy = 16.0f;
      samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
      samplerInfo.unnormalizedCoordinates = VK_FALSE;
      samplerInfo.compareEnable = VK_FALSE;
      samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
      samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
      samplerInfo.minLod = 0.0f;
      samplerInfo.maxLod = static_cast<float>(texture.mipLevels);
      samplerInfo.mipLodBias = 0.0f;

      if (vkCreateSampler(device, &samplerInfo, nullptr, &texture.sampler) !=
          VK_SUCCESS) {
        throw std::runtime_error("Failed to recreate texture sampler!");
      }
    }
  }

  Debug::log(Debug::Category::TEXTURE,
             "TextureManager: Successfully recreated ", textures.size(),
             " samplers");
}

VkCommandBuffer TextureManager::beginSingleTimeCommands() const {
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool = commandPool;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(commandBuffer, &beginInfo);

  return commandBuffer;
}

void TextureManager::endSingleTimeCommands(
    VkCommandBuffer commandBuffer) const {
  vkEndCommandBuffer(commandBuffer);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(graphicsQueue);

  vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}