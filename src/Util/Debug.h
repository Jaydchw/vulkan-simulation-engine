#pragma once
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

class Debug final {
 public:
  enum class Verbosity { LOW = 0, MEDIUM = 1, HIGH = 2, TRACE = 3 };

  enum class Category {
    MAIN = 0,
    APP_LIFECYCLE,
    CONFIG,
    ECS,
    CAMERA,
    CAMERA_VERBOSE,
    INPUT,
    INPUT_VERBOSE,
    UI,
    UI_LAYOUT,
    RENDERING,
    RENDERING_FRAME,
    RENDERING_CULLING,
    RENDERING_BATCH,
    RENDERING_SYNC,
    VULKAN,
    VULKAN_MEMORY,
    VULKAN_SWAPCHAIN,
    VULKAN_PIPELINE,
    VULKAN_DESCRIPTORS,
    VULKAN_COMMANDS,
    SKYBOX,
    WORLD,
    SCENE,
    SCENE_LOADER,
    GIZMO,
    MESH,
    LIGHTS,
    OBJECTS,
    TEXTURE,
    MATERIALS,
    POSTPROCESSING,
    RESOURCE_IO,
    SHADOWS,
    PHYSICS,
    PHYSICS_SYNC,
    PHYSICS_COLLISION,
    NETWORK,
    NETWORK_DISCOVERY,
    NETWORK_PACKETS,
    NETWORK_OWNERSHIP,
    NETWORK_SYNC,
    ANIMATION,
    SPAWNING,
    TIMELINE,
    PLANTMANAGER,
    PARTICLES,
    THREADING,
    PERFORMANCE,
    FILE_IO,
    COUNT
  };

  static constexpr int CATEGORY_COUNT = static_cast<int>(Category::COUNT);

  static void setRuntimeEnabled(bool enabled) {
    ensureInitialized();
    runtimeEnabled.store(enabled, std::memory_order_relaxed);
    if (enabled) {
      startWorker();
    } else {
      stopWorker();
    }
  }

  static bool isRuntimeEnabled() {
    ensureInitialized();
    return runtimeEnabled.load(std::memory_order_relaxed);
  }

  static void setEnabled(Category c, bool v) {
    ensureInitialized();
    flags[static_cast<size_t>(c)].store(v, std::memory_order_relaxed);
  }

  static void setAllEnabled(bool v) {
    ensureInitialized();
    for (auto& flag : flags) {
      flag.store(v, std::memory_order_relaxed);
    }
  }

  static bool isEnabled(Category c) {
    ensureInitialized();
    return flags[static_cast<size_t>(c)].load(std::memory_order_relaxed);
  }

  static void setVerbosity(Verbosity v) {
    ensureInitialized();
    currentVerbosity.store(static_cast<int>(v), std::memory_order_relaxed);
  }

  static Verbosity getVerbosity() {
    ensureInitialized();
    return static_cast<Verbosity>(currentVerbosity.load(std::memory_order_relaxed));
  }

