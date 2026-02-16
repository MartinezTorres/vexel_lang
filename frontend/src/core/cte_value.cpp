#include "cte_value.h"

namespace vexel {

CTValue clone_ct_value(const CTValue& value) {
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
