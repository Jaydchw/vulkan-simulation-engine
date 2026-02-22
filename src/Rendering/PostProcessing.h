#pragma once
#include <vulkan/vulkan.h>

#include <glm/glm.hpp>
#include <vector>

class RenderDevice;

struct PostProcessingConfig {
  float hue = 0.0f;
  float saturation = 1.0f;
  float contrast = 1.0f;
  float chromaticAberration = 0.003f;
  float vignetteStrength = 0.3f;
  float sharpenStrength = 0.3f;
  float exposure = 1.0f;
  float gamma = 1.0f;
  float filmGrain = 0.0f;
  float temperature = 0.0f;
  float pixelResolution = 4.0f;
  bool useToon = false;
  bool usePixel = false;
};

class PostProcessing final {
 public:
  PostProcessing(RenderDevice* renderDevice, VkDevice device,
                 VkFormat swapchainFormat);
  ~PostProcessing() noexcept;
  PostProcessing(const PostProcessing&) = delete;
  PostProcessing& operator=(const PostProcessing&) = delete;

  void init(VkDescriptorPool descriptorPool, uint32_t frameWidth,
            uint32_t frameHeight);
  void cleanup();
  void resize(uint32_t newWidth, uint32_t newHeight,
              VkDescriptorPool descriptorPool);

  void beginOffscreenPass(VkCommandBuffer commandBuffer,
                          const VkExtent2D& extent,
                          const glm::vec4& clearColor) const;
  void endOffscreenPass(VkCommandBuffer commandBuffer) const;
  void render(VkCommandBuffer commandBuffer, VkImageView targetImageView,
              const VkExtent2D& extent, uint32_t frameIndex) const;

  VkImageView getOffscreenImageView() const { return offscreenImageView; }

  void toggleToonMode() { config.useToon = !config.useToon; }
  void setToonMode(bool enabled) { config.useToon = enabled; }
  bool isToonModeEnabled() const { return config.useToon; }

  void togglePixelMode() { config.usePixel = !config.usePixel; }
  void setPixelMode(bool enabled) { config.usePixel = enabled; }
  bool isPixelModeEnabled() const { return config.usePixel; }

  PostProcessingConfig getConfig() const { return config; }
  void setConfig(const PostProcessingConfig& c) { config = c; }

 private:
  std::vector<VkDescriptorSet> descriptorSets;
  RenderDevice* renderDevice;
  VkDevice device;
  VkImage offscreenImage = VK_NULL_HANDLE;
  VkDeviceMemory offscreenImageMemory = VK_NULL_HANDLE;
  VkImageView offscreenImageView = VK_NULL_HANDLE;
  VkSampler offscreenSampler = VK_NULL_HANDLE;
  VkImage depthImage = VK_NULL_HANDLE;
  VkDeviceMemory depthImageMemory = VK_NULL_HANDLE;
  VkImageView depthImageView = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkPipeline toonPipeline = VK_NULL_HANDLE;
  VkPipeline pixelPipeline = VK_NULL_HANDLE;
  VkFormat swapchainFormat;
  VkFormat depthFormat;
  uint32_t width = 0;
  uint32_t height = 0;

  PostProcessingConfig config;

  void createOffscreenResources();
  void createDepthResources();
  void createDescriptorSetLayout();
  void createPipelines();
  void createDescriptorSets(VkDescriptorPool descriptorPool);
  void updateDescriptorSets();
  void cleanupOffscreenResources();
  void cleanupDepthResources();
  VkShaderModule createShaderModule(const std::vector<char>& code) const;
};