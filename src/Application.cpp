#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "Application.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

#include <glm/gtc/constants.hpp>

#include "Util/Debug.h"
#include "Util/FrustumCuller.h"
#include "Util/RenderUtils.h"
#include "Util/ThreadAffinity.h"
#include "Physics/PhysicsSystem.h"
#include "Spawning/SpawnerSystem.h"
#include "Timeline/TimelineSystem.h"
#include "Timeline/WorldBakeSerializer.h"
#include "Network/NetworkManager.h"
#include "Vulkan/VulkanCommandBuffer.h"
#include "Vulkan/VulkanDepthBuffer.h"
#include "Vulkan/VulkanDescriptors.h"
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanInstance.h"
#include "Vulkan/VulkanSwapchain.h"
#include "Vulkan/VulkanSyncObjects.h"

Application::Application() {
  Debug::log(Debug::Category::MAIN, "Application: Constructor called");
}

Application::~Application() {
  try {
    Debug::log(Debug::Category::MAIN, "Application: Destructor called");
  } catch (...) {
  }
}

void Application::init() {
  initWindow();
  initVulkan();
}

void Application::setRegistry(Registry& reg) {
  registry = &reg;
  lightManager->setRegistry(registry);
  lightManager->syncLights();
  animationSystem->setRegistry(registry);
  physicsSystem->setRegistry(registry);
  spawnerSystem->setRegistry(registry);
  timelineSystem->setRegistry(registry);
  timelineSystem->saveInitialSnapshot();
}

void Application::loadWorld(const std::string& filepath) {
  vkDeviceWaitIdle(device);

  if (interface) interface->clearSelection();

  {
    std::lock_guard<std::mutex> lock(simMutex);

    materialManager->resetForNewScene();
    lightManager->resetForNewScene();

    ownedRegistry = std::make_unique<Registry>();
    registry = ownedRegistry.get();

    WorldSettings worldSettings;
    worldParser->load(filepath, *registry, worldSettings);

    sceneSettings.clearColor = worldSettings.clearColor;

    lightManager->setRegistry(registry);
    lightManager->syncLights();

    animationSystem->setRegistry(registry);
    physicsSystem->setRegistry(registry);
    spawnerSystem->setRegistry(registry);
    timelineSystem->setRegistry(registry);
    timelineSystem->saveInitialSnapshot();

    if (networkManager) {
      networkManager->init(registry);
      networkManager->assignObjectOwnership();
    }

    savedRenderMaterialIDs.clear();
    ownerRenderMaterialIDs.fill(INVALID_RENDER_MATERIAL_ID);
    lastColorByOwner = false;

    simState = SimulationState{};
    simState.timeSpeed = worldSettings.timeSpeed;
    simState.physicsAccumulator = 0.0f;
    if (worldSettings.simulationHz > 0)
      simState.stepSize = 1.0f / static_cast<float>(worldSettings.simulationHz);
    simState.maxFps = worldSettings.maxFps;
  }

  lastLoadedWorldPath = filepath;

  if (interface) interface->setCurrentWorldPath(filepath);

  initCamerasFromRegistry();

  Debug::log(Debug::Category::MAIN, "Application: Loaded world: ", filepath);
}

void Application::loadFBScene(const std::string& filepath) {
  vkDeviceWaitIdle(device);

  if (interface) interface->clearSelection();

  {
    std::lock_guard<std::mutex> lock(simMutex);

    materialManager->resetForNewScene();
    lightManager->resetForNewScene();

    ownedRegistry = std::make_unique<Registry>();
    registry = ownedRegistry.get();

    FBWorldSettings fbSettings;
    fbSceneLoader->load(filepath, *registry, fbSettings);

    lightManager->setRegistry(registry);
    lightManager->syncLights();

    animationSystem->setRegistry(registry);
    physicsSystem->setRegistry(registry);
    spawnerSystem->setRegistry(registry);
    timelineSystem->setRegistry(registry);
    timelineSystem->saveInitialSnapshot();

    if (networkManager) {
      networkManager->init(registry);
      networkManager->assignObjectOwnership();
      // Item 1: apply explicit peer ownership from SimulatedObject::owner fields,
      // overriding the auto round-robin assignment above.
      for (const auto& [entityId, peerID] : fbSettings.entityOwners)
        networkManager->setEntityOwner(static_cast<Entity>(entityId), peerID);
    }

    savedRenderMaterialIDs.clear();
    ownerRenderMaterialIDs.fill(INVALID_RENDER_MATERIAL_ID);
    lastColorByOwner = false;

    simState = SimulationState{};
    simState.physicsAccumulator = 0.0f;
  }

  lastLoadedWorldPath = filepath;

  if (interface) interface->setCurrentWorldPath(filepath);

  initCamerasFromRegistry();

  Debug::log(Debug::Category::MAIN, "Application: Loaded FB scene: ", filepath);
}

void Application::run() {
  // ── Process affinity: pin this (render/UI) thread to Core 1 ──────────────
  ThreadAffinity::setCurrentThread(ThreadAffinity::VISUALISATION_MASK, "Visualisation");
  ThreadAffinity::logAvailableCores();

  // ── Launch the simulation thread (pinned to Core 4+ inside the func) ─────
  simRunning = true;
  simulationThread = std::thread(&Application::simulationThreadFunc, this);

  mainLoop();

  // ── Tear down simulation thread ───────────────────────────────────────────
  simRunning = false;
  if (simulationThread.joinable()) simulationThread.join();

  cleanup();
}

// ─────────────────────────────────────────────────────────────────────────────
// Simulation thread  (Core 4+)
//
// Runs the live physics simulation independently of the render loop, allowing
// simulation Hz and render Hz to be controlled separately via ImGui.
//
// Ownership of simState.physicsAccumulator, physicsSystem, spawnerSystem,
// timelineSystem, and the network-send accumulator lives here.
//
// simMutex is held while any registry-writing work is done so that the render
// thread (which reads transforms during recordCommandBuffer) cannot observe a
// partially-updated state.
// ─────────────────────────────────────────────────────────────────────────────
void Application::simulationThreadFunc() {
  ThreadAffinity::setCurrentThread(ThreadAffinity::SIMULATION_MASK, "Simulation");

  using Clock = std::chrono::steady_clock;
  auto lastTime = Clock::now();

  while (simRunning) {
    auto stepStart = Clock::now();
    float dt = std::chrono::duration<float>(stepStart - lastTime).count();
    lastTime = stepStart;
    // Clamp to avoid a "spiral of death" after pauses or debug breaks
    dt = std::min(dt, 0.1f);

    bool didWork = false;
    {
      std::lock_guard<std::mutex> lock(simMutex);

      // ── Skip: let the main loop handle baking, reverse-play, and baked scrub
      const bool liveMode = !simState.isBaking
                         && !simState.reversePlay
                         && !simState.baked
                         && registry != nullptr;

      if (liveMode && (!simState.isPaused || simState.stepFrame)) {
        bool snapshotsOn = interface ? interface->getSnapshotsEnabled() : false;
        const bool doPerfTiming = interface && interface->isPerfProfilingEnabled();

        if (snapshotsOn && !timelineSystem->hasInitialSnapshot())
          timelineSystem->saveInitialSnapshot();

        // If the user scrubbed back into history then resumed, truncate the
        // future portion of the timeline before adding new snapshots.
        if (snapshotsOn && simState.historyIndex >= 0) {
          int truncIdx = simState.historyIndex;
          timelineSystem->truncateAfter(truncIdx);
          if (truncIdx + 1 < static_cast<int>(simState.timeHistory.size()))
            simState.timeHistory.erase(
                simState.timeHistory.begin() + truncIdx + 1,
                simState.timeHistory.end());
          simState.historyIndex = -1;
          simState.baked        = false;
        }

        simState.rewinding   = false;
        simState.reversePlay = false;

        auto msec = [](Clock::time_point a, Clock::time_point b) -> float {
          return static_cast<float>(
              std::chrono::duration<double, std::milli>(b - a).count());
        };

        const auto tSimStart = doPerfTiming ? Clock::now() : Clock::time_point{};

        {
          const auto t0 = doPerfTiming ? Clock::now() : Clock::time_point{};
          if (animationSystem)
            animationSystem->update(dt * simState.timeSpeed);
          if (doPerfTiming) simPerf.animationMs.store(msec(t0, Clock::now()));
        }

        {
          const auto t0 = doPerfTiming ? Clock::now() : Clock::time_point{};
          if (spawnerSystem)
            spawnerSystem->update(dt * simState.timeSpeed);
          if (doPerfTiming) simPerf.spawnerMs.store(msec(t0, Clock::now()));
        }

        if (simState.stepFrame) {
          // Single-step advance (triggered by the ImGui "Step" button)
          simState.stepFrame = false;

          if (doPerfTiming) {
            PhysicsStepTimings pt = physicsSystem->timedUpdate(simState.stepSize);
            simPerf.physSyncToMs.store(static_cast<float>(pt.syncToMs));
            simPerf.physStepMs.store(static_cast<float>(pt.physStepMs));
            simPerf.physSyncFromMs.store(static_cast<float>(pt.syncFromMs));
          } else {
            physicsSystem->update(simState.stepSize);
          }
          simState.currentTime += simState.stepSize;

          if (snapshotsOn) {
            const auto t0 = doPerfTiming ? Clock::now() : Clock::time_point{};
            timelineSystem->saveSnapshot();
            if (doPerfTiming) simPerf.snapshotMs.store(msec(t0, Clock::now()));
            simState.timeHistory.push_back(simState.currentTime);
            while (static_cast<int>(simState.timeHistory.size()) >
                   timelineSystem->getSnapshotCount())
              simState.timeHistory.erase(simState.timeHistory.begin());
          }
        } else {
          // Fixed-timestep accumulator — keeps physics deterministic regardless
          // of how fast the render loop or this thread happen to run.
          double accumPhysMs = 0.0, accumSyncToMs = 0.0, accumPhysStepMs = 0.0;
          double accumSyncFromMs = 0.0, accumSnapMs = 0.0;
          int physStepCount = 0;

          simState.physicsAccumulator += dt * simState.timeSpeed;
          while (simState.physicsAccumulator >= simState.stepSize) {
            if (doPerfTiming) {
              PhysicsStepTimings pt = physicsSystem->timedUpdate(simState.stepSize);
              accumSyncToMs   += pt.syncToMs;
              accumPhysStepMs += pt.physStepMs;
              accumSyncFromMs += pt.syncFromMs;
              ++physStepCount;
            } else {
              physicsSystem->update(simState.stepSize);
            }
            simState.physicsAccumulator -= simState.stepSize;
            simState.currentTime        += simState.stepSize;

            if (snapshotsOn) {
              const auto t0 = doPerfTiming ? Clock::now() : Clock::time_point{};
              timelineSystem->saveSnapshot();
              if (doPerfTiming)
                accumSnapMs += std::chrono::duration<double, std::milli>(
                    Clock::now() - t0).count();
              simState.timeHistory.push_back(simState.currentTime);
              while (static_cast<int>(simState.timeHistory.size()) >
                     timelineSystem->getSnapshotCount())
                simState.timeHistory.erase(simState.timeHistory.begin());
            }
          }

          if (doPerfTiming && physStepCount > 0) {
            simPerf.physSyncToMs.store(static_cast<float>(accumSyncToMs));
            simPerf.physStepMs.store(static_cast<float>(accumPhysStepMs));
            simPerf.physSyncFromMs.store(static_cast<float>(accumSyncFromMs));
            simPerf.snapshotMs.store(static_cast<float>(accumSnapMs));
          }
        }

        if (doPerfTiming)
          simPerf.totalMs.store(msec(tSimStart, Clock::now()));

        // Network send: queue owned-object states for the network thread to
        // transmit.  tickSend() is internally thread-safe (uses deferredMutex).
        if (networkManager) {
          networkSendAccumulator += dt;
          const float sendInterval = 1.0f / networkManager->networkSendHz;
          if (networkSendAccumulator >= sendInterval) {
            networkSendAccumulator -= sendInterval;
            networkManager->tickSend();
          }
        }

        didWork = true;
      }
    } // unlock simMutex

    // If no physics work was done this iteration, back off so we don't burn a
    // core spinning while paused or during baking.
    if (!didWork)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    else
      std::this_thread::yield(); // let the render thread in between steps
  }
}

