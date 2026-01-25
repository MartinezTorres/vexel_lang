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
    I8, I16, I32, I64,
    U8, U16, U32, U64,
    F32, F64,
    Bool, String
};

inline std::string primitive_name(PrimitiveType t) {
    switch(t) {
        case PrimitiveType::I8: return "i8";
        case PrimitiveType::I16: return "i16";
        case PrimitiveType::I32: return "i32";
        case PrimitiveType::I64: return "i64";
        case PrimitiveType::U8: return "u8";
        case PrimitiveType::U16: return "u16";
        case PrimitiveType::U32: return "u32";
        case PrimitiveType::U64: return "u64";
        case PrimitiveType::F32: return "f32";
        case PrimitiveType::F64: return "f64";
        case PrimitiveType::Bool: return "b";
        case PrimitiveType::String: return "s";
    }
    return "";
}

inline bool is_signed_int(PrimitiveType t) {
    return t == PrimitiveType::I8 || t == PrimitiveType::I16 ||
           t == PrimitiveType::I32 || t == PrimitiveType::I64;
}

inline bool is_unsigned_int(PrimitiveType t) {
    return t == PrimitiveType::U8 || t == PrimitiveType::U16 ||
           t == PrimitiveType::U32 || t == PrimitiveType::U64;
}

inline bool is_float(PrimitiveType t) {
    return t == PrimitiveType::F32 || t == PrimitiveType::F64;
}

inline int type_bits(PrimitiveType t) {
    switch(t) {
        case PrimitiveType::I8:
        case PrimitiveType::U8:
        case PrimitiveType::Bool: return 8;
        case PrimitiveType::I16:
        case PrimitiveType::U16: return 16;
        case PrimitiveType::I32:
        case PrimitiveType::U32:
        case PrimitiveType::F32: return 32;
        case PrimitiveType::I64:
        case PrimitiveType::U64:
        case PrimitiveType::F64: return 64;
        case PrimitiveType::String: return -1;
    }
    return 0;
}

}
