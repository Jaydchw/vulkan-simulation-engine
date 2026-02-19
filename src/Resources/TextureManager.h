#pragma once
#include <array>
#include <unordered_map>
#include <vector>

#include "Texture.h"

class TextureManager final {
 public:
  TextureManager(VkDevice dev, VkPhysicalDevice physDev, VkCommandPool cmdPool,
                 VkQueue gfxQueue);
  ~TextureManager() noexcept;

  TextureManager(const TextureManager&) = delete;
  TextureManager& operator=(const TextureManager&) = delete;

  TextureID load(const TextureCreateInfo& createInfo);
  TextureID load(const std::string& filepath,
                 TextureType type = TextureType::sRGB);

  VkImageView getImageView(TextureID id) const;
  VkSampler getSampler(TextureID id) const;

  TextureID getDefaultWhite() const { return defaultWhiteTexture; }
  TextureID getDefaultNormal() const { return defaultNormalTexture; }
  TextureID getDefaultBlack() const { return defaultBlackTexture; }

  void recreateSamplers(VkFilter magFilter, VkFilter minFilter);
  void cleanup();

 private:
  struct TextureData {
    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipLevels = 1;

    TextureData() = default;
    TextureData(const TextureData&) = delete;
    TextureData& operator=(const TextureData&) = delete;
    TextureData(TextureData&&) = default;
    TextureData& operator=(TextureData&&) = default;
  };

  std::vector<TextureData> textures;
  std::unordered_map<std::string, TextureID> filepathToID;

  VkDevice device;
  VkPhysicalDevice physicalDevice;
  VkCommandPool commandPool;
  VkQueue graphicsQueue;

  TextureID defaultWhiteTexture;
  TextureID defaultNormalTexture;
  TextureID defaultBlackTexture;

  TextureID createTexture(const TextureCreateInfo& createInfo);
  TextureID createDefaultTexture(const unsigned char* pixelData, uint32_t width,
                                 uint32_t height, TextureType type);

  void createImage(uint32_t width, uint32_t height, uint32_t mipLevels,
                   VkFormat format, VkImageTiling tiling,
                   VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
                   VkImage& image, VkDeviceMemory& imageMemory) const;

  void transitionImageLayout(VkImage image, VkImageLayout oldLayout,
                             VkImageLayout newLayout, uint32_t mipLevels) const;

  void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width,
                         uint32_t height) const;

  void generateMipmaps(VkImage image, uint32_t width, uint32_t height,
                       uint32_t mipLevels) const;

  VkSampler createSampler(TextureFilter filter, TextureWrap wrap,
                          uint32_t mipLevels) const;

  uint32_t findMemoryType(uint32_t typeFilter,
                          VkMemoryPropertyFlags properties) const;

  VkCommandBuffer beginSingleTimeCommands() const;
  void endSingleTimeCommands(VkCommandBuffer commandBuffer) const;

  void createStagingBuffer(VkDeviceSize size, VkBuffer& stagingBuffer,
                           VkDeviceMemory& stagingBufferMemory) const;
};