void Application::initWindow() {
  window = std::make_unique<Window>(WIDTH, HEIGHT, "Vulkan Simulation Engine");
  window->setUserPointer(this);
  window->setFramebufferSizeCallback(framebufferResizeCallback);
  window->setKeyCallback(keyCallback);
  window->setCursorPosCallback(cursorPosCallback);
  window->setMouseButtonCallback(mouseButtonCallback);
  window->setScrollCallback(scrollCallback);
  lastFrameTime = static_cast<float>(glfwGetTime());
}

void Application::initVulkan() {
  instance = Vulkan::createInstance();
  debugMessenger = Vulkan::setupDebugMessenger(instance);
  surface = window->createSurface(instance);
  physicalDevice = Vulkan::pickPhysicalDevice(instance, surface);
  device = Vulkan::createLogicalDevice(physicalDevice, surface, graphicsQueue,
                                       presentQueue);
  swapChain = Vulkan::createSwapChain(device, physicalDevice, surface,
                                      window->getHandle(), swapChainImageFormat,
                                      swapChainExtent, swapChainImages);
  Vulkan::createImageViews(device, swapChainImages, swapChainImageFormat,
                           swapChainImageViews);
  depthFormat = Vulkan::findDepthFormat(physicalDevice);
  descriptorSetLayout = Vulkan::createDescriptorSetLayout(device);
  materialDescriptorSetLayout =
      Vulkan::createMaterialDescriptorSetLayout(device);
  commandPool = Vulkan::createCommandPool(device, physicalDevice, surface);

  Vulkan::QueueFamilyIndices indices;
  Vulkan::findQueueFamilies(physicalDevice, surface, indices);

  interface = std::make_unique<Interface>(
      window->getHandle(), instance, physicalDevice, device, graphicsQueue,
      commandPool,  // Passed CommandPool here
      indices.graphicsFamily.value(), swapChainImageFormat, depthFormat);
  interface->init();
  interface->resize(swapChainExtent, swapChainImageViews);

  renderDevice = std::make_unique<RenderDevice>(instance, device, physicalDevice,
                                                commandPool, graphicsQueue);
  textureManager = std::make_unique<TextureManager>(device, physicalDevice,
                                                    commandPool, graphicsQueue,
                                                    renderDevice->getAllocator());
  materialManager = std::make_unique<RenderMaterialManager>(renderDevice.get(),
                                                      textureManager.get());
  meshManager = std::make_unique<MeshManager>(renderDevice.get());
  gizmoMeshID = meshManager->createSphere(0.3f, 16);
  lightManager = std::make_unique<LightManager>(renderDevice.get());
  descriptorPool = Vulkan::createDescriptorPool(device, MAX_FRAMES_IN_FLIGHT);
  materialManager->init(materialDescriptorSetLayout);
  gizmoRenderMaterialID = materialManager->getDefaultMaterial();
  lightManager->init();
  instanceDescriptorSetLayout =
      Vulkan::createInstanceDescriptorSetLayout(device);
  mainPipeline =
      std::make_unique<MainPipeline>(device, swapChainImageFormat, depthFormat);
  mainPipeline->create(descriptorSetLayout, materialDescriptorSetLayout,
                       lightManager->getShadowDescriptorSetLayout(),
                       instanceDescriptorSetLayout);
  createShadowPipeline();
  postProcessing = std::make_unique<PostProcessing>(renderDevice.get(), device,
                                                    swapChainImageFormat);
  postProcessing->init(descriptorPool, swapChainExtent.width,
                       swapChainExtent.height);
  createDepthResources();
  createUniformBuffers();
  Vulkan::createDescriptorSets(device, descriptorPool, descriptorSetLayout,
                               uniformBuffers, lightManager->getLightBuffer(),
                               MAX_FRAMES_IN_FLIGHT, descriptorSets);
  createInstanceSSBOs();
  Vulkan::createCommandBuffers(device, commandPool, MAX_FRAMES_IN_FLIGHT,
                               commandBuffers);
  Vulkan::createSyncObjects(device, static_cast<int>(swapChainImages.size()),
                            imageAvailableSemaphores, renderFinishedSemaphores,
                            inFlightFences);

  worldParser    = std::make_unique<WorldParser>(meshManager.get(),
                                               materialManager.get(),
                                               textureManager.get());
  physicsMaterialManager = std::make_unique<PhysicsMaterialManager>();
  fbSceneLoader  = std::make_unique<FBSceneLoader>(meshManager.get(),
                                                   materialManager.get(),
                                                   physicsMaterialManager.get());
  animationSystem = std::make_unique<AnimationSystem>();
  physicsSystem   = std::make_unique<PhysicsSystem>();
  physicsSystem->setPhysicsMaterialManager(physicsMaterialManager.get());
  spawnerSystem   = std::make_unique<SpawnerSystem>();
  spawnerSystem->setPhysicsMaterialManager(physicsMaterialManager.get());
  timelineSystem  = std::make_unique<TimelineSystem>();

  // Networking
  networkManager = std::make_unique<NetworkManager>();
  networkManager->init(nullptr); // registry not available yet; set after world load
  physicsSystem->setNetworkManager(networkManager.get());
  spawnerSystem->setNetworkManager(networkManager.get());

  interface->setWorldDirectory("Scenes/Worlds");
  interface->setFBSceneDirectory("Scenes/FBs");
  interface->setWorldLoadCallback([this](const std::string& path) {
    auto ext = std::filesystem::path(path).extension().string();
    if (ext == ".fbscene") {
      loadFBScene(path);
    } else {
      loadWorld(path);
    }
    if (networkManager && !applyingRemoteSceneLoad) {
      networkManager->sendLoadScene(path);
      networkManager->sendOwnedObjectProperties();
    }
  });
}

