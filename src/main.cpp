#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif
#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "Application.h"
#include "Util/ConfigParser.h"
#include "Util/Debug.h"
#include "Util/WorldParser.h"

void printControls() {
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
  1 / 2         Camera Presets
)" << std::endl;
}

int main() {
#ifdef _DEBUG
  _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
  ConfigParser config;
  config.load("config.ini");
  Debug::setEnabled(Debug::Category::MAIN, true);
  Debug::setEnabled(Debug::Category::VULKAN, true);
  Debug::setEnabled(Debug::Category::RENDERING, true);
  if (Debug::isEnabled(Debug::Category::MAIN)) printControls();
  try {
    Application app;
    Debug::log(Debug::Category::MAIN,
               "Initializing Vulkan Simulation Engine...");
    app.init();

    auto worlds = WorldParser::listWorlds("Worlds");
    if (!worlds.empty()) {
      app.loadWorld(worlds[0]);
    }
    app.getLightManager()->debugPrintLightInfo();

    Debug::log(Debug::Category::MAIN, "Starting main loop...");
    app.run();
  } catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}