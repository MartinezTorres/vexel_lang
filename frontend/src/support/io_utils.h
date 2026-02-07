#pragma once

#include <string>

namespace vexel {

std::string read_text_file_or_throw(const std::string& path);
void write_text_file_or_throw(const std::string& path, const std::string& content);

} // namespace vexel
