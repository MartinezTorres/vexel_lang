#pragma once

#include "cte_value.h"

#include <optional>

namespace vexel {

bool cte_scalar_to_bool(const CTValue& value, bool& out);
std::optional<bool> cte_scalar_to_bool(const CTValue& value);

} // namespace vexel
