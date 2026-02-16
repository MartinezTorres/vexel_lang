#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace vexel {

struct CTComposite;
struct CTArray;
struct CTUninitialized {};

using CTValue = std::variant<int64_t,
                             uint64_t,
                             double,
                             bool,
                             std::string,
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

CTValue clone_ct_value(const CTValue& value);

} // namespace vexel
