#pragma once
#include <string>
#include <vector>
#include <utility>

namespace vx {
// Explicit argv and bounded ordinary stdio. Proposal text is never argv. A
// source process import may explicitly select a shell only with its opt-in.
std::pair<std::string,std::string> runProcess(const std::vector<std::string>& args,
    const std::string& input, std::size_t timeoutMs, bool includeStderr = false,
    const std::string& directory = {},std::size_t maxOutput = 65536);
}