void Application::mainLoop() {
while (!window->shouldClose()) {
  window->pollEvents();
  const float currentTime = static_cast<float>(glfwGetTime());
  const float deltaTime = currentTime - lastFrameTime;

  // FPS cap: if maxFps > 0, spin-wait until the minimum frame interval has elapsed
  if (simState.maxFps > 0) {
    const float minFrameTime = 1.0f / static_cast<float>(simState.maxFps);
    if (deltaTime < minFrameTime) continue;
  }

  lastFrameTime = currentTime;

  if (simState.reloadRequested) {
    simState.reloadRequested = false;
    if (!lastLoadedWorldPath.empty()) {
      auto reloadExt = std::filesystem::path(lastLoadedWorldPath).extension().string();
      if (reloadExt == ".fbscene") loadFBScene(lastLoadedWorldPath);
      else                         loadWorld(lastLoadedWorldPath);
      if (networkManager && !applyingRemoteSceneLoad) {
        networkManager->sendLoadScene(lastLoadedWorldPath);
        networkManager->sendOwnedObjectProperties();
      }
    }
  }

  bool snapshotsOn = interface->getSnapshotsEnabled();

  if (simState.resetRequested) {
    simState.resetRequested = false;
    if (snapshotsOn) {
      timelineSystem->restoreInitialSnapshot();
      timelineSystem->clearSnapshots();
    }
    animationSystem->reset();
    simState.timeHistory.clear();
    simState.currentTime = 0.0f;
    simState.historyIndex = -1;
    simState.rewinding = false;
    simState.reversePlay = false;
    simState.baked = false;
    simState.scrubAccumulator = 0.0f;
    simState.physicsAccumulator = 0.0f;
    if (networkManager && !lastLoadedWorldPath.empty() && !applyingRemoteSceneLoad) {
      networkManager->sendLoadScene(lastLoadedWorldPath);
      networkManager->sendOwnedObjectProperties();
    }
  }

  if (simState.loadBakeRequested && snapshotsOn && !lastLoadedWorldPath.empty()) {
    simState.loadBakeRequested = false;
    BakeStats loadedStats;
    std::vector<float> loadedHistory;
    if (WorldBakeSerializer::load(lastLoadedWorldPath, *timelineSystem,
                                  loadedHistory, loadedStats)) {
      simState.timeHistory    = std::move(loadedHistory);
      simState.bakeStats      = loadedStats;
      simState.baked          = true;
      simState.isPaused       = true;
      simState.historyIndex   = 0;
      simState.currentTime    = simState.timeHistory.empty() ? 0.0f : simState.timeHistory.front();
      simState.bakeTotalSteps = timelineSystem->getSnapshotCount();
      simState.bakeCurrentStep = simState.bakeTotalSteps;
      if (simState.historyIndex < timelineSystem->getSnapshotCount())
        timelineSystem->restoreSnapshot(0);
    }
  } else if (simState.loadBakeRequested) {
    simState.loadBakeRequested = false;
  }

  if (simState.bakeRequested && snapshotsOn) {
    simState.bakeRequested = false;
    if (!timelineSystem->hasInitialSnapshot()) {
      timelineSystem->saveInitialSnapshot();
    }
    timelineSystem->restoreInitialSnapshot();
    timelineSystem->clearSnapshots();
    simState.timeHistory.clear();
    simState.currentTime = 0.0f;
    simState.historyIndex = -1;
    simState.baked = false;
    simState.isPaused = true;
    simState.bakeStats = BakeStats{};

    simState.bakeTimeSpeedSave = simState.timeSpeed;
    simState.timeSpeed = 1.0f;
    simState.bakeTotalSteps = static_cast<int>(simState.bakeDuration / simState.stepSize);
    simState.bakeCurrentStep = 0;
    simState.isBaking = true;

    if (simState.bakePerformanceMode) {
      skipSceneRendering = true;
      bakeWallStart = std::chrono::high_resolution_clock::now();
      bakeUiFrameTimeAccum = 0.0;
      bakeUiFrameCount = 0;
    }
  } else if (simState.bakeRequested) {
    simState.bakeRequested = false;
  }

  if (simState.isBaking && snapshotsOn) {
    using clock = std::chrono::high_resolution_clock;

    if (simState.bakePerformanceMode) {
      // In performance mode: run a batch of steps per frame, timing each one.
      // Accumulate into bakeStats as we go; UI is still rendered each frame.
      constexpr int stepsPerFrame = 16;
      int end = glm::min(simState.bakeCurrentStep + stepsPerFrame, simState.bakeTotalSteps);

      for (int i = simState.bakeCurrentStep; i < end; i++) {
        auto stepStart = clock::now();
        PhysicsStepTimings t = physicsSystem->timedUpdate(simState.stepSize);
        auto snapStart = clock::now();
        timelineSystem->saveSnapshotUncompressed();
        auto snapEnd = clock::now();

        simState.currentTime += simState.stepSize;
        simState.timeHistory.push_back(simState.currentTime);

        double stepMs = std::chrono::duration<double, std::milli>(snapEnd - stepStart).count();
        double snapMs = std::chrono::duration<double, std::milli>(snapEnd - snapStart).count();

        BakeStats& st = simState.bakeStats;
        if (i == 0) {
          st.minStepMs = stepMs;
          st.maxStepMs = stepMs;
        } else {
          st.minStepMs = std::min(st.minStepMs, stepMs);
          st.maxStepMs = std::max(st.maxStepMs, stepMs);
        }
        st.avgStepMs    += stepMs;
        st.avgSyncToMs  += t.syncToMs;
        st.avgPhysStepMs += t.physStepMs;
        st.avgSyncFromMs += t.syncFromMs;
        st.avgSnapshotMs += snapMs;

        // Accumulate per-pair collision stats
        for (const auto& pair : t.collisionStats) {
          bool found = false;
          for (auto& bp : st.collisionPairs) {
            if (bp.pairName == pair.pairName) {
              bp.totalChecks   += pair.checks;
              bp.totalResolved += pair.resolved;
              found = true;
              break;
            }
          }
          if (!found) {
            st.collisionPairs.push_back({pair.pairName, pair.checks, pair.resolved});
          }
        }
      }
      simState.bakeCurrentStep = end;
    } else {
      // Normal mode: no timing, just step
      constexpr int stepsPerFrame = 4;
      int end = glm::min(simState.bakeCurrentStep + stepsPerFrame, simState.bakeTotalSteps);
      for (int i = simState.bakeCurrentStep; i < end; i++) {
        physicsSystem->update(simState.stepSize);
        timelineSystem->saveSnapshotUncompressed();
        simState.currentTime += simState.stepSize;
        simState.timeHistory.push_back(simState.currentTime);
      }
      simState.bakeCurrentStep = end;
    }

    if (simState.bakeCurrentStep >= simState.bakeTotalSteps) {
      simState.isBaking = false;
      simState.timeSpeed = simState.bakeTimeSpeedSave;
      simState.baked = true;
      simState.historyIndex = 0;
      simState.currentTime = simState.timeHistory.front();
      timelineSystem->restoreSnapshot(0);
      skipSceneRendering = false;

      if (simState.bakePerformanceMode && simState.bakeTotalSteps > 0) {
        BakeStats& st = simState.bakeStats;
        st.totalWallTimeMs = std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - bakeWallStart).count();
        st.avgUiFrameMs  = (bakeUiFrameCount > 0)
            ? (bakeUiFrameTimeAccum / bakeUiFrameCount) : 0.0;
        st.avgStepMs     /= simState.bakeTotalSteps;
        st.avgSyncToMs   /= simState.bakeTotalSteps;
        st.avgPhysStepMs /= simState.bakeTotalSteps;
        st.avgSyncFromMs /= simState.bakeTotalSteps;
        st.avgSnapshotMs /= simState.bakeTotalSteps;
        st.stepsPerSecond = (st.totalWallTimeMs > 0.0)
            ? (simState.bakeTotalSteps / (st.totalWallTimeMs / 1000.0)) : 0.0;
        st.simSecondsPerWallSecond = (st.totalWallTimeMs > 0.0)
            ? (simState.bakeDuration / (st.totalWallTimeMs / 1000.0)) : 0.0;
        st.objectCount  = physicsSystem->getObjectCount();
        st.totalSteps   = simState.bakeTotalSteps;
        st.simDuration  = simState.bakeDuration;
        st.stepSize     = simState.stepSize;
        st.hasData      = true;
      }

      // Save bake to disk for both modes
      if (!lastLoadedWorldPath.empty()) {
        BakeStats saveStats = simState.bakeStats;
        if (!saveStats.hasData) {
          saveStats.totalSteps  = simState.bakeTotalSteps;
          saveStats.simDuration = simState.bakeDuration;
          saveStats.stepSize    = simState.stepSize;
          saveStats.objectCount = physicsSystem->getObjectCount();
        }
        WorldBakeSerializer::save(lastLoadedWorldPath, *timelineSystem,
                                  simState.timeHistory, saveStats);
        if (interface) interface->notifyBakeSaved();
      }
    }
  }

  if (simState.snapshotScrubbed && snapshotsOn && !simState.isBaking) {
    simState.snapshotScrubbed = false;
    if (simState.historyIndex >= 0 &&
        simState.historyIndex < timelineSystem->getSnapshotCount()) {
      timelineSystem->restoreSnapshot(simState.historyIndex);
    }
  } else if (simState.snapshotScrubbed) {
    simState.snapshotScrubbed = false;
  }

  if (!simState.isBaking) {

  if (networkManager) {
    networkManager->tickReceive();

    // Create entities spawned by remote peers
    if (spawnerSystem) {
      SpawnEntityPacket spawnPkt{};
      while (networkManager->pollPendingSpawnedEntity(spawnPkt))
        spawnerSystem->applyRemoteSpawn(spawnPkt);
    }

    if (networkManager->pollNewPeerConnected() && !lastLoadedWorldPath.empty()) {
      networkManager->assignObjectOwnership();
      networkManager->sendLoadScene(lastLoadedWorldPath);
      networkManager->sendOwnedObjectProperties();
      // Auto-enable colour-by-owner the moment a peer appears; broadcast to all peers
      networkManager->colorByOwner = true;
      lastBroadcastColorByOwner    = false;  // triggers broadcast on next simstate send
      applyOwnerColors(true);
      lastColorByOwner = true;
    }

    // Re-assign ownership when a peer disconnects and refresh colours
    if (networkManager->pollPeerDropped()) {
      networkManager->assignObjectOwnership();
      if (networkManager->colorByOwner) {
        applyOwnerColors(true);
      }
    }

    // Packet-loss isolation: above the threshold this instance runs solo;
    // crossing back below re-joins the normal round-robin.
    {
      const bool highLoss = networkManager->simPacketLossPercent >= OWNERSHIP_LOSS_ISOLATION_PCT;
      if (highLoss != ownershipHighLossMode) {
        ownershipHighLossMode = highLoss;
        networkManager->assignObjectOwnership();
      }
    }

    std::string remotePath;
    if (networkManager->pollPendingSceneLoad(remotePath) && !remotePath.empty()) {
      applyingRemoteSceneLoad = true;
      auto remoteExt = std::filesystem::path(remotePath).extension().string();
      if (remoteExt == ".fbscene") loadFBScene(remotePath);
      else                         loadWorld(remotePath);
      applyingRemoteSceneLoad = false;
    }

    bool receivedStepForward = false;
    PendingSimState remoteSimState{};
    if (networkManager->pollPendingSimState(remoteSimState)) {
      simState.isPaused  = remoteSimState.isPaused;
      simState.timeSpeed = remoteSimState.timeSpeed;
      lastBroadcastPaused    = simState.isPaused;
      lastBroadcastTimeSpeed = simState.timeSpeed;

      if (remoteSimState.historyIndex >= 0) {
        simState.historyIndex = remoteSimState.historyIndex;
        if (snapshotsOn && simState.historyIndex < timelineSystem->getSnapshotCount())
          timelineSystem->restoreSnapshot(simState.historyIndex);
        lastBroadcastHistoryIndex = simState.historyIndex;
      }
      if (remoteSimState.stepForward) {
        receivedStepForward = true;
      }
      if (remoteSimState.reversePlay >= 0) {
        simState.reversePlay = (remoteSimState.reversePlay == 1);
        if (!simState.reversePlay) simState.isPaused = true;
        lastBroadcastReversePlay = simState.reversePlay;
      }
      if (remoteSimState.colorByOwner >= 0) {
        networkManager->colorByOwner = (remoteSimState.colorByOwner == 1);
        lastBroadcastColorByOwner = networkManager->colorByOwner;
      }
    }

    {
      int32_t bHistIdx    = -1;
      bool    bStepFwd    = false;
      int8_t  bRevPlay    = -1;
      int8_t  bColor      = -1;
      bool    needBcast   = (simState.isPaused != lastBroadcastPaused ||
                              simState.timeSpeed != lastBroadcastTimeSpeed);

      if (simState.historyIndex != lastBroadcastHistoryIndex) {
        bHistIdx = simState.historyIndex;
        lastBroadcastHistoryIndex = simState.historyIndex;
        needBcast = true;
      }
      if (simState.stepFrame && !receivedStepForward) {
        bStepFwd = true;
        needBcast = true;
      }
      if (simState.reversePlay != lastBroadcastReversePlay) {
        bRevPlay = simState.reversePlay ? 1 : 0;
        lastBroadcastReversePlay = simState.reversePlay;
        needBcast = true;
      }
      bool curColor = networkManager->colorByOwner;
      if (curColor != lastBroadcastColorByOwner) {
        bColor = curColor ? 1 : 0;
        lastBroadcastColorByOwner = curColor;
        needBcast = true;
      }

      if (needBcast) {
        networkManager->sendSimState(simState.isPaused, simState.timeSpeed,
                                     bHistIdx, bStepFwd, bRevPlay, bColor);
        lastBroadcastPaused    = simState.isPaused;
        lastBroadcastTimeSpeed = simState.timeSpeed;
      }
    }

    if (receivedStepForward) {
      simState.stepFrame = true;
    }
  }

  // Reverse-play and baked-scrub modify the registry (timeline restore), so
  // they must be protected against the simulation thread with simMutex.
  // Live simulation has been moved to simulationThreadFunc() on Core 4+.
  {
    std::lock_guard<std::mutex> lock(simMutex);
    if (simState.reversePlay && !simState.isPaused && snapshotsOn) {
      int snapshotCount = timelineSystem->getSnapshotCount();
      if (snapshotCount > 0) {
        if (simState.historyIndex < 0)
          simState.historyIndex = snapshotCount - 1;

        float rewindSpeed = deltaTime * simState.timeSpeed;
        simState.scrubAccumulator += rewindSpeed / simState.stepSize;
        int steps = static_cast<int>(simState.scrubAccumulator);
        simState.scrubAccumulator -= static_cast<float>(steps);

        if (steps > 0) {
          simState.historyIndex = glm::max(simState.historyIndex - steps, 0);
          timelineSystem->restoreSnapshot(simState.historyIndex);

          if (simState.historyIndex < static_cast<int>(simState.timeHistory.size()))
            simState.currentTime = simState.timeHistory[simState.historyIndex];
        }

        if (simState.historyIndex <= 0) {
          simState.reversePlay = false;
          simState.isPaused = true;
        }
      }
    } else if (simState.baked && snapshotsOn && (!simState.isPaused || simState.stepFrame)) {
      int snapshotCount = timelineSystem->getSnapshotCount();
      if (snapshotCount > 0) {
        if (simState.historyIndex < 0) simState.historyIndex = 0;

        if (simState.stepFrame) {
          simState.historyIndex = glm::min(simState.historyIndex + 1,
                                            snapshotCount - 1);
        } else {
          float playSpeed = deltaTime * simState.timeSpeed;
          simState.scrubAccumulator += playSpeed / simState.stepSize;
          int steps = static_cast<int>(simState.scrubAccumulator);
          simState.scrubAccumulator -= static_cast<float>(steps);
          if (steps > 0) {
            simState.historyIndex = glm::min(simState.historyIndex + steps,
                                              snapshotCount - 1);
          }
        }

        timelineSystem->restoreSnapshot(simState.historyIndex);
        if (simState.historyIndex < static_cast<int>(simState.timeHistory.size()))
          simState.currentTime = simState.timeHistory[simState.historyIndex];

        simState.stepFrame = false;
        simState.rewinding = false;
        simState.reversePlay = false;

        if (simState.historyIndex >= snapshotCount - 1) {
          simState.isPaused = true;
        }
      }
    }
    // Live simulation is handled by simulationThreadFunc() running on Core 4+.
  }
  } // end !isBaking

    PerformanceMetrics perfMetrics;
    perfMetrics.frameTimeMs        = deltaTime * 1000.0f;
    perfMetrics.gpuWaitMs          = perfGpuWaitMs;
    perfMetrics.uniformBufferMs    = perfUniformBufferMs;
    perfMetrics.commandBufferMs    = perfCommandBufferMs;
    perfMetrics.interfaceRenderMs  = perfInterfaceRenderMs;
    perfMetrics.physSyncToMs       = simPerf.physSyncToMs.load(std::memory_order_relaxed);
    perfMetrics.physStepMs         = simPerf.physStepMs.load(std::memory_order_relaxed);
    perfMetrics.physSyncFromMs     = simPerf.physSyncFromMs.load(std::memory_order_relaxed);
    perfMetrics.animationMs        = simPerf.animationMs.load(std::memory_order_relaxed);
    perfMetrics.spawnerMs          = simPerf.spawnerMs.load(std::memory_order_relaxed);
    perfMetrics.snapshotMs         = simPerf.snapshotMs.load(std::memory_order_relaxed);
    perfMetrics.simThreadTotalMs   = simPerf.totalMs.load(std::memory_order_relaxed);
    if (registry) {
      std::lock_guard<std::mutex> lock(simMutex);
      perfMetrics.entityCount   = static_cast<int>(registry->allNames().size());
      perfMetrics.simBodyCount  = static_cast<int>(registry->allSimulated().size());
      perfMetrics.colliderCount = static_cast<int>(registry->allColliders().size());
      perfMetrics.lightCount    = static_cast<int>(registry->allLights().size());
      perfMetrics.spawnerCount  = static_cast<int>(registry->allSpawners().size());
      perfMetrics.animCount     = static_cast<int>(registry->allAnimations().size());
      if (timelineSystem) {
        perfMetrics.snapshotCount = timelineSystem->getSnapshotCount();
        perfMetrics.snapshotMemKB = static_cast<size_t>(perfMetrics.snapshotCount)
                                  * static_cast<size_t>(perfMetrics.entityCount)
                                  * 96 / 1024;
      }
    }
    if (networkManager && networkManager->getConnectedPeerCount() >= 1) {
      perfMetrics.connectedPeers = networkManager->getConnectedPeerCount();
      perfMetrics.packetLoss     = networkManager->simPacketLossPercent;
      perfMetrics.latencyMs      = networkManager->simExtraLatencyMs;
    }

    if (simState.isBaking && simState.bakePerformanceMode) {
      auto uiStart = std::chrono::high_resolution_clock::now();
      interface->render(simState, sceneSettings, *registry, mainPipeline.get(),
                         postProcessing.get(), perfMetrics, networkManager.get());
      input.endFrame();
      drawFrame();
      double uiMs = std::chrono::duration<double, std::milli>(
          std::chrono::high_resolution_clock::now() - uiStart).count();
      bakeUiFrameTimeAccum += uiMs;
      ++bakeUiFrameCount;
    } else {
      using PerfClock = std::chrono::steady_clock;
      const auto tRender0 = PerfClock::now();
      interface->render(simState, sceneSettings, *registry, mainPipeline.get(),
                         postProcessing.get(), perfMetrics, networkManager.get());
      perfInterfaceRenderMs = static_cast<float>(
          std::chrono::duration<double, std::milli>(PerfClock::now() - tRender0).count());

      if (networkManager && registry) {
        bool want = networkManager->colorByOwner;
        if (want != lastColorByOwner) {
          applyOwnerColors(want);
          lastColorByOwner = want;
        }
      }

      int camSwitch = -1;
      if (interface->pollCameraSwitch(camSwitch)) {
        switchToCamera(camSwitch);
      }
      interface->setActiveCameraIndex(activeCameraIndex);

      if (!ImGui::GetIO().WantCaptureMouse &&
          !ImGui::GetIO().WantCaptureKeyboard) {
        input.update();
        updateCameraController(deltaTime);
        if (camCtrl.mode == CameraMode::FPS)
          glfwSetInputMode(window->getHandle(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        else
          glfwSetInputMode(window->getHandle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
      } else {
        glfwSetInputMode(window->getHandle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
      }

      input.endFrame();
      drawFrame();
    }
  }
  vkDeviceWaitIdle(device);
}

void Application::cleanup() {
  vkDeviceWaitIdle(device);
  cleanupSwapChain();
  interface->cleanup();
  postProcessing.reset();
  lightManager.reset();
  materialManager.reset();
  textureManager.reset();
  renderDevice.reset();
  meshManager.reset();
  if (shadowPipeline != VK_NULL_HANDLE)
    vkDestroyPipeline(device, shadowPipeline, nullptr);
  if (shadowPipelineLayout != VK_NULL_HANDLE)
    vkDestroyPipelineLayout(device, shadowPipelineLayout, nullptr);
  if (mainPipeline) mainPipeline->cleanup();
  for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
    if (i < static_cast<int>(instanceSSBOBuffers.size())) {
      renderDevice->destroyBuffer(instanceSSBOBuffers[i], instanceSSBOAlloc[i]);
    }
  }
  if (instanceDescriptorPool != VK_NULL_HANDLE)
    vkDestroyDescriptorPool(device, instanceDescriptorPool, nullptr);
  if (instanceDescriptorSetLayout != VK_NULL_HANDLE)
    vkDestroyDescriptorSetLayout(device, instanceDescriptorSetLayout, nullptr);
  vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
  vkDestroyDescriptorSetLayout(device, materialDescriptorSetLayout, nullptr);
  renderDevice->destroyBuffer(indexBuffer, indexBufferAlloc);
  renderDevice->destroyBuffer(vertexBuffer, vertexBufferAlloc);
  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    renderDevice->destroyBuffer(uniformBuffers[i], uniformBuffersAlloc[i]);
  vkDestroyDescriptorPool(device, descriptorPool, nullptr);
  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
    vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
    vkDestroyFence(device, inFlightFences[i], nullptr);
  }
  vkDestroyCommandPool(device, commandPool, nullptr);
  vkDestroyDevice(device, nullptr);
  if (Vulkan::enableValidationLayers && debugMessenger != VK_NULL_HANDLE)
    Vulkan::DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
  vkDestroySurfaceKHR(instance, surface, nullptr);
  vkDestroyInstance(instance, nullptr);
}

void Application::createUniformBuffers() {
  const VkDeviceSize bufferSize = sizeof(UniformBufferObject);
  uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
  uniformBuffersAlloc.resize(MAX_FRAMES_IN_FLIGHT);
  uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);
  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    renderDevice->createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               uniformBuffers[i], uniformBuffersAlloc[i],
                               &uniformBuffersMapped[i]);
  }
}

