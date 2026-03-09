#pragma once
#include <string>
#include <vector>

#include "TimelineSystem.h"
#include "../Util/Interface.h"

class WorldBakeSerializer {
 public:
  // Returns the expected bake file path for a given world path
  static std::string bakePathFor(const std::string& worldPath);

  // Returns true if a bake file exists for this world
  static bool bakeExists(const std::string& worldPath);

  // Saves all current snapshots + stats to a .worldbake file.
  // Overwrites any existing file for this world.
  static bool save(const std::string& worldPath,
                   const TimelineSystem& timeline,
                   const std::vector<float>& timeHistory,
                   const BakeStats& stats);

  // Loads a .worldbake file back into the timeline system.
  // Populates timeHistory and returns the embedded BakeStats header.
  // Returns false on failure.
  static bool load(const std::string& worldPath,
                   TimelineSystem& timeline,
                   std::vector<float>& timeHistory,
                   BakeStats& statsOut);
};
