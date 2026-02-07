#include "io_utils.h"
#include "common.h"

#include <fstream>
#include <iterator>

namespace vexel {

std::string read_text_file_or_throw(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw CompileError("Cannot open file: " + path, SourceLocation());
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void write_text_file_or_throw(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    if (!file) {
        throw CompileError("Cannot write file: " + path, SourceLocation());
    }
    file << content;
}

} // namespace vexel
