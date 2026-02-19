#include "Util/ConfigParser.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "Util/Debug.h"

bool ConfigParser::load(const std::string& filename) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    Debug::log(Debug::Category::MAIN, "Failed to open config file: ", filename);
    return false;
  }

  std::string line;
  std::string currentSection;

  while (std::getline(file, line)) {
    line = trim(line);

    if (line.empty() || line[0] == ';' || line[0] == '#') {
      continue;
    }

    if (line[0] == '[' && line[line.length() - 1] == ']') {
      currentSection = line.substr(1, line.length() - 2);
      continue;
    }

    const size_t delimiterPos = line.find('=');
    if (delimiterPos != std::string::npos) {
      std::string key = trim(line.substr(0, delimiterPos));
      const std::string value = trim(line.substr(delimiterPos + 1));

      if (!currentSection.empty()) {
        key = currentSection + "." + key;
      }

      data[key] = value;
    }
  }

  Debug::log(Debug::Category::MAIN, "Loaded ", data.size(),
             " config values from ", filename);
  return true;
}

std::string ConfigParser::getString(const std::string& key,
                                    const std::string& defaultValue) const {
  const auto it = data.find(key);
  return it != data.end() ? it->second : defaultValue;
}

int ConfigParser::getInt(const std::string& key, int defaultValue) const {
  const auto it = data.find(key);
  if (it != data.end()) {
    try {
      return std::stoi(it->second);
    } catch (...) {
      return defaultValue;
    }
  }
  return defaultValue;
}

float ConfigParser::getFloat(const std::string& key, float defaultValue) const {
  const auto it = data.find(key);
  if (it != data.end()) {
    try {
      return std::stof(it->second);
    } catch (...) {
      return defaultValue;
    }
  }
  return defaultValue;
}

bool ConfigParser::getBool(const std::string& key, bool defaultValue) const {
  const auto it = data.find(key);
  if (it != data.end()) {
    std::string value = it->second;
    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
    return value == "true" || value == "1" || value == "yes" || value == "on";
  }
  return defaultValue;
}

std::string ConfigParser::trim(const std::string& str) const {
  const size_t first = str.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return "";
  const size_t last = str.find_last_not_of(" \t\r\n");
  return str.substr(first, last - first + 1);
}