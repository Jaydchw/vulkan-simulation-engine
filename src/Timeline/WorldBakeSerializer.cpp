#include "WorldBakeSerializer.h"

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

static constexpr char k_Magic[]    = "WORLDBAKE_V1";
static constexpr char k_DataBegin[] = "DATA_BEGIN";


std::string WorldBakeSerializer::bakePathFor(const std::string& worldPath) {
  std::filesystem::path p(worldPath);
  return (p.parent_path() / (p.stem().string() + ".worldbake")).string();
}

bool WorldBakeSerializer::bakeExists(const std::string& worldPath) {
  return std::filesystem::exists(bakePathFor(worldPath));
}

bool WorldBakeSerializer::save(const std::string& worldPath,
                               const TimelineSystem& timeline,
                               const std::vector<float>& timeHistory,
                               const BakeStats& stats) {
  const std::string outPath = bakePathFor(worldPath);
  std::ofstream f(outPath, std::ios::binary | std::ios::trunc);
  if (!f.is_open()) return false;

  std::time_t now = std::time(nullptr);
  char timeBuf[64];
#ifdef _WIN32
  struct tm tmBuf{};
  localtime_s(&tmBuf, &now);
  std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &tmBuf);
#else
  std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
#endif

  std::ostringstream hdr;
  hdr << k_Magic << "\n";
  hdr << "BakeDate: "          << timeBuf << "\n";
  hdr << "WorldFile: "         << std::filesystem::path(worldPath).filename().string() << "\n";
  hdr << "TotalFrames: "       << timeline.getSnapshotCount() << "\n";
  hdr << "StepSize: "          << stats.stepSize << "\n";
  hdr << "SimDuration: "       << stats.simDuration << "\n";
  hdr << "ObjectCount: "       << stats.objectCount << "\n";
  hdr << "TotalWallTimeMs: "   << stats.totalWallTimeMs << "\n";
  hdr << "AvgStepMs: "         << stats.avgStepMs << "\n";
  hdr << "MinStepMs: "         << stats.minStepMs << "\n";
  hdr << "MaxStepMs: "         << stats.maxStepMs << "\n";
  hdr << "StepsPerSecond: "    << stats.stepsPerSecond << "\n";
  hdr << "SimSpeedMultiplier: "<< stats.simSecondsPerWallSecond << "\n";
  hdr << "CollisionPairs: "    << stats.collisionPairs.size() << "\n";
  for (const auto& p : stats.collisionPairs) {
    // Encode pair name with underscores so it parses as one token
    std::string safeName = p.pairName;
    for (char& c : safeName) if (c == ' ') c = '_';
    hdr << "  " << safeName << " " << p.totalChecks << " " << p.totalResolved << "\n";
  }
  hdr << k_DataBegin << "\n";

  std::string hdrStr = hdr.str();
  f.write(hdrStr.c_str(), static_cast<std::streamsize>(hdrStr.size()));

  uint32_t histCount = static_cast<uint32_t>(timeHistory.size());
  f.write(reinterpret_cast<const char*>(&histCount), sizeof(histCount));
  if (histCount > 0)
    f.write(reinterpret_cast<const char*>(timeHistory.data()),
            static_cast<std::streamsize>(histCount * sizeof(float)));

  const auto& snaps = timeline.getSnapshots();
  uint32_t frameCount = static_cast<uint32_t>(snaps.size());
  f.write(reinterpret_cast<const char*>(&frameCount), sizeof(frameCount));

  for (const auto& frame : snaps) {
    uint32_t entityCount = static_cast<uint32_t>(frame.size());
    f.write(reinterpret_cast<const char*>(&entityCount), sizeof(entityCount));
    for (size_t ei = 0; ei < frame.entities.size(); ++ei) {
      const Entity entity = frame.entities[ei];
      const EntitySnapshot& snap = frame.states[ei];
      f.write(reinterpret_cast<const char*>(&entity), sizeof(entity));
      f.write(reinterpret_cast<const char*>(&snap.position.x), sizeof(float));
      f.write(reinterpret_cast<const char*>(&snap.position.y), sizeof(float));
      f.write(reinterpret_cast<const char*>(&snap.position.z), sizeof(float));
      f.write(reinterpret_cast<const char*>(&snap.velocity.x), sizeof(float));
      f.write(reinterpret_cast<const char*>(&snap.velocity.y), sizeof(float));
      f.write(reinterpret_cast<const char*>(&snap.velocity.z), sizeof(float));
    }
  }

  return f.good();
}

