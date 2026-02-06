#pragma once
#include <string>

namespace vexel {

constexpr char kScopeSeparator = '\x1F';

inline std::string reachability_key(const std::string& name, int instance_id) {
    if (instance_id < 0) {
        return name;
    }
    return name + kScopeSeparator + std::to_string(instance_id);
}

inline void split_reachability_key(const std::string& key, std::string& name, int& instance_id) {
    size_t pos = key.rfind(kScopeSeparator);
    if (pos == std::string::npos) {
        name = key;
        instance_id = -1;
        return;
    }
    name = key.substr(0, pos);
    instance_id = std::stoi(key.substr(pos + 1));
}

} // namespace vexel