void Application::createInstanceSSBOs() {
  const VkDeviceSize bufferSize = sizeof(InstanceData) * MAX_INSTANCES;

  instanceSSBOBuffers.resize(MAX_FRAMES_IN_FLIGHT);
  instanceSSBOAlloc.resize(MAX_FRAMES_IN_FLIGHT);
  instanceSSBOMapped.resize(MAX_FRAMES_IN_FLIGHT);

  for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
    renderDevice->createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        instanceSSBOBuffers[i], instanceSSBOAlloc[i],
        &instanceSSBOMapped[i]);
  }

  // Dedicated small pool for the instance SSBO descriptor sets
  VkDescriptorPoolSize poolSize{};
  poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  poolSize.descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = 1;
  poolInfo.pPoolSizes = &poolSize;
  poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

  if (vkCreateDescriptorPool(device, &poolInfo, nullptr,
                             &instanceDescriptorPool) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create instance descriptor pool!");
  }

  Vulkan::createInstanceDescriptorSets(
      device, instanceDescriptorPool, instanceDescriptorSetLayout,
      MAX_FRAMES_IN_FLIGHT, instanceSSBOBuffers, bufferSize,
      instanceDescriptorSets);
}

void Application::buildRenderProxies() {
  renderProxies.clear();
  if (!registry) return;
  const auto& entities = registry->getEntities();
  renderProxies.reserve(entities.size());
  for (Entity entity : entities) {
    const auto* renderComp    = registry->getComponent<RenderComponent>(entity);
    const auto* meshComp      = registry->getComponent<MeshComponent>(entity);
    const auto* matComp       = registry->getComponent<RenderMaterialComponent>(entity);
    const auto* transformComp = registry->getComponent<TransformComponent>(entity);
    const auto* lightComp     = registry->getComponent<LightComponent>(entity);

    RenderProxy proxy;
    proxy.entity = entity;
    if (transformComp) {
      proxy.modelMatrix = transformComp->getModelMatrix();
      proxy.position    = transformComp->position;
      proxy.maxAbsScale = std::max({std::abs(transformComp->scale.x),
                                    std::abs(transformComp->scale.y),
                                    std::abs(transformComp->scale.z)});
    }
    proxy.meshID       = meshComp    ? meshComp->meshID                    : INVALID_MESH_ID;
    proxy.matID        = matComp     ? matComp->renderMaterialID           : INVALID_RENDER_MATERIAL_ID;
    proxy.visible      = renderComp  ? renderComp->visible                 : false;
    proxy.layerMask    = renderComp  ? renderComp->layerMask               : 0xFFFFFFFF;
    proxy.isPointLight = lightComp   && lightComp->type != LightType::Sun;

    renderProxies.push_back(proxy);
  }
}

