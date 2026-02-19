#pragma once
#include <string>
#include <unordered_map>

class ConfigParser final {
 public:
  ConfigParser() = default;

  bool load(const std::string& filename);

  std::string getString(const std::string& key,
                        const std::string& defaultValue = "") const;

  int getInt(const std::string& key, int defaultValue = 0) const;

  float getFloat(const std::string& key, float defaultValue = 0.0f) const;

  bool getBool(const std::string& key, bool defaultValue = false) const;

 private:
  std::unordered_map<std::string, std::string> data;

  std::string trim(const std::string& str) const;
};