#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../ECS/Entity.h"
#include "../ECS/Registry.h"
#include "../Physics/PhysicsSystem.h"
#include "../Network/NetworkManager.h"

class MainPipeline;
class PostProcessing;

struct BakeCollisionPairStats {
  std::string pairName;
  long long totalChecks = 0;
  long long totalResolved = 0;
};

struct BakeStats {
  double totalWallTimeMs = 0.0;
  double avgStepMs = 0.0;
  double minStepMs = 0.0;
  double maxStepMs = 0.0;
  double avgSyncToMs = 0.0;
  double avgPhysStepMs = 0.0;
  double avgSyncFromMs = 0.0;
  double avgSnapshotMs = 0.0;
  double avgUiFrameMs = 0.0;

  double stepsPerSecond = 0.0;
  double simSecondsPerWallSecond = 0.0;

  int objectCount = 0;
  int totalSteps = 0;
  float simDuration = 0.0f;
  float stepSize = 0.0f;

  std::vector<BakeCollisionPairStats> collisionPairs;

  bool hasData = false;
};

struct SimulationState {
  bool isPaused = false;
  bool stepFrame = false;
  float timeSpeed = 1.0f;
  float currentTime = 0.0f;
  float stepSize = 0.016f;
  std::vector<float> timeHistory;
  int historyIndex = -1;
  bool rewinding = false;
  bool reversePlay = false;
  bool snapshotScrubbed = false;
  bool resetRequested = false;

  bool bakeRequested = false;
  float bakeDuration = 10.0f;
  bool baked = false;
  bool bakePerformanceMode = false;

  bool isBaking = false;
  int bakeTotalSteps = 0;
  int bakeCurrentStep = 0;
  float bakeTimeSpeedSave = 1.0f;

  BakeStats bakeStats;

  bool reloadRequested = false;
  bool loadBakeRequested = false;
  float scrubAccumulator = 0.0f;
  float physicsAccumulator = 0.0f;
  int maxFps = 0;
};

struct SceneSettings {
  glm::vec4 clearColor = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
};

enum class UIScalePreset { Small, Normal, Large, XL };

struct GeneralSettings {
  UIScalePreset scalePreset = UIScalePreset::Normal;
  bool showFPS = true;
  bool showLightGizmos = false;
  bool snapshotsEnabled = true;
};

class Interface {
 public:
  Interface(GLFWwindow* window, VkInstance instance,
            VkPhysicalDevice physicalDevice, VkDevice device, VkQueue queue,
            VkCommandPool commandPool, uint32_t queueFamily,
            VkFormat swapChainFormat, VkFormat depthFormat);
  ~Interface();

  void init();
  void resize(VkExtent2D extent, const std::vector<VkImageView>& imageViews);
  void cleanup();

  void render(SimulationState& simState, SceneSettings& sceneSettings,
              Registry& registry, MainPipeline* mainPipeline,
              PostProcessing* postProcessing,
              NetworkManager* networkManager = nullptr);

  void draw(VkCommandBuffer commandBuffer, uint32_t imageIndex);

  Entity getSelectedEntity() const { return selectedEntity; }
  Entity getHoveredEntity() const { return hoveredEntity; }
  bool getShowLightGizmos() const { return generalSettings.showLightGizmos; }
  bool getSnapshotsEnabled() const { return generalSettings.snapshotsEnabled; }
  void clearSelection() { selectedEntity = INVALID_ENTITY; hoveredEntity = INVALID_ENTITY; }

  void setWorldLoadCallback(std::function<void(const std::string&)> callback);
  void setWorldDirectory(const std::string& dir);
  void setFBSceneDirectory(const std::string& dir);
  void refreshWorldList();
  void setCurrentWorldPath(const std::string& path);
  void notifyBakeSaved();

  void setActiveCameraIndex(int idx) { activeCameraIdx = idx; }
  bool pollCameraSwitch(int& outIdx) {
    if (!cameraSwitchPending) return false;
    outIdx = cameraSwitchTarget;
    cameraSwitchPending = false;
    return true;
  }

 private:
  GLFWwindow* window;
  VkInstance instance;
  VkPhysicalDevice physicalDevice;
  VkDevice device;
  VkQueue queue;
  VkCommandPool commandPool;
  uint32_t queueFamily;
  VkFormat swapChainFormat;
  VkFormat depthFormat;
  VkDescriptorPool descriptorPool;

  VkRenderPass imGuiRenderPass;
  std::vector<VkFramebuffer> framebuffers;
  VkExtent2D currentExtent;

  GeneralSettings generalSettings;
  float currentScale = 1.0f;

  Entity selectedEntity = INVALID_ENTITY;
  Entity hoveredEntity = INVALID_ENTITY;

  std::string worldDirectory;
  std::string fbSceneDirectory;
  std::string lastLoadedWorld;
  std::vector<std::string> worldFiles;
  std::vector<std::string> fbSceneFiles;
  std::function<void(const std::string&)> worldLoadCallback;

  struct WorldFileStats {
    int objects = 0;
    int lights = 0;
    int textures = 0;
    int materials = 0;
  };
  std::unordered_map<std::string, WorldFileStats> worldFileStats;

  bool showSpeedPopup = false;
  bool showBakePopup = false;
  bool showBakeStatsWindow = false;
  bool scaleLinked = true;
  bool hasBakeFile = false;
  float speedPopupHeight = 0.0f;
  float bakePopupHeight  = 0.0f;

  void createDescriptorPool();
  void createImGuiRenderPass();
  void applyScalePreset();
  void renderWorldsMenu();
  void renderTransportBar(SimulationState& simState, ImVec4 peerTint = {0,0,0,0});
  void renderObjectsMenu(Registry& registry);
  void renderSceneMenu(SceneSettings& sceneSettings,
                       MainPipeline* mainPipeline,
                       SimulationState& simState);
  void renderPostProcessingMenu(PostProcessing* postProcessing);
  void renderSettingsMenu(SimulationState& simState);
  void renderNetworkMenu(NetworkManager* networkManager, SimulationState& simState);
  void renderCamerasMenu(Registry& registry);

  int activeCameraIdx = 0;
  bool cameraSwitchPending = false;
  int cameraSwitchTarget = 0;
};