void Application::drawFrame() {
  const bool doPerfTiming = interface && interface->isPerfProfilingEnabled();
  using FrameClock = std::chrono::steady_clock;
  auto msec = [](FrameClock::time_point a, FrameClock::time_point b) -> float {
    return static_cast<float>(
        std::chrono::duration<double, std::milli>(b - a).count());
  };

  const auto tGpuWait0 = doPerfTiming ? FrameClock::now() : FrameClock::time_point{};
  vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE,
                  UINT64_MAX);
  if (doPerfTiming) perfGpuWaitMs = msec(tGpuWait0, FrameClock::now());

  uint32_t imageIndex;
  VkResult result = vkAcquireNextImageKHR(
      device, swapChain, UINT64_MAX, imageAvailableSemaphores[currentFrame],
      VK_NULL_HANDLE, &imageIndex);
  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    recreateSwapChain();
    return;
  } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    throw std::runtime_error("Failed to acquire swap chain image!");
  vkResetFences(device, 1, &inFlightFences[currentFrame]);
  {
    // Hold simMutex only long enough to:
    //   1. update the UBO (camera + light matrices, ~microseconds)
    //   2. snapshot all entity render state into renderProxies (linear copy)
    // recordCommandBuffer then runs entirely outside the lock so the
    // simulation thread is free to advance physics during GPU submission.
    std::lock_guard<std::mutex> lock(simMutex);

    const auto tUbo0 = doPerfTiming ? FrameClock::now() : FrameClock::time_point{};
    updateUniformBuffer(currentFrame);
    buildRenderProxies();
    if (doPerfTiming) perfUniformBufferMs = msec(tUbo0, FrameClock::now());
  }

  vkResetCommandBuffer(commandBuffers[currentFrame], 0);
  const auto tCmd0 = doPerfTiming ? FrameClock::now() : FrameClock::time_point{};
  recordCommandBuffer(commandBuffers[currentFrame], imageIndex);
  if (doPerfTiming) perfCommandBufferMs = msec(tCmd0, FrameClock::now());
  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
  VkPipelineStageFlags waitStages[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffers[currentFrame];
  VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[imageIndex]};
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;
  if (vkQueueSubmit(graphicsQueue, 1, &submitInfo,
                    inFlightFences[currentFrame]) != VK_SUCCESS)
    throw std::runtime_error("Failed to submit draw command buffer!");
  VkPresentInfoKHR presentInfo{};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = signalSemaphores;
  VkSwapchainKHR swapChains[] = {swapChain};
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = swapChains;
  presentInfo.pImageIndices = &imageIndex;
  result = vkQueuePresentKHR(presentQueue, &presentInfo);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
      framebufferResized) {
    framebufferResized = false;
    recreateSwapChain();
  } else if (result != VK_SUCCESS)
    throw std::runtime_error("Failed to present swap chain image!");
  currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Application::recreateSwapChain() {
  int width = 0, height = 0;
  window->getFramebufferSize(width, height);
  while (width == 0 || height == 0) {
    window->getFramebufferSize(width, height);
    window->waitEvents();
  }
  vkDeviceWaitIdle(device);
  cleanupSwapChain();
  swapChain = Vulkan::createSwapChain(device, physicalDevice, surface,
                                      window->getHandle(), swapChainImageFormat,
                                      swapChainExtent, swapChainImages);
  Vulkan::createImageViews(device, swapChainImages, swapChainImageFormat,
                           swapChainImageViews);
  createDepthResources();
  postProcessing->resize(swapChainExtent.width, swapChainExtent.height,
                         descriptorPool);
  interface->resize(swapChainExtent, swapChainImageViews);
}

void Application::cleanupSwapChain() {
  if (depthImageView != VK_NULL_HANDLE) {
    vkDestroyImageView(device, depthImageView, nullptr);
    depthImageView = VK_NULL_HANDLE;
  }
  if (depthImage != VK_NULL_HANDLE) {
    renderDevice->destroyImage(depthImage, depthImageAlloc);
    depthImage      = VK_NULL_HANDLE;
    depthImageAlloc = VK_NULL_HANDLE;
  }
  for (const auto imageView : swapChainImageViews)
    vkDestroyImageView(device, imageView, nullptr);
  vkDestroySwapchainKHR(device, swapChain, nullptr);
}

void Application::updateUniformBuffer(uint32_t currentImage) {
lightManager->updateAllShadowMatrices(glm::vec3(0.0f), 1000.0f);
lightManager->updateLightBuffer();
  UniformBufferObject ubo{};
  ubo.view = getActiveCameraViewMatrix();
  const float aspect = swapChainExtent.width / static_cast<float>(swapChainExtent.height);
  const CameraComponent* cam = (activeCamera != INVALID_ENTITY && registry)
      ? registry->getComponent<CameraComponent>(activeCamera) : nullptr;
  if (cam && cam->type == CameraType::Orthographic) {
    float h = cam->orthographicSize;
    ubo.proj = glm::ortho(-h * aspect, h * aspect, -h, h, cam->nearPlane, cam->farPlane);
  } else {
    ubo.proj = glm::perspective(
        glm::radians(cam ? cam->fov : 45.0f), aspect,
        cam ? cam->nearPlane : 0.1f, cam ? cam->farPlane : 50000.0f);
  }
  ubo.proj[1][1] *= -1;
  currentViewProj = ubo.proj * ubo.view;
  ubo.eyePos = getActiveCameraPosition();
  ubo.time = simState.currentTime;
  std::vector<ShadowMapData> shadowMaps;
  lightManager->getShadowSystem()->getShadowMaps(shadowMaps);
  for (size_t i = 0; i < shadowMaps.size() && i < MAX_SHADOW_CASTERS; i++)
    ubo.lightSpaceMatrices[i] = shadowMaps[i].lightSpaceMatrix;
  for (size_t i = shadowMaps.size(); i < MAX_SHADOW_CASTERS; i++)
    ubo.lightSpaceMatrices[i] = glm::mat4(1.0f);
  memcpy(uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
}

void Application::recreateGraphicsPipeline() {
  vkDeviceWaitIdle(device);
  mainPipeline->recreate();
}

void Application::recreateTextureSamplers(VkFilter magFilter,
                                          VkFilter minFilter) {
  vkDeviceWaitIdle(device);
  textureManager->recreateSamplers(magFilter, minFilter);
}

void Application::toggleShadingMode() {
  if (mainPipeline->getShadingMode() == MainPipeline::ShadingMode::Phong)
    mainPipeline->setShadingMode(MainPipeline::ShadingMode::Gouraud);
  else
    mainPipeline->setShadingMode(MainPipeline::ShadingMode::Phong);
  recreateGraphicsPipeline();
}

void Application::createDepthResources() {
  renderDevice->createImage(swapChainExtent.width, swapChainExtent.height, 1,
                            depthFormat, VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            depthImage, depthImageAlloc);
  depthImageView = RenderUtils::createImageView(device, depthImage, depthFormat,
                                                VK_IMAGE_ASPECT_DEPTH_BIT);
}

void Application::framebufferResizeCallback(GLFWwindow* win, int width,
                                            int height) {
  auto* const app =
      reinterpret_cast<Application*>(glfwGetWindowUserPointer(win));
  app->framebufferResized = true;
}

void Application::keyCallback(GLFWwindow* win, int key, int scancode,
                              int action, int mods) {
  if (win == nullptr) return;
  auto* const app =
      reinterpret_cast<Application*>(glfwGetWindowUserPointer(win));
  app->input.onKey(key, scancode, action, mods);
  if (action == GLFW_PRESS) {
    switch (key) {
      case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(win, true);
        break;
      case GLFW_KEY_R:
        app->simState.resetRequested = true;
        break;
      case GLFW_KEY_SPACE:
        if (!ImGui::GetIO().WantCaptureKeyboard) {
          app->simState.isPaused = !app->simState.isPaused;
          app->simState.rewinding = false;
          app->simState.reversePlay = false;
        }
        break;
      case GLFW_KEY_PERIOD:
        app->simState.isPaused = true;
        app->simState.stepFrame = true;
        app->simState.rewinding = false;
        break;
      case GLFW_KEY_COMMA:
        if (app->timelineSystem->getSnapshotCount() > 0) {
          app->simState.isPaused = true;
          app->simState.rewinding = true;
          app->simState.reversePlay = false;
          int snapshotCount = app->timelineSystem->getSnapshotCount();
          if (app->simState.historyIndex < 0)
            app->simState.historyIndex = snapshotCount - 1;
          if (app->simState.historyIndex > 0) {
            app->simState.historyIndex--;
            app->timelineSystem->restoreSnapshot(app->simState.historyIndex);
            if (app->simState.historyIndex <
                static_cast<int>(app->simState.timeHistory.size()))
              app->simState.currentTime =
                  app->simState.timeHistory[app->simState.historyIndex];
          }
        }
        break;
      case GLFW_KEY_EQUAL:
      case GLFW_KEY_KP_ADD:
        app->simState.timeSpeed =
            glm::min(app->simState.timeSpeed * 2.0f, 10.0f);
        break;
      case GLFW_KEY_MINUS:
      case GLFW_KEY_KP_SUBTRACT:
        app->simState.timeSpeed =
            glm::max(app->simState.timeSpeed * 0.5f, 0.01f);
        break;
      case GLFW_KEY_L:
        app->toggleShadingMode();
        break;
      case GLFW_KEY_K:
        app->postProcessing->toggleToonMode();
        break;
      case GLFW_KEY_TAB: {
        auto current = app->mainPipeline->getPolygonMode();
        if (current == VK_POLYGON_MODE_FILL)
          app->mainPipeline->setPolygonMode(VK_POLYGON_MODE_LINE);
        else if (current == VK_POLYGON_MODE_LINE)
          app->mainPipeline->setPolygonMode(VK_POLYGON_MODE_POINT);
        else
          app->mainPipeline->setPolygonMode(VK_POLYGON_MODE_FILL);
        app->recreateGraphicsPipeline();
        break;
      }
      case GLFW_KEY_G:
        if (app->mainPipeline->getPolygonMode() == VK_POLYGON_MODE_LINE)
          app->mainPipeline->setPolygonMode(VK_POLYGON_MODE_FILL);
        else
          app->mainPipeline->setPolygonMode(VK_POLYGON_MODE_LINE);
        app->recreateGraphicsPipeline();
        break;
      case GLFW_KEY_F1:
        break;
      case GLFW_KEY_F11: {
        GLFWmonitor* monitor = glfwGetWindowMonitor(win);
        if (monitor) {
          glfwSetWindowMonitor(win, nullptr, 100, 100, WIDTH, HEIGHT, 0);
        } else {
          monitor = glfwGetPrimaryMonitor();
          const GLFWvidmode* mode = glfwGetVideoMode(monitor);
          glfwSetWindowMonitor(win, monitor, 0, 0, mode->width, mode->height,
                               mode->refreshRate);
        }
        break;
      }
      case GLFW_KEY_1: app->switchToCamera(0); break;
      case GLFW_KEY_2: app->switchToCamera(1); break;
      case GLFW_KEY_3: app->switchToCamera(2); break;
      case GLFW_KEY_4: app->switchToCamera(3); break;
      case GLFW_KEY_5: app->switchToCamera(4); break;
      case GLFW_KEY_6: app->switchToCamera(5); break;
      case GLFW_KEY_7: app->switchToCamera(6); break;
      case GLFW_KEY_8: app->switchToCamera(7); break;
      case GLFW_KEY_9: app->switchToCamera(8); break;
      default:
        break;
    }
  }
}

void Application::initCamerasFromRegistry() {
  scenecameras.clear();
  if (!registry) return;

  for (const auto& [entity, _] : registry->allCameras())
    scenecameras.push_back(entity);
  std::sort(scenecameras.begin(), scenecameras.end());

  if (scenecameras.empty()) {
    Entity cam = registry->createEntity();
    registry->addComponent<NameComponent>(cam, {"Default Camera"});

    const glm::vec3 pos(30.0f, 35.0f, 80.0f);
    const glm::vec3 target(0.0f, 8.0f, 0.0f);
    const glm::vec3 fwd = glm::normalize(target - pos);
    const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0,1,0)));
    const glm::vec3 up = glm::cross(right, fwd);

    TransformComponent t;
    t.position = pos;
    t.rotation = glm::quat_cast(glm::mat3(right, up, -fwd));
    registry->addComponent<TransformComponent>(cam, t);

    CameraComponent camComp;
    camComp.fov = 60.0f;
    camComp.nearPlane = 0.1f;
    camComp.farPlane = 5000.0f;
    registry->addComponent<CameraComponent>(cam, camComp);

    scenecameras.push_back(cam);
  }

  activeCameraIndex = 0;
  activeCamera = scenecameras[0];

  auto* t = registry->getComponent<TransformComponent>(activeCamera);
  if (t) {
    glm::vec3 fwd = glm::normalize(t->rotation * glm::vec3(0,0,-1));
    camCtrl.fpsPosition = t->position;
    camCtrl.fpsYaw = glm::degrees(atan2(fwd.z, fwd.x));
    camCtrl.fpsPitch = glm::degrees(asin(glm::clamp(fwd.y, -1.0f, 1.0f)));
    float radius = glm::max(glm::length(t->position), 50.0f);
    camCtrl.orbitRadius = radius;
    camCtrl.orbitPivot = t->position + fwd * radius;
    glm::vec3 toPos = glm::normalize(t->position - camCtrl.orbitPivot);
    camCtrl.orbitPhi = acos(glm::clamp(toPos.y, -1.0f, 1.0f));
    camCtrl.orbitTheta = atan2(toPos.z, toPos.x);
    camCtrl.lastOrbitPosition = t->position;
  } else {
    camCtrl.orbitPivot = glm::vec3(0,0,0);
    camCtrl.orbitRadius = 350.0f;
    camCtrl.fpsPosition = glm::vec3(0, 50, 100);
    camCtrl.lastOrbitPosition = glm::vec3(0, 50, 100);
  }
  camCtrl.mode = CameraMode::ORBIT;
}