bool WorldBakeSerializer::load(const std::string& worldPath,
                               TimelineSystem& timeline,
                               std::vector<float>& timeHistory,
                               BakeStats& statsOut) {
  const std::string inPath = bakePathFor(worldPath);
  std::ifstream f(inPath, std::ios::binary);
  if (!f.is_open()) return false;

  statsOut = BakeStats{};
  int collisionPairCount = 0;
  std::string line;

  // First line must be magic
  if (!std::getline(f, line)) return false;
  if (!line.empty() && line.back() == '\r') line.pop_back();
  if (line != k_Magic) return false;

  while (std::getline(f, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line == k_DataBegin) break;

    auto colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string key   = line.substr(0, colon);
    std::string value = line.substr(colon + 1);
    // Trim leading space from value
    while (!value.empty() && value.front() == ' ') value.erase(value.begin());

    if (key == "TotalFrames")        { /* informational */ }
    else if (key == "StepSize")            statsOut.stepSize      = std::stof(value);
    else if (key == "SimDuration")         statsOut.simDuration   = std::stof(value);
    else if (key == "ObjectCount")         statsOut.objectCount   = std::stoi(value);
    else if (key == "TotalWallTimeMs")     statsOut.totalWallTimeMs = std::stod(value);
    else if (key == "AvgStepMs")           statsOut.avgStepMs     = std::stod(value);
    else if (key == "MinStepMs")           statsOut.minStepMs     = std::stod(value);
    else if (key == "MaxStepMs")           statsOut.maxStepMs     = std::stod(value);
    else if (key == "StepsPerSecond")      statsOut.stepsPerSecond = std::stod(value);
    else if (key == "SimSpeedMultiplier")  statsOut.simSecondsPerWallSecond = std::stod(value);
    else if (key == "CollisionPairs") {
      collisionPairCount = std::stoi(value);
      for (int i = 0; i < collisionPairCount; i++) {
        std::string pLine;
        if (!std::getline(f, pLine)) break;
        if (!pLine.empty() && pLine.back() == '\r') pLine.pop_back();
        size_t start = pLine.find_first_not_of(" \t");
        if (start == std::string::npos) { i--; continue; }
        pLine = pLine.substr(start);
        std::istringstream ss(pLine);
        std::string safeName;
        long long checks = 0, resolved = 0;
        ss >> safeName >> checks >> resolved;
        for (char& c : safeName) if (c == '_') c = ' ';
        statsOut.collisionPairs.push_back({safeName, checks, resolved});
      }
    }
  }

  uint32_t histCount = 0;
  f.read(reinterpret_cast<char*>(&histCount), sizeof(histCount));
  if (!f) return false;
  timeHistory.resize(histCount);
  if (histCount > 0)
    f.read(reinterpret_cast<char*>(timeHistory.data()),
           static_cast<std::streamsize>(histCount * sizeof(float)));
  if (!f && histCount > 0) return false;

  uint32_t frameCount = 0;
  f.read(reinterpret_cast<char*>(&frameCount), sizeof(frameCount));
  if (!f) return false;

  std::vector<FrameSnapshot> frames;
  frames.reserve(frameCount);
  for (uint32_t fi = 0; fi < frameCount; fi++) {
    uint32_t entityCount = 0;
    f.read(reinterpret_cast<char*>(&entityCount), sizeof(entityCount));
    if (!f) return false;
    FrameSnapshot frame;
    frame.reserve(entityCount);
    for (uint32_t ei = 0; ei < entityCount; ei++) {
      uint32_t entity = 0;
      EntitySnapshot snap{};
      f.read(reinterpret_cast<char*>(&entity), sizeof(entity));
      f.read(reinterpret_cast<char*>(&snap.position.x), sizeof(float));
      f.read(reinterpret_cast<char*>(&snap.position.y), sizeof(float));
      f.read(reinterpret_cast<char*>(&snap.position.z), sizeof(float));
      f.read(reinterpret_cast<char*>(&snap.velocity.x), sizeof(float));
      f.read(reinterpret_cast<char*>(&snap.velocity.y), sizeof(float));
      f.read(reinterpret_cast<char*>(&snap.velocity.z), sizeof(float));
      if (!f) return false;
      frame.push(entity, snap);
    }
    frames.push_back(std::move(frame));
  }

  statsOut.totalSteps = static_cast<int>(frameCount);
  statsOut.hasData    = (statsOut.totalWallTimeMs > 0.0 || statsOut.stepSize > 0.0f);

  timeline.loadFromBake(std::move(frames));
  return true;
}
