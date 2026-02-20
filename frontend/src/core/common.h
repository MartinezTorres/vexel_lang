#pragma once
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <cstdint>
#include <stdexcept>
#include <iostream>

namespace vexel {

struct SourceLocation {
    std::string filename;
    int line;
    int column;
    SourceLocation(const std::string& f = "", int l = 0, int c = 0)
        : filename(f), line(l), column(c) {}
};

class CompileError : public std::runtime_error {
public:
    SourceLocation location;
    CompileError(const std::string& msg, const SourceLocation& loc = SourceLocation())
        : std::runtime_error(msg), location(loc) {}
};

// Diagnostic severity levels
enum class DiagnosticLevel {
    Error,
    Warning,
    Note
};

// Diagnostic message with location and severity
struct Diagnostic {
    DiagnosticLevel level;
    std::string message;
    SourceLocation location;
    std::string hint; // Optional suggestion for fixing

    Diagnostic(DiagnosticLevel lvl, const std::string& msg, const SourceLocation& loc, const std::string& h = "")
        : level(lvl), message(msg), location(loc), hint(h) {}

    std::string to_string() const {
        std::string level_str;
        switch (level) {
            case DiagnosticLevel::Error: level_str = "Error"; break;
            case DiagnosticLevel::Warning: level_str = "Warning"; break;
            case DiagnosticLevel::Note: level_str = "Note"; break;
        }

        std::string result = level_str + " at " + location.filename + ":" +
                           std::to_string(location.line) + ":" +
                           std::to_string(location.column) + ": " + message;

        if (!hint.empty()) {
            result += "\n  Hint: " + hint;
        }

        return result;
    }
};

enum class PrimitiveType {
    Int,
    UInt,
    F16,
    F32,
    F64,
    Bool,
    String
};

inline std::string primitive_name(PrimitiveType t, uint64_t integer_bits = 0) {
    switch(t) {
        case PrimitiveType::Int:
            return integer_bits > 0 ? "i" + std::to_string(integer_bits) : "i";
        case PrimitiveType::UInt:
            return integer_bits > 0 ? "u" + std::to_string(integer_bits) : "u";
        case PrimitiveType::F16: return "f16";
        case PrimitiveType::F32: return "f32";
        case PrimitiveType::F64: return "f64";
        case PrimitiveType::Bool: return "b";
        case PrimitiveType::String: return "s";
    }
    return "";
}

inline bool is_signed_int(PrimitiveType t) {
    return t == PrimitiveType::Int;
}

inline bool is_unsigned_int(PrimitiveType t) {
    return t == PrimitiveType::UInt;
}

inline bool is_float(PrimitiveType t) {
    return t == PrimitiveType::F16 || t == PrimitiveType::F32 || t == PrimitiveType::F64;
}

inline int64_t type_bits(PrimitiveType t, uint64_t integer_bits = 0) {
    switch(t) {
        case PrimitiveType::Int:
        case PrimitiveType::UInt:
            return static_cast<int64_t>(integer_bits);
        case PrimitiveType::Bool: return 1;
        case PrimitiveType::F16: return 16;
        case PrimitiveType::F32: return 32;
        case PrimitiveType::F64: return 64;
        case PrimitiveType::String: return -1;
    }
    return 0;
}

}
