#pragma once
#include "cte_value.h"
#include <string>

namespace vexel {

struct EvalBreak {};
struct EvalContinue {};
struct EvalReturn {
    CTValue value;
};

inline std::string ct_value_kind(const CTValue& value) {
    if (std::holds_alternative<int64_t>(value)) return "int";
    if (std::holds_alternative<uint64_t>(value)) return "uint";
    if (std::holds_alternative<double>(value)) return "float";
    if (std::holds_alternative<bool>(value)) return "bool";
    if (std::holds_alternative<std::string>(value)) return "string";
    if (std::holds_alternative<CTUninitialized>(value)) return "uninitialized";
    if (std::holds_alternative<std::shared_ptr<CTComposite>>(value)) return "composite";
    if (std::holds_alternative<std::shared_ptr<CTArray>>(value)) return "array";
    return "unknown";
}

} // namespace vexel
