#pragma once
#include <vulkan/vulkan.h>

#include <vector>

class RenderDevice;

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
                          VkImageView depthImageViewParam,
                          const VkExtent2D& extent) const;
  void endOffscreenPass(VkCommandBuffer commandBuffer) const;
  void render(VkCommandBuffer commandBuffer, VkImageView targetImageView,
              const VkExtent2D& extent, uint32_t frameIndex) const;

  VkImageView getOffscreenImageView() const { return offscreenImageView; }

  void toggleToonMode() { useToonShader = !useToonShader; }
  bool isToonModeEnabled() const { return useToonShader; }

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

  VkFormat swapchainFormat;
  VkFormat depthFormat;

  uint32_t width = 0;
  uint32_t height = 0;

  bool useToonShader = false;

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