#include "evaluator.h"
#include "constants.h"
#include "cte_value_utils.h"
#include <cmath>
#include <limits>

namespace vexel {

namespace {

bool is_fixed_primitive_type(const TypePtr& type) {
    return type &&
           type->kind == Type::Kind::Primitive &&
           (is_signed_fixed(type->primitive) || is_unsigned_fixed(type->primitive));
}

bool fixed_cast_meta(const TypePtr& type,
                    uint64_t& total_bits,
                    bool& is_signed_raw,
                    int64_t& fractional_bits) {
    if (!is_fixed_primitive_type(type)) return false;
    int64_t bits_i64 = type_bits(type->primitive, type->integer_bits, type->fractional_bits);
    if (bits_i64 <= 0) return false;
    total_bits = static_cast<uint64_t>(bits_i64);
    is_signed_raw = type->primitive == PrimitiveType::FixedInt;
    fractional_bits = type->fractional_bits;
    return true;
}

APInt trunc_div_pow2(const APInt& value, uint64_t shift) {
    if (shift == 0) return value;
    if (value.is_negative()) {
        APInt mag = -value;
        return -(mag >> shift);
    }
    return value >> shift;
}

APInt scale_by_pow2_trunc_zero(const APInt& value, int64_t shift) {
    if (shift == 0) return value;
    if (shift > 0) {
        return value << static_cast<uint64_t>(shift);
    }
    return trunc_div_pow2(value, static_cast<uint64_t>(-shift));
}

} // namespace

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
        target_type->element_type->primitive == PrimitiveType::UInt &&
        target_type->element_type->integer_bits == 8 &&
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
        int64_t length = 0;
        if (!ctvalue_to_i64_exact(size_val, length)) {
            error_msg = "Array length must be a compile-time integer constant";
            return false;
        }
        if (length < 0) {
            error_msg = "Array length cannot be negative";
            return false;
        }

        uint64_t bits = 0;
        if (expr->operand->type->primitive == PrimitiveType::Int ||
            expr->operand->type->primitive == PrimitiveType::UInt) {
            bits = expr->operand->type->integer_bits;
        } else if (expr->operand->type->primitive == PrimitiveType::Bool) {
            bits = 1;
        } else {
            int64_t type_bits_val = type_bits(expr->operand->type->primitive,
                                             expr->operand->type->integer_bits,
                                             expr->operand->type->fractional_bits);
            if (type_bits_val < 0) {
                error_msg = "Unsupported operand type for byte array cast";
                return false;
            }
            bits = static_cast<uint64_t>(type_bits_val);
        }
        if (bits == 0 || bits % 8 != 0 || static_cast<uint64_t>(length) != (bits / 8)) {
            error_msg = "Array length/type size mismatch in cast";
            return false;
        }

        APInt value_bits(uint64_t(0));
        bool operand_unsigned = false;
        if (!ctvalue_to_exact_int(operand_val, value_bits, operand_unsigned)) {
            error_msg = "Unsupported operand type for byte array cast";
            return false;
        }
        value_bits = value_bits.wrapped_unsigned(bits);

        auto array = std::make_shared<CTArray>();
        array->elements.reserve(static_cast<size_t>(length));
        for (int64_t i = 0; i < length; ++i) {
            uint64_t shift = static_cast<uint64_t>((length - 1 - i) * 8);
            APInt byte = (value_bits >> shift) & APInt(uint64_t(0xFF));
            array->elements.push_back(ctvalue_from_exact_int(byte, true));
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
            if (!ctvalue_to_i64_exact(size_val, length)) {
                error_msg = "Array length must be a compile-time integer constant";
                return false;
            }
        }

        if (length <= 0) {
            error_msg = "Boolean array size must be non-zero";
            return false;
        }
        if (target_type->integer_bits == 0) {
            error_msg = "Unsigned integer cast target is missing width";
            return false;
        }
        if (length != static_cast<int64_t>(target_type->integer_bits)) {
            error_msg = "Boolean array size mismatch for cast to #" +
                        primitive_name(target_type->primitive,
                                       target_type->integer_bits,
                                       target_type->fractional_bits);
            return false;
        }

