#include "cte_value.h"
#include <limits>

namespace vexel {

bool ctvalue_is_exact_int(const CTValue& value) {
    return std::holds_alternative<CTExactInt>(value);
}

bool ctvalue_is_integer(const CTValue& value) {
    return std::holds_alternative<int64_t>(value) ||
           std::holds_alternative<uint64_t>(value) ||
           std::holds_alternative<CTExactInt>(value);
}

bool ctvalue_to_exact_int(const CTValue& value, APInt& out, bool& is_unsigned) {
    if (std::holds_alternative<int64_t>(value)) {
        out = APInt(std::get<int64_t>(value));
        is_unsigned = false;
        return true;
    }
    if (std::holds_alternative<uint64_t>(value)) {
        out = APInt(std::get<uint64_t>(value));
        is_unsigned = true;
        return true;
    }
    if (std::holds_alternative<CTExactInt>(value)) {
        const CTExactInt& exact = std::get<CTExactInt>(value);
        out = exact.value;
        is_unsigned = exact.is_unsigned;
        return true;
    }
    if (std::holds_alternative<bool>(value)) {
        out = APInt(uint64_t(std::get<bool>(value) ? 1ULL : 0ULL));
        is_unsigned = true;
        return true;
    }
    return false;
}

CTValue ctvalue_from_exact_int(const APInt& value, bool is_unsigned) {
    if (is_unsigned) {
        if (value.fits_u64()) {
            return value.to_u64();
        }
    } else {
        if (value.fits_i64()) {
            return value.to_i64();
        }
    }
    CTExactInt exact;
    exact.value = value;
    exact.is_unsigned = is_unsigned;
    return exact;
}

bool ctvalue_to_i64_exact(const CTValue& value, int64_t& out) {
    if (std::holds_alternative<int64_t>(value)) {
        out = std::get<int64_t>(value);
        return true;
    }
    if (std::holds_alternative<uint64_t>(value)) {
        uint64_t u = std::get<uint64_t>(value);
        if (u > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) return false;
        out = static_cast<int64_t>(u);
        return true;
    }
    if (std::holds_alternative<CTExactInt>(value)) {
        const CTExactInt& exact = std::get<CTExactInt>(value);
        if (!exact.value.fits_i64()) return false;
        out = exact.value.to_i64();
        return true;
    }
    if (std::holds_alternative<bool>(value)) {
        out = std::get<bool>(value) ? 1 : 0;
        return true;
    }
    return false;
}

bool ctvalue_to_u64_exact(const CTValue& value, uint64_t& out) {
    if (std::holds_alternative<uint64_t>(value)) {
        out = std::get<uint64_t>(value);
        return true;
    }
    if (std::holds_alternative<int64_t>(value)) {
        int64_t s = std::get<int64_t>(value);
        if (s < 0) return false;
        out = static_cast<uint64_t>(s);
        return true;
    }
    if (std::holds_alternative<CTExactInt>(value)) {
        const CTExactInt& exact = std::get<CTExactInt>(value);
        if (!exact.value.fits_u64()) return false;
        out = exact.value.to_u64();
        return true;
    }
    if (std::holds_alternative<bool>(value)) {
        out = std::get<bool>(value) ? 1u : 0u;
        return true;
    }
    return false;
}

CTValue clone_ct_value(const CTValue& value) {
    if (std::holds_alternative<CTNoValue>(value)) {
        return CTNoValue{};
    }
    if (std::holds_alternative<CTUninitialized>(value)) {
        return CTUninitialized{};
    }
    if (std::holds_alternative<std::shared_ptr<CTComposite>>(value)) {
        auto src = std::get<std::shared_ptr<CTComposite>>(value);
        if (!src) {
            return std::shared_ptr<CTComposite>();
        }
        auto dst = std::make_shared<CTComposite>();
        dst->type_name = src->type_name;
        for (const auto& entry : src->fields) {
            dst->fields[entry.first] = clone_ct_value(entry.second);
        }
        return dst;
    }
    if (std::holds_alternative<std::shared_ptr<CTArray>>(value)) {
        auto src = std::get<std::shared_ptr<CTArray>>(value);
        if (!src) {
            return std::shared_ptr<CTArray>();
        }
        auto dst = std::make_shared<CTArray>();
        dst->elements.reserve(src->elements.size());
        for (const auto& elem : src->elements) {
            dst->elements.push_back(clone_ct_value(elem));
        }
        return dst;
    }
    return value;
}

} // namespace vexel