  template <typename... Args>
  static void log(Category category, Args&&... args) {
    write(category, Verbosity::LOW, "", std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void logVerbose(Category category, Args&&... args) {
    write(category, Verbosity::MEDIUM, "V", std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void logTrace(Category category, Args&&... args) {
    write(category, Verbosity::TRACE, "T", std::forward<Args>(args)...);
  }

  static const char* verbosityName(Verbosity v) {
    switch (v) {
      case Verbosity::LOW: return "Low";
      case Verbosity::MEDIUM: return "Medium";
      case Verbosity::HIGH: return "High";
      case Verbosity::TRACE: return "Trace";
      default: return "Low";
    }
  }

  static const char* categoryName(Category cat) {
    switch (cat) {
      case Category::MAIN:               return "MAIN";
      case Category::APP_LIFECYCLE:      return "APP_LIFECYCLE";
      case Category::CONFIG:             return "CONFIG";
      case Category::ECS:                return "ECS";
      case Category::CAMERA:             return "CAMERA";
      case Category::CAMERA_VERBOSE:     return "CAMERA_VERBOSE";
      case Category::INPUT:              return "INPUT";
      case Category::INPUT_VERBOSE:      return "INPUT_VERBOSE";
      case Category::UI:                 return "UI";
      case Category::UI_LAYOUT:          return "UI_LAYOUT";
      case Category::RENDERING:          return "RENDERING";
      case Category::RENDERING_FRAME:    return "RENDERING_FRAME";
      case Category::RENDERING_CULLING:  return "RENDERING_CULLING";
      case Category::RENDERING_BATCH:    return "RENDERING_BATCH";
      case Category::RENDERING_SYNC:     return "RENDERING_SYNC";
      case Category::VULKAN:             return "VULKAN";
      case Category::VULKAN_MEMORY:      return "VULKAN_MEMORY";
      case Category::VULKAN_SWAPCHAIN:   return "VULKAN_SWAPCHAIN";
      case Category::VULKAN_PIPELINE:    return "VULKAN_PIPELINE";
      case Category::VULKAN_DESCRIPTORS: return "VULKAN_DESCRIPTORS";
      case Category::VULKAN_COMMANDS:    return "VULKAN_COMMANDS";
      case Category::SKYBOX:             return "SKYBOX";
      case Category::WORLD:              return "WORLD";
      case Category::SCENE:              return "SCENE";
      case Category::SCENE_LOADER:       return "SCENE_LOADER";
      case Category::GIZMO:              return "GIZMO";
      case Category::MESH:               return "MESH";
      case Category::LIGHTS:             return "LIGHTS";
      case Category::OBJECTS:            return "OBJECTS";
      case Category::TEXTURE:            return "TEXTURE";
      case Category::MATERIALS:          return "MATERIALS";
      case Category::POSTPROCESSING:     return "POSTPROCESSING";
      case Category::RESOURCE_IO:        return "RESOURCE_IO";
      case Category::SHADOWS:            return "SHADOWS";
      case Category::PHYSICS:            return "PHYSICS";
      case Category::PHYSICS_SYNC:       return "PHYSICS_SYNC";
      case Category::PHYSICS_COLLISION:  return "PHYSICS_COLLISION";
      case Category::NETWORK:            return "NETWORK";
      case Category::NETWORK_DISCOVERY:  return "NETWORK_DISCOVERY";
      case Category::NETWORK_PACKETS:    return "NETWORK_PACKETS";
      case Category::NETWORK_OWNERSHIP:  return "NETWORK_OWNERSHIP";
      case Category::NETWORK_SYNC:       return "NETWORK_SYNC";
      case Category::ANIMATION:          return "ANIMATION";
      case Category::SPAWNING:           return "SPAWNING";
      case Category::TIMELINE:           return "TIMELINE";
      case Category::PLANTMANAGER:       return "PLANTMANAGER";
      case Category::PARTICLES:          return "PARTICLES";
      case Category::THREADING:          return "THREADING";
      case Category::PERFORMANCE:        return "PERFORMANCE";
      case Category::FILE_IO:            return "FILE_IO";
      default:                           return "UNKNOWN";
    }
  }

  static const char* categoryDisplayName(Category cat) {
    switch (cat) {
      case Category::MAIN:               return "Main";
      case Category::APP_LIFECYCLE:      return "App Lifecycle";
      case Category::CONFIG:             return "Config";
      case Category::ECS:                return "ECS";
      case Category::CAMERA:             return "Camera";
      case Category::CAMERA_VERBOSE:     return "Camera (Verbose)";
      case Category::INPUT:              return "Input";
      case Category::INPUT_VERBOSE:      return "Input (Verbose)";
      case Category::UI:                 return "UI";
      case Category::UI_LAYOUT:          return "UI Layout";
      case Category::RENDERING:          return "Rendering";
      case Category::RENDERING_FRAME:    return "Rendering Frame";
      case Category::RENDERING_CULLING:  return "Rendering Culling";
      case Category::RENDERING_BATCH:    return "Rendering Batch";
      case Category::RENDERING_SYNC:     return "Rendering Sync";
      case Category::VULKAN:             return "Vulkan";
      case Category::VULKAN_MEMORY:      return "Vulkan Memory";
      case Category::VULKAN_SWAPCHAIN:   return "Vulkan Swapchain";
      case Category::VULKAN_PIPELINE:    return "Vulkan Pipeline";
      case Category::VULKAN_DESCRIPTORS: return "Vulkan Descriptors";
      case Category::VULKAN_COMMANDS:    return "Vulkan Commands";
      case Category::SKYBOX:             return "Skybox";
      case Category::WORLD:              return "World";
      case Category::SCENE:              return "Scene";
      case Category::SCENE_LOADER:       return "Scene Loader";
      case Category::GIZMO:              return "Gizmo";
      case Category::MESH:               return "Mesh";
      case Category::LIGHTS:             return "Lights";
      case Category::OBJECTS:            return "Objects";
      case Category::TEXTURE:            return "Texture";
      case Category::MATERIALS:          return "Materials";
      case Category::POSTPROCESSING:     return "Post Processing";
      case Category::RESOURCE_IO:        return "Resource IO";
      case Category::SHADOWS:            return "Shadows";
      case Category::PHYSICS:            return "Physics";
      case Category::PHYSICS_SYNC:       return "Physics Sync";
      case Category::PHYSICS_COLLISION:  return "Physics Collision";
      case Category::NETWORK:            return "Network";
      case Category::NETWORK_DISCOVERY:  return "Network Discovery";
      case Category::NETWORK_PACKETS:    return "Network Packets";
      case Category::NETWORK_OWNERSHIP:  return "Network Ownership";
      case Category::NETWORK_SYNC:       return "Network Sync";
      case Category::ANIMATION:          return "Animation";
      case Category::SPAWNING:           return "Spawning";
      case Category::TIMELINE:           return "Timeline";
      case Category::PLANTMANAGER:       return "Plant Manager";
      case Category::PARTICLES:          return "Particles";
      case Category::THREADING:          return "Threading";
      case Category::PERFORMANCE:        return "Performance";
      case Category::FILE_IO:            return "File IO";
      default:                           return "Unknown";
    }
  }

 private:
  static void ensureInitialized() {
    std::call_once(initOnce, [] {
      for (auto& flag : flags) {
        flag.store(true, std::memory_order_relaxed);
      }
#ifdef _DEBUG
      runtimeEnabled.store(true, std::memory_order_relaxed);
#else
      runtimeEnabled.store(false, std::memory_order_relaxed);
#endif
      if (runtimeEnabled.load(std::memory_order_relaxed)) {
        startWorker();
      }
    });
  }

  static void startWorker() {
    std::lock_guard<std::mutex> lock(workerMutex);
    if (workerRunning.load(std::memory_order_relaxed)) return;
    stopRequested.store(false, std::memory_order_relaxed);
    worker = std::thread(&Debug::workerLoop);
    workerRunning.store(true, std::memory_order_relaxed);
  }

  static void stopWorker() {
    std::thread joinThread;
    {
      std::lock_guard<std::mutex> lock(workerMutex);
      if (!workerRunning.load(std::memory_order_relaxed)) return;
      stopRequested.store(true, std::memory_order_relaxed);
      queueCv.notify_all();
      joinThread = std::move(worker);
      workerRunning.store(false, std::memory_order_relaxed);
    }
    if (joinThread.joinable()) {
      joinThread.join();
    }
    std::lock_guard<std::mutex> queueLock(queueMutex);
    queue.clear();
  }

  static void workerLoop() {
    while (true) {
      std::string line;
      uint64_t dropped = 0;
      {
        std::unique_lock<std::mutex> lock(queueMutex);
        queueCv.wait(lock, [] {
          return stopRequested.load(std::memory_order_relaxed) || !queue.empty();
        });
        if (stopRequested.load(std::memory_order_relaxed) && queue.empty()) {
          break;
        }
        line = std::move(queue.front());
        queue.pop_front();
        dropped = droppedCount.exchange(0, std::memory_order_relaxed);
      }
      if (dropped > 0) {
        std::cout << "[DEBUG][WARN] Dropped " << dropped
                  << " log messages (queue full)" << '\n';
      }
      std::cout << line << '\n';
    }
  }

  template <typename... Args>
  static void write(Category category, Verbosity requiredVerbosity,
                    const char* suffix, Args&&... args) {
    ensureInitialized();
    if (!runtimeEnabled.load(std::memory_order_relaxed)) return;
    if (!flags[static_cast<size_t>(category)].load(std::memory_order_relaxed)) return;
    if (static_cast<int>(requiredVerbosity) >
        currentVerbosity.load(std::memory_order_relaxed)) return;

    std::ostringstream oss;
    oss << "[" << categoryName(category) << "]";
    if (suffix && suffix[0] != '\0') oss << "[" << suffix << "]";
    oss << " ";
    (oss << ... << args);

    std::lock_guard<std::mutex> lock(queueMutex);
    if (queue.size() >= MAX_QUEUED_LINES) {
      droppedCount.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    queue.push_back(oss.str());
    queueCv.notify_one();
  }

  static constexpr size_t MAX_QUEUED_LINES = 8192;

  static inline std::once_flag initOnce;
  static inline std::array<std::atomic_bool, CATEGORY_COUNT> flags{};
  static inline std::atomic_int currentVerbosity{static_cast<int>(Verbosity::LOW)};
  static inline std::atomic_bool runtimeEnabled{false};
  static inline std::atomic_bool stopRequested{false};
  static inline std::atomic_bool workerRunning{false};

  static inline std::mutex workerMutex;
  static inline std::thread worker;
  static inline std::mutex queueMutex;
  static inline std::condition_variable queueCv;
  static inline std::deque<std::string> queue;
  static inline std::atomic_uint64_t droppedCount{0};

  struct ShutdownGuard {
    ~ShutdownGuard() { Debug::stopWorker(); }
  };
  static inline ShutdownGuard shutdownGuard;
};
