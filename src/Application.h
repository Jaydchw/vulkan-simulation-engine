#pragma once
#include <vulkan/vulkan.h>

#include <array>
#include <memory>
#include <vector>

#include "Rendering/MainPipeline.h"
#include "Rendering/PostProcessing.h"
#include "Rendering/PushConstants.h"
#include "Rendering/RenderDevice.h"
#include "Rendering/Window.h"
#include "Resources/MaterialManager.h"
#include "Resources/MeshManager.h"
#include "Resources/Object.h"
#include "Resources/TextureManager.h"
#include "Scene/LightManager.h"
#include "Util/Camera.h"
#include "Util/Input.h"

constexpr uint32_t WIDTH = 1280;
constexpr uint32_t HEIGHT = 720;
constexpr int MAX_FRAMES_IN_FLIGHT = 2;

struct UniformBufferObject {
  alignas(16) glm::mat4 view;
  alignas(16) glm::mat4 proj;
  alignas(16) std::array<glm::mat4, MAX_SHADOW_CASTERS> lightSpaceMatrices;
  alignas(16) glm::vec3 eyePos;
  alignas(4) float time;
};

struct PipelineConfigInfo {
  VkPipelineVertexInputStateCreateInfo vertexInputInfo;
  VkPipelineInputAssemblyStateCreateInfo inputAssembly;
  VkPipelineViewportStateCreateInfo viewportState;
  VkPipelineRasterizationStateCreateInfo rasterizer;
  VkPipelineMultisampleStateCreateInfo multisampling;
  VkPipelineDepthStencilStateCreateInfo depthStencil;
  VkPipelineColorBlendStateCreateInfo colorBlending;
  VkPipelineDynamicStateCreateInfo dynamicState;
  std::vector<VkDynamicState> dynamicStates;
  VkPipelineColorBlendAttachmentState colorBlendAttachment;
};

class Application final {
 public:
  Application();
  ~Application();

  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;

  void init();
  void setScene(const std::vector<Object>& objects);
  void run();

  MeshManager* getMeshManager() const { return meshManager.get(); }
  MaterialManager* getMaterialManager() const { return materialManager.get(); }
  LightManager* getLightManager() const { return lightManager.get(); }

  void setCameraPreset(int presetIndex);
  void resetApplication();

 private:
  std::vector<Object> sceneObjects;
  std::vector<VkImage> swapChainImages;
  std::vector<VkImageView> swapChainImageViews;
  std::vector<VkBuffer> uniformBuffers;
  std::vector<VkDeviceMemory> uniformBuffersMemory;
  std::vector<void*> uniformBuffersMapped;
  std::vector<VkDescriptorSet> descriptorSets;
  std::vector<VkCommandBuffer> commandBuffers;
  std::vector<VkSemaphore> imageAvailableSemaphores;
  std::vector<VkSemaphore> renderFinishedSemaphores;
  std::vector<VkFence> inFlightFences;

  Input input;
  Camera camera;
  VkExtent2D swapChainExtent{0, 0};

  std::unique_ptr<Window> window;
  std::unique_ptr<RenderDevice> renderDevice;
  std::unique_ptr<TextureManager> textureManager;
  std::unique_ptr<MaterialManager> materialManager;
  std::unique_ptr<MeshManager> meshManager;
  std::unique_ptr<LightManager> lightManager;
  std::unique_ptr<PostProcessing> postProcessing;
  std::unique_ptr<MainPipeline> mainPipeline;

  VkInstance instance = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue graphicsQueue = VK_NULL_HANDLE;
  VkQueue presentQueue = VK_NULL_HANDLE;
  VkCommandPool commandPool = VK_NULL_HANDLE;
  VkSwapchainKHR swapChain = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout materialDescriptorSetLayout = VK_NULL_HANDLE;
  VkBuffer vertexBuffer = VK_NULL_HANDLE;
  VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;
  VkBuffer indexBuffer = VK_NULL_HANDLE;
  VkDeviceMemory indexBufferMemory = VK_NULL_HANDLE;
  VkImage depthImage = VK_NULL_HANDLE;
  VkDeviceMemory depthImageMemory = VK_NULL_HANDLE;
  VkImageView depthImageView = VK_NULL_HANDLE;
  VkPipeline shadowPipeline = VK_NULL_HANDLE;
  VkPipelineLayout shadowPipelineLayout = VK_NULL_HANDLE;

  float lastFrameTime = 0.0f;

  VkFormat swapChainImageFormat = VK_FORMAT_UNDEFINED;
  VkFormat depthFormat = VK_FORMAT_UNDEFINED;

  uint32_t currentFrame = 0;
  bool framebufferResized = false;

  void initWindow();
  void initVulkan();
  void mainLoop();
  void cleanup();

  void recreateGraphicsPipeline();
  void createShadowPipeline();
  void createUniformBuffers();
  void drawFrame();
  void recreateSwapChain();
  void cleanupSwapChain();
  void createDepthResources();
  void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);
  void updateUniformBuffer(uint32_t currentImage);

  void recreateTextureSamplers(VkFilter magFilter, VkFilter minFilter);
  void toggleShadingMode();

  static void framebufferResizeCallback(GLFWwindow* window, int width,
                                        int height);
  static void keyCallback(GLFWwindow* window, int key, int scancode, int action,
                          int mods);
  static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
  static void mouseButtonCallback(GLFWwindow* window, int button, int action,
                                  int mods);
  static void scrollCallback(GLFWwindow* window, double xoffset,
                             double yoffset);

  void createDefaultPipelineConfig(PipelineConfigInfo& configInfo) const;
  void setupRenderingCreateInfo(
      VkPipelineRenderingCreateInfo& createInfo) const;
  void setupViewportScissor(VkCommandBuffer commandBuffer, float width,
                            float height) const;
  void getShaderStages(
      const std::string& vertPath, const std::string& fragPath,
      VkShaderModule& vertModule, VkShaderModule& fragModule,
      std::array<VkPipelineShaderStageCreateInfo, 2>& stages) const;
};