void Application::switchToCamera(int index) {
  if (scenecameras.empty() || !registry) return;
  index = glm::clamp(index, 0, static_cast<int>(scenecameras.size()) - 1);
  activeCameraIndex = index;
  activeCamera = scenecameras[index];

  auto* t = registry->getComponent<TransformComponent>(activeCamera);
  if (!t) return;
  glm::vec3 fwd = glm::normalize(t->rotation * glm::vec3(0,0,-1));
  camCtrl.fpsPosition = t->position;
  camCtrl.fpsYaw = glm::degrees(atan2(fwd.z, fwd.x));
  camCtrl.fpsPitch = glm::degrees(asin(glm::clamp(fwd.y, -1.0f, 1.0f)));
  float radius = glm::max(glm::length(t->position), 50.0f);
  camCtrl.orbitRadius = radius;
  camCtrl.orbitPivot = t->position + fwd * radius;
  glm::vec3 toPos = glm::normalize(t->position - camCtrl.orbitPivot);
  camCtrl.orbitPhi = acos(glm::clamp(toPos.y, -1.0f, 1.0f));
  camCtrl.orbitTheta = atan2(toPos.z, toPos.x);
  camCtrl.lastOrbitPosition = t->position;
  camCtrl.mode = CameraMode::ORBIT;
}

void Application::updateCameraController(float deltaTime) {
  if (activeCamera == INVALID_ENTITY || !registry) return;
  auto* transform = registry->getComponent<TransformComponent>(activeCamera);
  if (!transform) return;

  auto fpsForward = [](float yaw, float pitch) -> glm::vec3 {
    return glm::normalize(glm::vec3(
        cos(glm::radians(yaw)) * cos(glm::radians(pitch)),
        sin(glm::radians(pitch)),
        sin(glm::radians(yaw)) * cos(glm::radians(pitch))));
  };

  if (input.wasKeyJustPressed(GLFW_KEY_ENTER)) {
    if (camCtrl.mode == CameraMode::ORBIT) {
      camCtrl.mode = CameraMode::FPS;
      camCtrl.fpsPosition = camCtrl.lastOrbitPosition;
      glm::vec3 dir = glm::normalize(camCtrl.orbitPivot - camCtrl.lastOrbitPosition);
      camCtrl.fpsYaw = glm::degrees(atan2(dir.z, dir.x));
      camCtrl.fpsPitch = glm::degrees(asin(glm::clamp(dir.y, -1.0f, 1.0f)));
    } else {
      camCtrl.mode = CameraMode::ORBIT;
      glm::vec3 offset = camCtrl.fpsPosition - camCtrl.orbitPivot;
      camCtrl.orbitRadius = glm::length(offset);
      if (camCtrl.orbitRadius > 0.001f) {
        glm::vec3 n = offset / camCtrl.orbitRadius;
        camCtrl.orbitPhi = acos(glm::clamp(n.y, -1.0f, 1.0f));
        camCtrl.orbitTheta = atan2(n.z, n.x);
      }
    }
  }

  const bool ctrl = input.isKeyPressed(GLFW_KEY_LEFT_CONTROL) || input.isKeyPressed(GLFW_KEY_RIGHT_CONTROL);
  if (ctrl) {
    glm::vec3 fwd = fpsForward(camCtrl.fpsYaw, camCtrl.fpsPitch);
    glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0,1,0)));
    glm::vec3 movement(0);
    if (input.isKeyPressed(GLFW_KEY_UP)) movement += fwd;
    if (input.isKeyPressed(GLFW_KEY_DOWN)) movement -= fwd;
    if (input.isKeyPressed(GLFW_KEY_LEFT)) movement -= right;
    if (input.isKeyPressed(GLFW_KEY_RIGHT)) movement += right;
    if (input.isKeyPressed(GLFW_KEY_PAGE_UP)) movement.y += 1;
    if (input.isKeyPressed(GLFW_KEY_PAGE_DOWN)) movement.y -= 1;
    if (glm::length(movement) > 0) {
      glm::vec3 delta = glm::normalize(movement) * (camCtrl.fpsSpeed * deltaTime);
      camCtrl.fpsPosition += delta;
      if (camCtrl.mode == CameraMode::ORBIT) camCtrl.orbitPivot += delta;
    }
  } else {
    const float rotSpeed = 2.0f * deltaTime;
    float dYaw = 0, dPitch = 0;
    if (input.isKeyPressed(GLFW_KEY_LEFT)) dYaw -= rotSpeed;
    if (input.isKeyPressed(GLFW_KEY_RIGHT)) dYaw += rotSpeed;
    if (input.isKeyPressed(GLFW_KEY_UP)) dPitch += rotSpeed;
    if (input.isKeyPressed(GLFW_KEY_DOWN)) dPitch -= rotSpeed;
    if (std::abs(dYaw) > 0 || std::abs(dPitch) > 0) {
      if (camCtrl.mode == CameraMode::FPS) {
        camCtrl.fpsYaw += glm::degrees(dYaw) * 50.0f * deltaTime;
        camCtrl.fpsPitch = glm::clamp(camCtrl.fpsPitch + glm::degrees(dPitch) * 50.0f * deltaTime, -89.0f, 89.0f);
      } else {
        camCtrl.orbitTheta += dYaw * 2.0f;
        camCtrl.orbitPhi = glm::clamp(camCtrl.orbitPhi - dPitch * 2.0f, 0.1f, glm::pi<float>() - 0.1f);
      }
    }
  }

  if (camCtrl.mode == CameraMode::ORBIT) {
    double dx = 0, dy = 0;
    input.getMouseDelta(dx, dy);
    if (input.isMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT)) {
      camCtrl.orbitTheta -= static_cast<float>(dx) * 0.005f;
      camCtrl.orbitPhi = glm::clamp(camCtrl.orbitPhi + static_cast<float>(dy) * 0.005f, 0.1f, glm::pi<float>() - 0.1f);
    }
    const double scroll = input.getScrollDelta();
    if (std::abs(scroll) > 0) camCtrl.orbitRadius -= static_cast<float>(scroll) * 5.0f;

    const float x = camCtrl.orbitRadius * sin(camCtrl.orbitPhi) * cos(camCtrl.orbitTheta);
    const float y = camCtrl.orbitRadius * cos(camCtrl.orbitPhi);
    const float z = camCtrl.orbitRadius * sin(camCtrl.orbitPhi) * sin(camCtrl.orbitTheta);
    camCtrl.lastOrbitPosition = camCtrl.orbitPivot + glm::vec3(x, y, z);
    transform->position = camCtrl.lastOrbitPosition;
  } else {
    double dx = 0, dy = 0;
    input.getMouseDelta(dx, dy);
    camCtrl.fpsYaw += static_cast<float>(dx) * 0.1f;
    camCtrl.fpsPitch = glm::clamp(camCtrl.fpsPitch - static_cast<float>(dy) * 0.1f, -89.0f, 89.0f);
    const double scroll = input.getScrollDelta();
    if (std::abs(scroll) > 0) camCtrl.fpsSpeed = glm::max(1.0f, camCtrl.fpsSpeed + static_cast<float>(scroll) * 2.0f);

    glm::vec3 fwd = fpsForward(camCtrl.fpsYaw, camCtrl.fpsPitch);
    glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0,1,0)));
    const float spd = camCtrl.fpsSpeed * deltaTime;
    if (input.isKeyPressed(GLFW_KEY_W)) camCtrl.fpsPosition += fwd * spd;
    if (input.isKeyPressed(GLFW_KEY_S)) camCtrl.fpsPosition -= fwd * spd;
    if (input.isKeyPressed(GLFW_KEY_A)) camCtrl.fpsPosition -= right * spd;
    if (input.isKeyPressed(GLFW_KEY_D)) camCtrl.fpsPosition += right * spd;
    if (input.isKeyPressed(GLFW_KEY_SPACE)) camCtrl.fpsPosition.y += spd;
    if (input.isKeyPressed(GLFW_KEY_LEFT_SHIFT) || input.isKeyPressed(GLFW_KEY_RIGHT_SHIFT)) camCtrl.fpsPosition.y -= spd;
    transform->position = camCtrl.fpsPosition;
  }
}

glm::mat4 Application::getActiveCameraViewMatrix() const {
  if (camCtrl.mode == CameraMode::ORBIT)
    return glm::lookAt(camCtrl.lastOrbitPosition, camCtrl.orbitPivot, glm::vec3(0,1,0));
  const glm::vec3 fwd = glm::normalize(glm::vec3(
      cos(glm::radians(camCtrl.fpsYaw)) * cos(glm::radians(camCtrl.fpsPitch)),
      sin(glm::radians(camCtrl.fpsPitch)),
      sin(glm::radians(camCtrl.fpsYaw)) * cos(glm::radians(camCtrl.fpsPitch))));
  return glm::lookAt(camCtrl.fpsPosition, camCtrl.fpsPosition + fwd, glm::vec3(0,1,0));
}

glm::vec3 Application::getActiveCameraPosition() const {
  return (camCtrl.mode == CameraMode::ORBIT) ? camCtrl.lastOrbitPosition : camCtrl.fpsPosition;
}

void Application::resetApplication() {
  if (!scenecameras.empty()) switchToCamera(0);
  simState.currentTime = 0.0f;
  simState.timeHistory.clear();
  simState.historyIndex = -1;
  simState.rewinding = false;
}

void Application::cursorPosCallback(GLFWwindow* win, double xpos, double ypos) {
  auto* const app =
      reinterpret_cast<Application*>(glfwGetWindowUserPointer(win));
  app->input.onMouseMove(xpos, ypos);
}

