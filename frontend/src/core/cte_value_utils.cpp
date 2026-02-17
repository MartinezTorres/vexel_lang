#include "cte_value_utils.h"

namespace vexel {

bool cte_scalar_to_bool(const CTValue& value, bool& out) {
    if (std::holds_alternative<int64_t>(value)) {
        out = std::get<int64_t>(value) != 0;
        return true;
    }
    if (std::holds_alternative<uint64_t>(value)) {
        out = std::get<uint64_t>(value) != 0;
        return true;
    }
    if (std::holds_alternative<bool>(value)) {
        out = std::get<bool>(value);
        return true;
    }
    if (std::holds_alternative<double>(value)) {
        out = std::get<double>(value) != 0.0;
        return true;
    }
    return false;
}

std::optional<bool> cte_scalar_to_bool(const CTValue& value) {
    bool out = false;
    if (!cte_scalar_to_bool(value, out)) {
        return std::nullopt;
    }
    return out;
}

} // namespace vexel
