#include "evaluator.h"
#include "constants.h"

namespace vexel {

bool CompileTimeEvaluator::eval_cast(ExprPtr expr, CTValue& result) {
    // Evaluate the operand
    CTValue operand_val;
    if (!try_evaluate(expr->operand, operand_val)) {
        return false;
    }

    // Get target type
    TypePtr target_type = expr->target_type;
    if (!target_type) {
        error_msg = "Cast expression has no target type";
        return false;
    }

    // Primitive to byte array conversion (big-endian order)
    if (target_type->kind == Type::Kind::Array &&
        target_type->element_type &&
        target_type->element_type->kind == Type::Kind::Primitive &&
        target_type->element_type->primitive == PrimitiveType::U8 &&
        expr->operand && expr->operand->type &&
        expr->operand->type->kind == Type::Kind::Primitive &&
        !is_float(expr->operand->type->primitive)) {

        if (!target_type->array_size) {
            error_msg = "Array length must be a compile-time constant";
            return false;
        }
        CTValue size_val;
        if (!try_evaluate(target_type->array_size, size_val)) {
            error_msg = "Array length must be a compile-time constant";
            return false;
        }
        int64_t length = std::holds_alternative<int64_t>(size_val)
            ? std::get<int64_t>(size_val)
            : static_cast<int64_t>(std::get<uint64_t>(size_val));
        if (length < 0) {
            error_msg = "Array length cannot be negative";
            return false;
        }

        int bits = type_bits(expr->operand->type->primitive);
        if (bits < 0 || bits / 8 != length) {
            error_msg = "Array length/type size mismatch in cast";
            return false;
        }

        uint64_t value_bits = 0;
        if (std::holds_alternative<uint64_t>(operand_val)) {
            value_bits = std::get<uint64_t>(operand_val);
        } else if (std::holds_alternative<int64_t>(operand_val)) {
            value_bits = static_cast<uint64_t>(std::get<int64_t>(operand_val));
        } else if (std::holds_alternative<bool>(operand_val)) {
            value_bits = std::get<bool>(operand_val) ? 1u : 0u;
        } else {
            error_msg = "Unsupported operand type for byte array cast";
            return false;
        }

        if (bits < 64) {
            uint64_t mask = (bits == 64) ? ~0ull : ((1ull << bits) - 1ull);
            value_bits &= mask;
        }

        auto array = std::make_shared<CTArray>();
        array->elements.reserve(static_cast<size_t>(length));
        for (int64_t i = 0; i < length; ++i) {
            int64_t shift = (length - 1 - i) * 8;
            uint64_t byte = (value_bits >> shift) & 0xFFu;
            array->elements.push_back(byte);
        }
        result = array;
        return true;
    }

    // Pack boolean arrays into unsigned integers
    if (target_type->kind == Type::Kind::Primitive &&
        is_unsigned_int(target_type->primitive) &&
        expr->operand && expr->operand->type &&
        expr->operand->type->kind == Type::Kind::Array &&
        expr->operand->type->element_type &&
        expr->operand->type->element_type->kind == Type::Kind::Primitive &&
        expr->operand->type->element_type->primitive == PrimitiveType::Bool) {

        int64_t length = 0;
        if (std::holds_alternative<std::shared_ptr<CTArray>>(operand_val)) {
            auto array = std::get<std::shared_ptr<CTArray>>(operand_val);
            if (!array) {
                error_msg = "Cast from null boolean array";
                return false;
            }
            length = static_cast<int64_t>(array->elements.size());
        } else if (expr->operand->type->array_size) {
            CTValue size_val;
            if (!try_evaluate(expr->operand->type->array_size, size_val)) {
                error_msg = "Array length must be a compile-time constant";
                return false;
            }
            length = std::holds_alternative<int64_t>(size_val)
                ? std::get<int64_t>(size_val)
                : static_cast<int64_t>(std::get<uint64_t>(size_val));
        }

        if (length <= 0) {
            error_msg = "Boolean array size must be non-zero";
            return false;
        }
        if (length != type_bits(target_type->primitive)) {
            error_msg = "Boolean array size mismatch for cast to #" +
                        primitive_name(target_type->primitive);
            return false;
        }

        uint64_t out = 0;
        auto to_bit = [&](const CTValue& v, bool& bit) -> bool {
            if (std::holds_alternative<bool>(v)) {
                bit = std::get<bool>(v);
                return true;
            }
            if (std::holds_alternative<int64_t>(v)) {
                bit = std::get<int64_t>(v) != 0;
                return true;
            }
            if (std::holds_alternative<uint64_t>(v)) {
                bit = std::get<uint64_t>(v) != 0;
                return true;
            }
            return false;
        };

        if (!std::holds_alternative<std::shared_ptr<CTArray>>(operand_val)) {
            error_msg = "Boolean array cast requires compile-time array";
            return false;
        }
        auto array = std::get<std::shared_ptr<CTArray>>(operand_val);
        if (!array) {
            error_msg = "Cast from null boolean array";
            return false;
        }
        if (static_cast<int64_t>(array->elements.size()) != length) {
            error_msg = "Boolean array size mismatch for cast to #" +
                        primitive_name(target_type->primitive);
            return false;
        }
        for (int64_t i = 0; i < length; ++i) {
            bool bit = false;
            if (!to_bit(array->elements[static_cast<size_t>(i)], bit)) {
                error_msg = "Boolean array contains non-boolean value";
                return false;
            }
            if (bit) {
                int64_t shift = (length - 1 - i);
                out |= (uint64_t)(1u) << shift;
            }
        }
        result = out;
        return true;
    }

    // Only handle primitive types at compile time
    if (target_type->kind != Type::Kind::Primitive) {
        error_msg = "Can only cast to primitive types at compile time";
        return false;
    }

    // Perform the cast based on target primitive type
    if (target_type->primitive == PrimitiveType::I8 || target_type->primitive == PrimitiveType::I16 ||
        target_type->primitive == PrimitiveType::I32 || target_type->primitive == PrimitiveType::I64) {
        // Cast to signed integer
        result = to_int(operand_val);
        return true;
    } else if (target_type->primitive == PrimitiveType::U8 || target_type->primitive == PrimitiveType::U16 ||
               target_type->primitive == PrimitiveType::U32 || target_type->primitive == PrimitiveType::U64) {
        // Cast to unsigned integer
        result = (uint64_t)to_int(operand_val);
        return true;
    } else if (target_type->primitive == PrimitiveType::F32 || target_type->primitive == PrimitiveType::F64) {
        // Cast to float
        result = to_float(operand_val);
        return true;
    } else if (target_type->primitive == PrimitiveType::Bool) {
        // Cast to bool
        result = to_int(operand_val) != 0;
        return true;
    }

    error_msg = "Unsupported cast type at compile time";
    return false;
}

} // namespace vexel