void Application::mouseButtonCallback(GLFWwindow* win, int button, int action,
                                      int mods) {
  auto* const app =
      reinterpret_cast<Application*>(glfwGetWindowUserPointer(win));
  app->input.onMouseButton(button, action, mods);
}

void Application::scrollCallback(GLFWwindow* win, double xoffset,
                                 double yoffset) {
  auto* const app =
      reinterpret_cast<Application*>(glfwGetWindowUserPointer(win));
  app->input.onScroll(xoffset, yoffset);
}

void Application::createDefaultPipelineConfig(
    PipelineConfigInfo& configInfo) const {
  RenderUtils::createInputAssemblyState(configInfo.inputAssembly,
                                        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

  configInfo.viewportState = {};
  configInfo.viewportState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  configInfo.viewportState.viewportCount = 1;
  configInfo.viewportState.scissorCount = 1;

  configInfo.rasterizer = {};
  configInfo.rasterizer.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  configInfo.rasterizer.depthClampEnable = VK_FALSE;
  configInfo.rasterizer.rasterizerDiscardEnable = VK_FALSE;
  configInfo.rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  configInfo.rasterizer.lineWidth = 1.0f;
  configInfo.rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  configInfo.rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  configInfo.rasterizer.depthBiasEnable = VK_FALSE;

  configInfo.multisampling = {};
  configInfo.multisampling.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  configInfo.multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  configInfo.multisampling.minSampleShading = 1.0f;

  configInfo.depthStencil = {};
  configInfo.depthStencil.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  configInfo.depthStencil.depthTestEnable = VK_TRUE;
  configInfo.depthStencil.depthWriteEnable = VK_TRUE;
  configInfo.depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

  RenderUtils::createColorBlendAttachment(configInfo.colorBlendAttachment);

  configInfo.colorBlending = {};
  configInfo.colorBlending.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  configInfo.colorBlending.logicOpEnable = VK_FALSE;
  configInfo.colorBlending.attachmentCount = 1;
  configInfo.colorBlending.pAttachments = &configInfo.colorBlendAttachment;

  configInfo.dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                              VK_DYNAMIC_STATE_SCISSOR};
  configInfo.dynamicState = {};
  configInfo.dynamicState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  configInfo.dynamicState.dynamicStateCount =
      static_cast<uint32_t>(configInfo.dynamicStates.size());
  configInfo.dynamicState.pDynamicStates = configInfo.dynamicStates.data();
}

void Application::getShaderStages(
    const std::string& vertPath, const std::string& fragPath,
    VkShaderModule& vertModule, VkShaderModule& fragModule,
    std::array<VkPipelineShaderStageCreateInfo, 2>& stages) const {
  std::vector<char> vertShaderCode;
  RenderUtils::readFile(vertPath, vertShaderCode);
  std::vector<char> fragShaderCode;
  RenderUtils::readFile(fragPath, fragShaderCode);
  vertModule = RenderUtils::createShaderModule(device, vertShaderCode);
  fragModule = RenderUtils::createShaderModule(device, fragShaderCode);

  VkPipelineShaderStageCreateInfo vertStage{};
  vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertStage.module = vertModule;
  vertStage.pName = RenderUtils::ENTRY_POINT_MAIN;

  VkPipelineShaderStageCreateInfo fragStage{};
  fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragStage.module = fragModule;
  fragStage.pName = RenderUtils::ENTRY_POINT_MAIN;

  stages = {vertStage, fragStage};
}

void Application::setupRenderingCreateInfo(
    VkPipelineRenderingCreateInfo& createInfo) const {
  createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  createInfo.pNext = nullptr;
  createInfo.colorAttachmentCount = 1;
  createInfo.pColorAttachmentFormats = &swapChainImageFormat;
  createInfo.depthAttachmentFormat = depthFormat;
  createInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
  createInfo.viewMask = 0;
}

void Application::createShadowPipeline() {
  VkShaderModule vertShaderModule = VK_NULL_HANDLE;
  VkShaderModule fragShaderModule = VK_NULL_HANDLE;
  std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;
  getShaderStages("shaders/shadow_vert.spv", "shaders/shadow_frag.spv",
                  vertShaderModule, fragShaderModule, shaderStages);
  PipelineConfigInfo configInfo{};
  createDefaultPipelineConfig(configInfo);
  configInfo.rasterizer.depthBiasEnable = VK_TRUE;
  configInfo.depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  configInfo.colorBlending.attachmentCount = 0;
  configInfo.dynamicStates.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
  configInfo.dynamicState.dynamicStateCount =
      static_cast<uint32_t>(configInfo.dynamicStates.size());
  configInfo.dynamicState.pDynamicStates = configInfo.dynamicStates.data();
  const auto bindingDescription = Vertex::getBindingDescription();
  const auto attributeDescriptions = Vertex::getAttributeDescriptions();

  configInfo.vertexInputInfo = {};
  configInfo.vertexInputInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  configInfo.vertexInputInfo.vertexBindingDescriptionCount = 1;
  configInfo.vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
  configInfo.vertexInputInfo.vertexAttributeDescriptionCount = 1;
  configInfo.vertexInputInfo.pVertexAttributeDescriptions =
      &attributeDescriptions[0];

  struct ShadowPushConstants {
    glm::mat4 lightSpaceMatrix;
    glm::mat4 model;
  };
  VkPushConstantRange pushConstantRange{VK_SHADER_STAGE_VERTEX_BIT, 0,
                                        sizeof(ShadowPushConstants)};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

  if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr,
                             &shadowPipelineLayout) != VK_SUCCESS)
    throw std::runtime_error("Failed to create shadow pipeline layout!");
  const VkFormat shadowDepthFormat = VK_FORMAT_D32_SFLOAT;

  VkPipelineRenderingCreateInfo renderingCreateInfo{};
  renderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  renderingCreateInfo.depthAttachmentFormat = shadowDepthFormat;

  RenderUtils::createGraphicsPipeline(
      device, shadowPipelineLayout, VK_NULL_HANDLE, 2, shaderStages.data(),
      &configInfo.vertexInputInfo, &configInfo.inputAssembly,
      &configInfo.viewportState, &configInfo.rasterizer,
      &configInfo.multisampling, &configInfo.depthStencil,
      &configInfo.colorBlending, &configInfo.dynamicState, &shadowPipeline,
      &renderingCreateInfo);
  vkDestroyShaderModule(device, fragShaderModule, nullptr);
  vkDestroyShaderModule(device, vertShaderModule, nullptr);
}