        APInt out(uint64_t(0));
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
            if (std::holds_alternative<CTExactInt>(v)) {
                bit = !std::get<CTExactInt>(v).value.is_zero();
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
                        primitive_name(target_type->primitive,
                                       target_type->integer_bits,
                                       target_type->fractional_bits);
            return false;
        }
        for (int64_t i = 0; i < length; ++i) {
            bool bit = false;
            if (!to_bit(array->elements[static_cast<size_t>(i)], bit)) {
                error_msg = "Boolean array contains non-boolean value";
                return false;
            }
            if (bit) {
                uint64_t shift = static_cast<uint64_t>(length - 1 - i);
                out = out | (APInt(uint64_t(1)) << shift);
            }
        }
        result = ctvalue_from_exact_int(out, true);
        return true;
    }

    // Only handle primitive types at compile time
    if (target_type->kind != Type::Kind::Primitive) {
        error_msg = "Can only cast to primitive types at compile time";
        return false;
    }

    TypePtr source_type = expr->operand ? expr->operand->type : nullptr;
    const bool source_is_fixed = is_fixed_primitive_type(source_type);
    const bool target_is_fixed = is_fixed_primitive_type(target_type);
    if (source_is_fixed || target_is_fixed) {
        if (!source_type || source_type->kind != Type::Kind::Primitive) {
            error_msg = "Fixed-point compile-time casts require primitive operands";
            return false;
        }

        uint64_t src_fixed_bits = 0;
        bool src_fixed_signed = false;
        int64_t src_frac = 0;
        if (source_is_fixed &&
            !fixed_cast_meta(source_type, src_fixed_bits, src_fixed_signed, src_frac)) {
            error_msg = "Fixed-point compile-time cast source has invalid storage width";
            return false;
        }
        (void)src_fixed_bits;
        (void)src_fixed_signed;
        uint64_t dst_fixed_bits = 0;
        bool dst_fixed_signed = false;
        int64_t dst_frac = 0;
        if (target_is_fixed &&
            !fixed_cast_meta(target_type, dst_fixed_bits, dst_fixed_signed, dst_frac)) {
            error_msg = "Fixed-point compile-time cast target has invalid storage width";
            return false;
        }

        auto to_exact_int_scalar = [&](const CTValue& v, APInt& out, bool& is_unsigned) -> bool {
            if (ctvalue_to_exact_int(v, out, is_unsigned)) {
                return true;
            }
            return false;
        };

        if (target_is_fixed) {
            APInt raw(uint64_t(0));
            bool src_unsigned = false;
            if (source_is_fixed) {
                if (!to_exact_int_scalar(operand_val, raw, src_unsigned)) {
                    error_msg = "Unsupported fixed-point cast source";
                    return false;
                }
                raw = scale_by_pow2_trunc_zero(raw, dst_frac - src_frac);
            } else if (is_signed_int(source_type->primitive) ||
                       is_unsigned_int(source_type->primitive) ||
                       source_type->primitive == PrimitiveType::Bool) {
                if (!to_exact_int_scalar(operand_val, raw, src_unsigned)) {
                    error_msg = "Unsupported fixed-point cast source";
                    return false;
                }
                raw = scale_by_pow2_trunc_zero(raw, dst_frac);
            } else if (is_float(source_type->primitive)) {
                double scaled = std::ldexp(to_float(operand_val), static_cast<int>(dst_frac));
                if (!std::isfinite(scaled)) {
                    error_msg = "Fixed-point compile-time cast from floating-point requires a finite value";
                    return false;
                }
                double truncd = std::trunc(scaled);
                if (truncd >= 0.0) {
                    long double max_u64 = static_cast<long double>(std::numeric_limits<uint64_t>::max());
                    if (static_cast<long double>(truncd) > max_u64) {
                        error_msg = "Fixed-point compile-time cast from floating-point exceeds supported range";
                        return false;
                    }
                    raw = APInt(static_cast<uint64_t>(truncd));
                } else {
                    long double min_i64 = static_cast<long double>(std::numeric_limits<int64_t>::min());
                    if (static_cast<long double>(truncd) < min_i64) {
                        error_msg = "Fixed-point compile-time cast from floating-point exceeds supported range";
                        return false;
                    }
                    raw = APInt(static_cast<int64_t>(truncd));
                }
            } else {
                error_msg = "Unsupported fixed-point cast source";
                return false;
            }

            raw = dst_fixed_signed ? raw.wrapped_signed(dst_fixed_bits)
                                   : raw.wrapped_unsigned(dst_fixed_bits);
            result = ctvalue_from_exact_int(raw, !dst_fixed_signed);
            return true;
        }

        if (source_is_fixed) {
            APInt raw(uint64_t(0));
            bool raw_unsigned = false;
            if (!to_exact_int_scalar(operand_val, raw, raw_unsigned)) {
                error_msg = "Unsupported fixed-point cast source";
                return false;
            }
            if (is_float(target_type->primitive)) {
                double raw_d = raw.raw().convert_to<double>();
                result = std::ldexp(raw_d, -static_cast<int>(src_frac));
                return true;
            }
            APInt numeric = scale_by_pow2_trunc_zero(raw, -src_frac);

            if (is_signed_int(target_type->primitive)) {
                if (target_type->integer_bits == 0) {
                    error_msg = "Signed integer cast target is missing width";
                    return false;
                }
                result = ctvalue_from_exact_int(numeric.wrapped_signed(target_type->integer_bits), false);
                return true;
            }
            if (is_unsigned_int(target_type->primitive)) {
                if (target_type->integer_bits == 0) {
                    error_msg = "Unsigned integer cast target is missing width";
                    return false;
                }
                result = ctvalue_from_exact_int(numeric.wrapped_unsigned(target_type->integer_bits), true);
                return true;
            }
            if (target_type->primitive == PrimitiveType::Bool) {
                result = !numeric.is_zero();
                return true;
            }
            error_msg = "Unsupported fixed-point cast target";
            return false;
        }
    }

    // Perform the cast based on target primitive type
    if (is_signed_int(target_type->primitive)) {
        if (target_type->integer_bits == 0) {
            error_msg = "Signed integer cast target is missing width";
            return false;
        }
        APInt exact(uint64_t(0));
        bool is_unsigned = false;
        if (!ctvalue_to_exact_int(operand_val, exact, is_unsigned)) {
            if (std::holds_alternative<double>(operand_val)) {
                exact = APInt(static_cast<int64_t>(std::get<double>(operand_val)));
            } else {
                error_msg = "Unsupported cast source for signed integer";
                return false;
            }
        }
        result = ctvalue_from_exact_int(exact.wrapped_signed(target_type->integer_bits), false);
        return true;
    } else if (is_unsigned_int(target_type->primitive)) {
        if (target_type->integer_bits == 0) {
            error_msg = "Unsigned integer cast target is missing width";
            return false;
        }
        APInt exact(uint64_t(0));
        bool is_unsigned = false;
        if (!ctvalue_to_exact_int(operand_val, exact, is_unsigned)) {
            if (std::holds_alternative<double>(operand_val)) {
                exact = APInt(static_cast<int64_t>(std::get<double>(operand_val)));
            } else {
                error_msg = "Unsupported cast source for unsigned integer";
                return false;
            }
        }
        result = ctvalue_from_exact_int(exact.wrapped_unsigned(target_type->integer_bits), true);
        return true;
    } else if (is_float(target_type->primitive)) {
        // Cast to float
        result = to_float(operand_val);
        return true;
    } else if (target_type->primitive == PrimitiveType::Bool) {
        bool b = false;
        if (!cte_scalar_to_bool(operand_val, b)) {
            error_msg = "Unsupported cast source for bool";
            return false;
        }
        result = b;
        return true;
    }

    error_msg = "Unsupported cast type at compile time";
    return false;
}

} // namespace vexel
