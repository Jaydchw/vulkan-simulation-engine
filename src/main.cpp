#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "Application.h"
#include "Util/ConfigParser.h"
#include "Util/Debug.h"
#include "Util/WorldParser.h"

int main() {
#ifdef _DEBUG
  _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
  Debug::setEnabled(Debug::Category::MAIN, true);
  Debug::setEnabled(Debug::Category::VULKAN, true);
  Debug::setEnabled(Debug::Category::RENDERING, true);
  Debug::setEnabled(Debug::Category::SCENE_LOADER, true);
  Debug::setEnabled(Debug::Category::CONFIG, true);
  std::cout << R"(
CONTROLS
  ESC           Exit
  Space         Pause / Resume
  .             Step Forward
  ,             Step Backward
  R             Restart Simulation
  + / -         Double / Half Speed
  L             Toggle Shading Mode
  K             Toggle Toon Shader
  Tab           Cycle Render Mode
  G             Toggle Wireframe
  F1            Toggle FPS Display
  F11           Toggle Fullscreen
  Enter         Toggle Orbit / FPS Camera
  W/A/S/D       Move Camera (FPS)
  Space/Shift   Camera Up / Down (FPS)
  Right Click   Orbit Camera (Orbit)
  Scroll        Zoom / Speed
  Ctrl+Arrows   Pan Camera
  1-9           Switch Camera
)" << std::endl;
#endif
  try {
    Application app;
    Debug::log(Debug::Category::MAIN,
               "Initializing Vulkan Simulation Engine...");
    app.init();

    auto worlds = WorldParser::listWorlds("Scenes/Worlds");
    if (!worlds.empty()) {
      std::string toLoad = worlds[0];
      for (const auto& w : worlds) {
        auto stem = std::filesystem::path(w).stem().string();
        if (stem == "default") { toLoad = w; break; }
      }
      app.loadWorld(toLoad);
    }
    app.getLightManager()->debugPrintLightInfo();

    Debug::log(Debug::Category::MAIN, "Starting main loop...");
    app.run();
  } catch (const std::exception& e [[maybe_unused]]) {
#ifdef _DEBUG
    std::cerr << e.what() << std::endl;
#endif
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}