void Application::setupViewportScissor(VkCommandBuffer commandBuffer,
                                       float width, float height) const {
  VkViewport viewport{0.0f, 0.0f, width, height, 0.0f, 1.0f};
  vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
  VkRect2D scissor{
      {0, 0}, {static_cast<uint32_t>(width), static_cast<uint32_t>(height)}};
  vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

void Application::recordCommandBuffer(VkCommandBuffer commandBuffer,
                                      uint32_t imageIndex) {
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
    throw std::runtime_error("Failed to begin recording command buffer!");

  if (skipSceneRendering) {
    // Performance bake: skip all scene/shadow/postprocessing work.
    // Just transition the swapchain image and let ImGui draw into it.
    VkImageMemoryBarrier2 toColorBarrier{};
    toColorBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toColorBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    toColorBarrier.srcAccessMask = 0;
    toColorBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toColorBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toColorBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toColorBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toColorBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toColorBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toColorBarrier.image = swapChainImages[imageIndex];
    toColorBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo depInfo{};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &toColorBarrier;
    vkCmdPipelineBarrier2(commandBuffer, &depInfo);

    interface->draw(commandBuffer, imageIndex);

    toColorBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toColorBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toColorBarrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    toColorBarrier.dstAccessMask = 0;
    toColorBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toColorBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier2(commandBuffer, &depInfo);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
      throw std::runtime_error("Failed to record command buffer!");
    return;
  }

  std::vector<ShadowMapData> shadowMaps;
  lightManager->getShadowSystem()->getShadowMaps(shadowMaps);

  for (size_t smIdx = 0; smIdx < shadowMaps.size(); smIdx++) {
    const auto& shadowMap = shadowMaps[smIdx];

    VkImageMemoryBarrier2 toWriteBarrier{};
    toWriteBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toWriteBarrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    toWriteBarrier.srcAccessMask = VK_ACCESS_2_NONE;
    toWriteBarrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                  VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    toWriteBarrier.dstAccessMask =
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    toWriteBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toWriteBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    toWriteBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toWriteBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toWriteBarrier.image = shadowMap.image;
    toWriteBarrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};

    VkDependencyInfo toWriteDep{};
    toWriteDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    toWriteDep.imageMemoryBarrierCount = 1;
    toWriteDep.pImageMemoryBarriers = &toWriteBarrier;

    vkCmdPipelineBarrier2(commandBuffer, &toWriteDep);

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = shadowMap.imageView;
    depthAttachment.imageLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = {1.0f, 0};

    const int shadowMapSize = 16384;
    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = {{0, 0},
                                {static_cast<uint32_t>(shadowMapSize),
                                 static_cast<uint32_t>(shadowMapSize)}};
    renderingInfo.layerCount = 1;
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(commandBuffer, &renderingInfo);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      shadowPipeline);
    setupViewportScissor(commandBuffer, static_cast<float>(shadowMapSize),
                         static_cast<float>(shadowMapSize));
    vkCmdSetDepthBias(commandBuffer, 0.0f, 0.0f, 0.0f);

    struct ShadowPushConstants {
      glm::mat4 lightSpaceMatrix;
      glm::mat4 model;
    } shadowPush;
    shadowPush.lightSpaceMatrix = shadowMap.lightSpaceMatrix;

    // Frustum-cull entities against the light's clip frustum so we only
    // render geometry that can actually cast a shadow into this shadow map.
    const Frustum lightFrustum = extractFrustum(shadowMap.lightSpaceMatrix);

    for (const auto& proxy : renderProxies) {
      if (!proxy.visible || proxy.meshID == INVALID_MESH_ID) continue;
      const Mesh* const mesh = meshManager->getMesh(proxy.meshID);
      if (!mesh || mesh->getVertexBuffer() == VK_NULL_HANDLE) continue;

      if (!sphereInFrustum(lightFrustum, proxy.position,
                           mesh->getBoundingRadius() * proxy.maxAbsScale))
        continue;

      shadowPush.model = proxy.modelMatrix;
      vkCmdPushConstants(commandBuffer, shadowPipelineLayout,
                         VK_SHADER_STAGE_VERTEX_BIT, 0,
                         sizeof(ShadowPushConstants), &shadowPush);
      VkBuffer vertexBuffers[] = {mesh->getVertexBuffer()};
      VkDeviceSize offsets[] = {0};
      vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
      vkCmdBindIndexBuffer(commandBuffer, mesh->getIndexBuffer(), 0,
                           VK_INDEX_TYPE_UINT16);
      vkCmdDrawIndexed(commandBuffer, mesh->getIndexCount(), 1, 0, 0, 0);
    }
    vkCmdEndRendering(commandBuffer);

    VkImageMemoryBarrier2 toReadBarrier{};
    toReadBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toReadBarrier.srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    toReadBarrier.srcAccessMask =
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    toReadBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    toReadBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    toReadBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    toReadBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    toReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toReadBarrier.image = shadowMap.image;
    toReadBarrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};

    VkDependencyInfo toReadDep{};
    toReadDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    toReadDep.imageMemoryBarrierCount = 1;
    toReadDep.pImageMemoryBarriers = &toReadBarrier;

    vkCmdPipelineBarrier2(commandBuffer, &toReadDep);
  }

  postProcessing->beginOffscreenPass(commandBuffer, swapChainExtent,
                                     sceneSettings.clearColor);
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    mainPipeline->getPipeline());
  setupViewportScissor(commandBuffer, static_cast<float>(swapChainExtent.width),
                       static_cast<float>(swapChainExtent.height));

  // -----------------------------------------------------------------------
  // Instanced batching + frustum culling
  // -----------------------------------------------------------------------
  // Group entities by (materialID, meshID).  After culling, each group is
  // drawn with a single vkCmdDrawIndexed(instanceCount > 1) reading per-
  // instance data from a pre-uploaded SSBO at descriptor set=3.
  // -----------------------------------------------------------------------

  struct BatchKey {
    RenderMaterialID matID;
    MeshID meshID;
    bool operator==(const BatchKey& o) const {
      return matID == o.matID && meshID == o.meshID;
    }
  };
  struct BatchKeyHash {
    size_t operator()(const BatchKey& k) const {
      return std::hash<uint32_t>{}(k.matID) ^
             (std::hash<uint32_t>{}(k.meshID) * 2654435761u);
    }
  };

  const Frustum cameraFrustum = extractFrustum(currentViewProj);
  const Entity selEntity =
      interface ? interface->getSelectedEntity() : INVALID_ENTITY;
  const Entity hovEntity =
      interface ? interface->getHoveredEntity() : INVALID_ENTITY;

  std::unordered_map<BatchKey, std::vector<InstanceData>, BatchKeyHash> batchMap;
  batchMap.reserve(64);

  for (const auto& proxy : renderProxies) {
    if (!proxy.visible || proxy.meshID == INVALID_MESH_ID ||
        proxy.matID == INVALID_RENDER_MATERIAL_ID)
      continue;
    const Mesh* mesh = meshManager->getMesh(proxy.meshID);
    const RenderMaterial* material = materialManager->getMaterial(proxy.matID);
    if (!mesh || mesh->getVertexBuffer() == VK_NULL_HANDLE || !material ||
        material->getDescriptorSet() == VK_NULL_HANDLE)
      continue;

    if (!sphereInFrustum(cameraFrustum, proxy.position,
                         mesh->getBoundingRadius() * proxy.maxAbsScale))
      continue;

    InstanceData inst{};
    inst.model       = proxy.modelMatrix;
    inst.layerMask   = proxy.layerMask;
    inst.cameraLayer = 0xFFFFFFFF;
    inst.highlightIntensity = (proxy.entity == selEntity)   ? 0.25f
                              : (proxy.entity == hovEntity) ? 0.12f
                                                            : 0.0f;
    batchMap[{proxy.matID, proxy.meshID}].push_back(inst);
  }

  // Append gizmo instances (point-light sphere overlays)
  if (interface && gizmoMeshID != INVALID_MESH_ID) {
    const Mesh* gizmoMesh = meshManager->getMesh(gizmoMeshID);
    const RenderMaterial* gizmoMat =
        materialManager->getMaterial(gizmoRenderMaterialID);
    if (gizmoMesh && gizmoMesh->getVertexBuffer() != VK_NULL_HANDLE &&
        gizmoMat && gizmoMat->getDescriptorSet() != VK_NULL_HANDLE) {
      const bool showAll = interface->getShowLightGizmos();
      // addGizmo uses proxy.position (captured in buildRenderProxies)
      // so no registry access is needed here.
      auto addGizmo = [&](const RenderProxy& proxy, float highlight) {
        InstanceData inst{};
        inst.model = glm::translate(glm::mat4(1.0f), proxy.position);
        inst.layerMask = 0xFFFFFFFF;
        inst.cameraLayer = 0xFFFFFFFF;
        inst.highlightIntensity = highlight;
        batchMap[{gizmoRenderMaterialID, gizmoMeshID}].push_back(inst);
      };
      if (showAll) {
        for (const auto& proxy : renderProxies) {
          if (!proxy.isPointLight) continue;
          addGizmo(proxy, (proxy.entity == selEntity)   ? 0.4f
                          : (proxy.entity == hovEntity) ? 0.25f
                                                        : 0.15f);
        }
      } else {
        Entity gizmoEntity = selEntity;
        if (gizmoEntity == INVALID_ENTITY) gizmoEntity = hovEntity;
        if (gizmoEntity != INVALID_ENTITY) {
          // Find the proxy for the selected/hovered entity
          auto it = std::find_if(renderProxies.begin(), renderProxies.end(),
                                 [gizmoEntity](const RenderProxy& p) {
                                   return p.entity == gizmoEntity;
                                 });
          if (it != renderProxies.end() && it->isPointLight)
            addGizmo(*it, 0.4f);
        }
      }
    }
  }

  // Flatten batches into a linear SSBO buffer and build the draw list
  struct DrawBatch {
    RenderMaterialID matID;
    MeshID meshID;
    uint32_t firstInstance;
    uint32_t instanceCount;
  };
  std::vector<InstanceData> allInstances;
  std::vector<DrawBatch> drawBatches;
  allInstances.reserve(std::min<size_t>(renderProxies.size() + 16, MAX_INSTANCES));
  drawBatches.reserve(batchMap.size());

  for (auto& [key, insts] : batchMap) {
    if (allInstances.size() + insts.size() > MAX_INSTANCES) break;
    drawBatches.push_back({key.matID, key.meshID,
                           static_cast<uint32_t>(allInstances.size()),
                           static_cast<uint32_t>(insts.size())});
    allInstances.insert(allInstances.end(), insts.begin(), insts.end());
  }

  // Sort by material to minimise vkCmdBindDescriptorSets calls
  std::sort(drawBatches.begin(), drawBatches.end(),
            [](const DrawBatch& a, const DrawBatch& b) {
              return a.matID != b.matID ? a.matID < b.matID
                                       : a.meshID < b.meshID;
            });

  // Upload all instance transforms to the per-frame SSBO
  if (!allInstances.empty()) {
    memcpy(instanceSSBOMapped[currentFrame], allInstances.data(),
           sizeof(InstanceData) * allInstances.size());
  }

  // Bind sets that don't change within this pass (set=0, set=2, set=3)
  const VkPipelineLayout mainLayout = mainPipeline->getPipelineLayout();
  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          mainLayout, 0, 1, &descriptorSets[currentFrame], 0,
                          nullptr);
  {
    VkDescriptorSet shadowDS = lightManager->getShadowDescriptorSet();
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            mainLayout, 2, 1, &shadowDS, 0, nullptr);
  }
  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          mainLayout, 3, 1,
                          &instanceDescriptorSets[currentFrame], 0, nullptr);

  // Issue one draw call per (material, mesh) batch
  RenderMaterialID lastMat = INVALID_RENDER_MATERIAL_ID;
  MeshID lastMesh = INVALID_MESH_ID;
  for (const DrawBatch& batch : drawBatches) {
    if (batch.matID != lastMat) {
      const RenderMaterial* mat = materialManager->getMaterial(batch.matID);
      VkDescriptorSet matDS = mat->getDescriptorSet();
      vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              mainLayout, 1, 1, &matDS, 0, nullptr);
      lastMat = batch.matID;
    }
    if (batch.meshID != lastMesh) {
      const Mesh* mesh = meshManager->getMesh(batch.meshID);
      VkBuffer vb[] = {mesh->getVertexBuffer()};
      VkDeviceSize off[] = {0};
      vkCmdBindVertexBuffers(commandBuffer, 0, 1, vb, off);
      vkCmdBindIndexBuffer(commandBuffer, mesh->getIndexBuffer(), 0,
                           VK_INDEX_TYPE_UINT16);
      lastMesh = batch.meshID;
    }
    vkCmdDrawIndexed(commandBuffer,
                     meshManager->getMesh(batch.meshID)->getIndexCount(),
                     batch.instanceCount, 0, 0, batch.firstInstance);
  }

  postProcessing->endOffscreenPass(commandBuffer);

  VkImageMemoryBarrier2 swapchainBarrier{};
  swapchainBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  swapchainBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
  swapchainBarrier.srcAccessMask = 0;
  swapchainBarrier.dstStageMask =
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  swapchainBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
  swapchainBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  swapchainBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  swapchainBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  swapchainBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  swapchainBarrier.image = swapChainImages[imageIndex];
  swapchainBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

  VkDependencyInfo dependencyInfo{};
  dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dependencyInfo.imageMemoryBarrierCount = 1;
  dependencyInfo.pImageMemoryBarriers = &swapchainBarrier;
  vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

  postProcessing->render(commandBuffer, swapChainImageViews[imageIndex],
                         swapChainExtent, currentFrame);

  interface->draw(commandBuffer, imageIndex);

  swapchainBarrier.srcStageMask =
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  swapchainBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
  swapchainBarrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
  swapchainBarrier.dstAccessMask = 0;
  swapchainBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  swapchainBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

  if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
    throw std::runtime_error("Failed to record command buffer!");
}

// ============================================================================
// Owner-colour material system
// ============================================================================

void Application::initOwnerMaterials() {
  if (!materialManager) return;
  if (ownerRenderMaterialIDs[0] != INVALID_RENDER_MATERIAL_ID) return; // already created

  // Peer colours: Red, Green, Blue, Yellow
  static const glm::vec3 colors[4] = {
      {0.85f, 0.20f, 0.20f}, // Peer 1 – Red
      {0.20f, 0.80f, 0.25f}, // Peer 2 – Green
      {0.20f, 0.35f, 0.90f}, // Peer 3 – Blue
      {0.90f, 0.85f, 0.15f}, // Peer 4 – Yellow
  };
  static const char* names[4] = {
      "_net_owner_peer1", "_net_owner_peer2",
      "_net_owner_peer3", "_net_owner_peer4"};

  for (int i = 0; i < 4; ++i) {
    RenderMaterialBuilder b;
    b.name(names[i]).albedoColor(colors[i]).roughness(0.7f);
    ownerRenderMaterialIDs[i] = materialManager->registerMaterial(b);
  }
}

void Application::applyOwnerColors(bool enable) {
  if (!registry || !materialManager || !networkManager) return;

  if (enable) {
    initOwnerMaterials();

    for (const auto& [e, _] : registry->allSimulated()) {
      uint8_t ownerID = networkManager->getOwnerPeerID(e);
      if (ownerID == 0 || ownerID > 4) continue;
      auto* matComp = registry->getComponent<RenderMaterialComponent>(e);
      if (!matComp) continue;

      if (savedRenderMaterialIDs.find(e) == savedRenderMaterialIDs.end())
        savedRenderMaterialIDs[e] = matComp->renderMaterialID;

      matComp->renderMaterialID = ownerRenderMaterialIDs[ownerID - 1];
    }
  } else {
    // Restore original materials
    for (auto& [e, origID] : savedRenderMaterialIDs) {
      auto* matComp = registry->getComponent<RenderMaterialComponent>(e);
      if (matComp) matComp->renderMaterialID = origID;
    }
    savedRenderMaterialIDs.clear();
  }
}