#pragma once

#include "apint.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace vexel {

struct CTComposite;
struct CTArray;
struct CTNoValue {};
struct CTUninitialized {};
struct CTExactInt {
    APInt value = APInt(uint64_t(0));
    bool is_unsigned = false;
};

using CTValue = std::variant<int64_t,
                             uint64_t,
                             CTExactInt,
                             double,
                             bool,
                             std::string,
                             CTNoValue,
                             CTUninitialized,
                             std::shared_ptr<CTComposite>,
                             std::shared_ptr<CTArray>>;

struct CTComposite {
    std::string type_name;
    std::unordered_map<std::string, CTValue> fields;
};

struct CTArray {
    std::vector<CTValue> elements;
};

enum class CTEQueryStatus {
    Known,
    Unknown,
    Error,
};

struct CTEQueryResult {
    CTEQueryStatus status = CTEQueryStatus::Unknown;
    CTValue value = static_cast<int64_t>(0);
    std::string message;
};

inline CTValue copy_ct_value(const CTValue& value) {
    return value;
}

bool ctvalue_is_exact_int(const CTValue& value);
bool ctvalue_is_integer(const CTValue& value);
bool ctvalue_to_exact_int(const CTValue& value, APInt& out, bool& is_unsigned);
CTValue ctvalue_from_exact_int(const APInt& value, bool is_unsigned);
bool ctvalue_to_i64_exact(const CTValue& value, int64_t& out);
bool ctvalue_to_u64_exact(const CTValue& value, uint64_t& out);

CTValue clone_ct_value(const CTValue& value);

} // namespace vexel
