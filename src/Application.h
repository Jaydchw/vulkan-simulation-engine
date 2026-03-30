#pragma once
// Winsock2 must come before any Windows.h (e.g. pulled in by vulkan.h)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <vulkan/vulkan.h>

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "Rendering/MainPipeline.h"
#include "Rendering/PostProcessing.h"
#include "Rendering/PushConstants.h"
#include "Rendering/RenderDevice.h"
#include "Rendering/Window.h"
#include "Resources/RenderMaterialManager.h"
#include "Resources/MeshManager.h"
#include "ECS/Registry.h"
#include "Resources/TextureManager.h"
#include "Scene/LightManager.h"
#include "Animation/AnimationSystem.h"
#include "Physics/PhysicsSystem.h"
#include "Spawning/SpawnerSystem.h"
#include "Timeline/TimelineSystem.h"
#include "Network/NetworkManager.h"
#include "Util/Input.h"
#include "Util/Interface.h"
#include "Util/WorldParser.h"
#include "Physics/PhysicsMaterialManager.h"
#include "Util/FBSceneLoader.h"

#include <unordered_map>

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
  void setRegistry(Registry& reg);
  void loadWorld(const std::string& filepath);
  void loadFBScene(const std::string& filepath);
  void run();

  MeshManager* getMeshManager() const { return meshManager.get(); }
  RenderMaterialManager* getRenderMaterialManager() const { return materialManager.get(); }
  TextureManager* getTextureManager() const { return textureManager.get(); }
  LightManager* getLightManager() const { return lightManager.get(); }

  void switchToCamera(int index);
  void resetApplication();

 private:
  Registry* registry = nullptr;
  std::unique_ptr<Registry> ownedRegistry;
  std::unique_ptr<WorldParser>   worldParser;
  std::unique_ptr<FBSceneLoader> fbSceneLoader;
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

  enum class CameraMode { ORBIT, FPS };
  struct CameraControllerState {
    CameraMode mode = CameraMode::ORBIT;
    glm::vec3 orbitPivot{0.0f, 0.0f, 0.0f};
    glm::vec3 lastOrbitPosition{0.0f, 100.0f, 300.0f};
    float orbitRadius = 350.0f;
    float orbitTheta = 0.0f;
    float orbitPhi = 1.5f;
    glm::vec3 fpsPosition{0.0f, 100.0f, 300.0f};
    float fpsYaw = -90.0f;
    float fpsPitch = 0.0f;
    float fpsSpeed = 50.0f;
  };
  CameraControllerState camCtrl;
  Entity activeCamera = INVALID_ENTITY;
  int activeCameraIndex = 0;
  std::vector<Entity> scenecameras;

  VkExtent2D swapChainExtent{0, 0};

  std::unique_ptr<Window> window;
  std::unique_ptr<RenderDevice> renderDevice;
  std::unique_ptr<TextureManager> textureManager;
  std::unique_ptr<RenderMaterialManager> materialManager;
  std::unique_ptr<MeshManager> meshManager;
  std::unique_ptr<LightManager> lightManager;
  std::unique_ptr<PostProcessing> postProcessing;
  std::unique_ptr<MainPipeline> mainPipeline;
  std::unique_ptr<Interface> interface;
  std::unique_ptr<PhysicsMaterialManager> physicsMaterialManager;
  std::unique_ptr<AnimationSystem> animationSystem;
  std::unique_ptr<PhysicsSystem> physicsSystem;
  std::unique_ptr<SpawnerSystem> spawnerSystem;
  std::unique_ptr<TimelineSystem> timelineSystem;
  std::unique_ptr<NetworkManager> networkManager;

  // --- Owner-colour material system ---
  // Created once; IDs indexed [0]=peer1(red) [1]=peer2(green) [2]=peer3(blue) [3]=peer4(yellow)
  std::array<RenderMaterialID, 4> ownerRenderMaterialIDs = {
      INVALID_RENDER_MATERIAL_ID, INVALID_RENDER_MATERIAL_ID,
      INVALID_RENDER_MATERIAL_ID, INVALID_RENDER_MATERIAL_ID};
  std::unordered_map<Entity, RenderMaterialID> savedRenderMaterialIDs;
  bool lastColorByOwner = false;

  void initOwnerMaterials();
  void applyOwnerColors(bool enable);

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
  bool skipSceneRendering = false;
  std::chrono::high_resolution_clock::time_point bakeWallStart;
  double bakeUiFrameTimeAccum = 0.0;
  int bakeUiFrameCount = 0;

  SimulationState simState;
  SceneSettings sceneSettings;
  std::string lastLoadedWorldPath;

  bool applyingRemoteSceneLoad = false;

  bool    lastBroadcastPaused        = false;
  float   lastBroadcastTimeSpeed     = 1.0f;
  int32_t lastBroadcastHistoryIndex  = -1;
  bool    lastBroadcastReversePlay   = false;
  bool    lastBroadcastColorByOwner  = false;

  float networkSendAccumulator   = 0.0f;
  bool  ownershipHighLossMode    = false; // hysteresis flag for packet-loss isolation

  MeshID gizmoMeshID = INVALID_MESH_ID;
  RenderMaterialID gizmoRenderMaterialID = INVALID_RENDER_MATERIAL_ID;

  // Shadow area is recomputed every N frames; Y is never adjusted (no vertical drift).
  static constexpr int SHADOW_UPDATE_INTERVAL = 200;
  int   shadowUpdateFrameCounter  = 0;
  float cachedShadowSceneRadius   = 100.0f;
  glm::vec3 cachedShadowSceneCenter = glm::vec3(0.0f);

  // ── Simulation thread (pinned to Core 4+) ────────────────────────────────
  // Runs physicsSystem::update() independently of the render loop so that
  // simulation Hz and render Hz can be set to different values via ImGui.
  std::thread         simulationThread;
  std::atomic<bool>   simRunning{false};
  // Guards shared registry/simState access between the simulation thread
  // (writes physics state) and the render thread (reads for draw calls).
  std::mutex          simMutex;

  struct SimPerfAtomics {
    std::atomic<float> physSyncToMs{0.0f};
    std::atomic<float> physStepMs{0.0f};
    std::atomic<float> physSyncFromMs{0.0f};
    std::atomic<float> animationMs{0.0f};
    std::atomic<float> spawnerMs{0.0f};
    std::atomic<float> snapshotMs{0.0f};
    std::atomic<float> totalMs{0.0f};
  } simPerf;

  float perfGpuWaitMs         = 0.0f;
  float perfUniformBufferMs   = 0.0f;
  float perfCommandBufferMs   = 0.0f;
  float perfInterfaceRenderMs = 0.0f;

  void initWindow();
  void initVulkan();
  void mainLoop();
  void simulationThreadFunc();
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

  void initCamerasFromRegistry();
  void updateCameraController(float deltaTime);
  glm::mat4 getActiveCameraViewMatrix() const;
  glm::vec3 getActiveCameraPosition() const;

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