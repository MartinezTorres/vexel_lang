#include "vexel.hpp"
#include "floating.hpp"
#include <algorithm>

namespace vx {
namespace {
Big wrap(Big n, const Type& t) {
    if (t.kind != Type::Integer && t.kind != Type::Fixed) return n;
    const Big modulus = Big(1) << t.bits;
    n %= modulus;
    if (n < 0) n += modulus;
    if (t.sign && n >= (Big(1) << (t.bits - 1))) n -= modulus;
    return n;
}
bool fits(const Big& n, const Type& t) {
    if (t.kind == Type::Boolean) return n == 0 || n == 1;
    if (t.kind != Type::Integer && t.kind != Type::Fixed) return false;
    if (t.sign) return n >= -(Big(1) << (t.bits - 1)) && n < (Big(1) << (t.bits - 1));
    return n >= 0 && n < (Big(1) << t.bits);
}
bool integral(const Type& t) { return t.kind == Type::Exact || t.kind == Type::Integer; }
bool numeric(const Type& t) { return integral(t) || t.kind == Type::Fixed || t.kind == Type::Float; }
bool comparisonResult(const std::string& op,std::optional<int> order) {
    if (!order) return op == "!=";
    int c=*order;
    return op == "==" ? c == 0 : op == "!=" ? c != 0 : op == "<" ? c < 0 : op == "<=" ? c <= 0 : op == ">" ? c > 0 : c >= 0;
}
}
std::optional<int> compareValues(const Type& type,const Value& a,const Value& b) {
    if (type.kind == Type::Float) return floatCompare(a.number,b.number,type.bits);
    if (a.kind == Value::Elements && b.kind == Value::Elements) {
        for (std::size_t i=0;i<std::min(a.elements.size(),b.elements.size());++i) {
            auto order=compareValues(type.kind==Type::Array ? *type.element : type.fields.at(i),a.elements[i],b.elements[i]);
            if (!order || *order) return order;
        }
        return a.elements.size()<b.elements.size() ? -1 : a.elements.size()>b.elements.size() ? 1 : 0;
    }
    return compareValues(a,b);
}
int compareValues(const Value& a, const Value& b) {
    if (a.kind == Value::Number && b.kind == Value::Number) return a.number < b.number ? -1 : a.number > b.number ? 1 : 0;
    if (a.kind == Value::Bytes && b.kind == Value::Bytes) {
        auto less = [](const std::string& x, const std::string& y) {
            return std::lexicographical_compare(x.begin(), x.end(), y.begin(), y.end(),
                [](unsigned char c, unsigned char d) { return c < d; });
        };
        return less(a.bytes, b.bytes) ? -1 : less(b.bytes, a.bytes) ? 1 : 0;
    }
    if (a.kind == Value::Elements && b.kind == Value::Elements) {
        for (std::size_t i = 0; i < std::min(a.elements.size(), b.elements.size()); ++i)
            if (int result = compareValues(a.elements[i], b.elements[i])) return result;
        return a.elements.size() < b.elements.size() ? -1 : a.elements.size() > b.elements.size() ? 1 : 0;
    }
    throw std::logic_error("comparison of invalid value shapes");
}
Type Solver::common(const Type& a, const Type& b, Id at) const {
    Type left=resolvedType(a),right=resolvedType(b);
    if(!(left==a) || !(right==b)) return common(left,right,at);
    if (a == b) return a;
    if(a.kind==Type::Unknown && a.obligation!=none) requireTypeProperties(a);
    if(b.kind==Type::Unknown && b.obligation!=none) requireTypeProperties(b);
    if (a.kind == Type::Unknown) return b;
    if (b.kind == Type::Unknown) return a;
    if (a.kind == Type::Exact && (numeric(b) || b.kind == Type::Boolean)) return b;
    if (b.kind == Type::Exact && (numeric(a) || a.kind == Type::Boolean)) return a;
    if (a.kind == Type::Integer && b.kind == Type::Integer && a.sign == b.sign)
        return Type::integer(std::max(a.bits, b.bits), a.sign);
    if (a.kind == Type::Float && b.kind == Type::Float) return Type::floating(std::max(a.bits,b.bits));
    if (a.kind == Type::Array && b.kind == Type::Array && a.size == b.size)
        return Type::array(common(*a.element, *b.element, at), a.size);
    if (a.kind == Type::Record && b.kind == Type::Record && a.declaration == b.declaration && a.fieldNames == b.fieldNames) {
        requireTypeProperties(a);requireTypeProperties(b);
        if(a.schema!=b.schema) fail(at,"record types have different captured schema values");
        Type out = a;
        for (std::size_t i = 0; i < a.fields.size(); ++i) out.fields[i] = common(a.fields[i], b.fields[i], at);
        return out;
    }
    fail(at, "incompatible types: " + a.str() + " and " + b.str());
}
Fact Solver::convert(const Fact& f, const Type& target, bool cast, Id at) const {
    Type from=resolvedType(f.type),to=resolvedType(target);
    if(!(from==f.type) || !(to==target)) return convert({from,f.value,f.effect},to,cast,at);
    requireTypeProperties(f.type);requireTypeProperties(target);
    Fact result = f; result.type = target;
    if (f.type == target) return result;
    if (cast && representationCast(f.type,target)) return representation(f,target,at);
    if (target.kind == Type::Record && f.type.kind == Type::Record && target.declaration == f.type.declaration &&
        target.fieldNames == f.type.fieldNames) {
        if(target.schema!=f.type.schema) fail(at,"record types have different captured schema values");
        std::vector<Type> fields; std::vector<Value> values;
        for (std::size_t i = 0; i < target.fields.size(); ++i) {
            Type field = target.fields[i].kind == Type::Unknown ? f.type.fields[i] : target.fields[i];
            Fact item{f.type.fields[i], f.value.kind == Value::Elements ? f.value.elements[i] : Value::runtime(), f.effect};
            Fact converted = convert(item, field, cast, at);
            fields.push_back(converted.type); values.push_back(converted.value); result.effect=std::max(result.effect,converted.effect);
        }
        result.type.fields = std::move(fields);
        result.value = f.value.kind == Value::Elements ? Value::array(std::move(values)) : Value::runtime();
        return result;
    }
    if (target.kind == Type::Array && f.type.kind == Type::Array) {
        if (target.size != f.type.size) fail(at, "array shape mismatch: " + f.type.str() + " to " + target.str());
        // An open record element requirement is specialized by the supplied
        // element interface even when the array has no runtime elements.
        result.type=Type::array(conversionType(*f.type.element,*target.element,cast,at),target.size);
        if (f.value.kind == Value::Elements) {
            result.value.elements.clear();
            for (const auto& item : f.value.elements) {
                Fact converted=convert({*f.type.element,item,Effect::Pure},*target.element,cast,at);
                result.value.elements.push_back(converted.value); result.effect=std::max(result.effect,converted.effect);
            }
            // Empty arrays still have element-type constraints if already typed.
            if (f.value.elements.empty() && f.type.element->kind != Type::Unknown)
                (void)convert({*f.type.element, Value::runtime(), Effect::Pure}, *target.element, cast, at);
        } else result.effect=std::max(result.effect,convert({*f.type.element, Value::runtime(), Effect::Pure}, *target.element, cast, at).effect);
        return result;
    }
    bool sourceNumeric = numeric(f.type) || f.type.kind == Type::Boolean;
    if (f.value.symbolic()) return relation({f}, at, [&](const std::vector<Fact>& values) {
        return convert(values[0], target, cast, at);
    }, target);
    bool targetNumeric = numeric(target) || target.kind == Type::Boolean;
    if (!sourceNumeric || !targetNumeric || target.kind == Type::Exact)
        fail(at, "cannot convert " + f.type.str() + " to " + target.str());
    if (f.type.kind == Type::Exact && !f.value.known())
        fail(at, "runtime exact integer has no concrete representation; type its branch values or operands");
    if (f.type.kind == Type::Float || target.kind == Type::Float) {
        if (!cast && !(target.kind == Type::Float && (f.type.kind == Type::Exact ||
            (f.type.kind == Type::Float && f.type.bits <= target.bits))))
            fail(at,"implicit floating conversion requires a widening float or representable integer literal");
        if (f.value.kind == Value::Number) {
            FloatParts value;
            if (f.type.kind == Type::Float) value=splitFloat(f.value.number,f.type.bits);
            else {
                value.negative=f.value.number<0;
                value.numerator=value.negative ? -f.value.number : f.value.number;
                value.denominator=Big(1)<<f.type.fraction;
            }
            if (target.kind == Type::Float) {
                result.value.number=joinFloat(value,target.bits);
                if (!cast && f.type.kind == Type::Exact) {
                    FloatParts actual=splitFloat(result.value.number,target.bits);
                    if (actual.infinity || actual.numerator!=value.numerator*actual.denominator)
                        fail(at,"integer literal is not exactly representable as "+target.str()+"; use an explicit cast to round");
                }
            } else if (target.kind == Type::Boolean) result.value.number=value.nan || value.infinity || value.numerator!=0 ? 1 : 0;
            else {
                if (value.nan || value.infinity) fail(at,"non-finite float has no integer or fixed-point representation",Diagnostic::ValueTrap);
                Big raw=(value.numerator<<target.fraction)/value.denominator;
                if (value.negative) raw=-raw;
                result.value.number=wrap(raw,target);
            }
        } else if (target.kind != Type::Float && target.kind != Type::Boolean)
            result.effect=std::max(result.effect,Effect::Control);
        return result;
    }
    if (f.type.kind == Type::Fixed || target.kind == Type::Fixed) {
        if (!cast && f.type.kind != Type::Exact)
            fail(at,"fixed-point conversion requires an explicit cast unless the types match exactly");
        if (f.value.kind == Value::Number) {
            if (target.kind == Type::Boolean) result.value.number = f.value.number != 0 ? 1 : 0;
            else {
                unsigned fromFraction = f.type.kind == Type::Fixed ? f.type.fraction : 0;
                unsigned toFraction = target.kind == Type::Fixed ? target.fraction : 0;
                Big raw = f.value.number;
                if (toFraction >= fromFraction) raw <<= toFraction-fromFraction;
                else raw /= Big(1) << (fromFraction-toFraction); // Signed rescaling truncates toward zero.
                if (!cast && !fits(raw,target)) fail(at,"value "+f.value.number.str()+" is not representable as "+target.str(),Diagnostic::ValueTrap);
                result.value.number = cast ? wrap(raw,target) : raw;
            }
        }
        return result;
    }
    if (!cast) {
        if (f.type.kind != Type::Exact &&
            !(f.type.kind == Type::Integer && target.kind == Type::Integer &&
              f.type.sign == target.sign && f.type.bits <= target.bits))
            fail(at, "implicit conversion requires matching families without narrowing: " + f.type.str() + " to " + target.str());
        if (f.value.kind == Value::Number && !fits(f.value.number, target))
            fail(at, "value " + f.value.number.str() + " is not representable as " + target.str(),Diagnostic::ValueTrap);
    }
    if (f.value.kind == Value::Number) {
        if (target.kind == Type::Boolean) result.value.number = f.value.number != 0 ? 1 : 0;
        else result.value.number = cast ? wrap(f.value.number, target) : f.value.number;
    }
    return result;
}
Fact Solver::unary(const std::string& op, const Fact& value, Id at) const {
    if (op == "|" && value.type.kind == Type::Array)
        return {Type::simple(Type::Exact), Value::integer(value.type.size), value.effect};
    if (value.value.symbolic()) return relation({value}, at, [&](const std::vector<Fact>& values) {
        return unary(op, values[0], at);
    }, op == "!" ? Type::simple(Type::Boolean) : value.type);
    if (op == "!") {
        Fact b = convert(value, Type::simple(Type::Boolean), false, at);
        if (b.value.known()) b.value.number = b.value.number == 0 ? 1 : 0;
        return b;
    }
    if (op == "|" && value.type.kind == Type::String) {
        if (!value.value.known()) fail(at, "string length requires a constant string");
        return {Type::simple(Type::Exact), Value::integer(value.value.bytes.size()), value.effect};
    }
    if (value.type.kind == Type::Float) {
        if (op != "+" && op != "-" && op != "|") fail(at,"floating-point unary operator must be +, - or absolute value");
        Fact result=value;
        if (value.value.known()) result.value.number=floatUnary(op,value.value.number,value.type.bits);
        return result;
    }
    if (!numeric(value.type)) fail(at, "unary operator requires integer, array length, or string length");
    if (op == "+" || !value.value.known() || (op == "|" && value.value.number >= 0)) return value;
    Fact zero{value.type, Value::integer(op == "~" ? -1 : 0), Effect::Pure};
    return binary(op == "~" ? "^" : "-", zero, value, at);
}
Fact Solver::binary(const std::string& op, const Fact& left, const Fact& right, Id at) const {
    if (op == ".." && (left.value.symbolic() || right.value.symbolic()))
        requestChoice(left.value.symbolic() ? left.value : right.value,at);
    if (op == "&&" || op == "||") {
        Fact a = convert(left, Type::simple(Type::Boolean), false, at), b = convert(right, Type::simple(Type::Boolean), false, at);
        if (a.value.symbolic() || b.value.symbolic() || guarded(at)) return relation({a,b}, at, [&](const std::vector<Fact>& inputs) {
            return binary(op, inputs[0], inputs[1], at);
        }, Type::simple(Type::Boolean));
        Value value = a.value.known() && b.value.known() ? Value::integer(op == "&&" ?
            a.value.number != 0 && b.value.number != 0 : a.value.number != 0 || b.value.number != 0) : Value::runtime();
        return {Type::simple(Type::Boolean), value, std::max(a.effect, b.effect)};
    }
    if (left.value.symbolic() || right.value.symbolic() || guarded(at))
        return relation({left, right}, at, [&](const std::vector<Fact>& values) {
            return binary(op, values[0], values[1], at);
        }, (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") ?
            Type::simple(Type::Boolean) : (op == "<<" || op == ">>") ? left.type : common(left.type, right.type, at));
    Effect effects = std::max(left.effect, right.effect);
    const bool comparison = op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
    if (op == "..") {
        if (!integral(left.type) || !integral(right.type)) fail(at, "range bounds must be integers");
        if (!left.value.known() || !right.value.known()) fail(at, "range bounds must be compile-time constants");
        Type t = common(left.type, right.type, at);
        Fact a = convert(left, t, false, at), b = convert(right, t, false, at);
        Big count = a.value.number >= b.value.number ? a.value.number - b.value.number : b.value.number - a.value.number;
        if (count == 0) fail(at, "empty range; use [] for an empty array");
        if (count > g.options.maxElements) fail(at, "range exceeds element budget",Diagnostic::Exhausted);
        std::vector<Value> values;
        Big n = a.value.number;
        for (std::size_t i = 0, size = count.convert_to<std::size_t>(); i < size; ++i) {
            values.push_back(Value::integer(n));
            if (a.value.number < b.value.number) ++n; else --n;
        }
        return {Type::array(t, values.size()), Value::array(std::move(values)), effects};
    }
    if (comparison && left.type.kind == Type::Record && right.type.kind == Type::Record) {
        Type type = common(left.type, right.type, at);
        Fact a = convert(left, type, false, at), b = convert(right, type, false, at);
        if (!a.value.known() || !b.value.known()) return {Type::simple(Type::Boolean), Value::runtime(), effects};
        bool v = comparisonResult(op,compareValues(type,a.value,b.value));
        return {Type::simple(Type::Boolean), Value::integer(v ? 1 : 0), effects};
    }
    if (comparison && left.type.kind == Type::Array && right.type.kind == Type::Array) {
        Type element = common(*left.type.element, *right.type.element, at);
        Fact a = convert(left, Type::array(element, left.type.size), false, at);
        Fact b = convert(right, Type::array(element, right.type.size), false, at);
        if (!a.value.known() || !b.value.known()) return {Type::simple(Type::Boolean), Value::runtime(), effects};
        bool v = comparisonResult(op,compareValues(a.type,a.value,b.value));
        return {Type::simple(Type::Boolean), Value::integer(v ? 1 : 0), effects};
    }
    const bool shift = op == "<<" || op == ">>";
    Type t = shift ? left.type : common(left.type, right.type, at);
    Fact a = convert(left, t, false, at);
    Fact b = shift ? right : convert(right, t, false, at);
    Type out = comparison ? Type::simple(Type::Boolean) : t;
    if (comparison) {
        if (!numeric(t) && t.kind != Type::Boolean && t.kind != Type::String) fail(at, "unsupported comparison type: " + t.str());
    } else if (!numeric(t) || !numeric(b.type)) fail(at, "operator '" + op + "' requires numeric operands");
    if (t.kind == Type::Float) {
        if (!comparison && op!="+" && op!="-" && op!="*" && op!="/" && op!="%") fail(at,"unsupported floating-point operator: "+op);
        if (!a.value.known() || !b.value.known()) return {out,Value::runtime(),effects};
        return {out,Value::integer(comparison ? Big(comparisonResult(op,floatCompare(a.value.number,b.value.number,t.bits))) :
            floatBinary(op,a.value.number,b.value.number,t.bits)),effects};
    }
    if (shift && !integral(b.type)) fail(at,"shift count must be an integer");
    if ((op == "/" || op == "%") && b.value.kind == Value::Number && b.value.number == 0) fail(at, "division by zero",Diagnostic::ValueTrap);
    if (shift && b.value.kind == Value::Number &&
        (b.value.number < 0 || b.value.number >= (t.kind == Type::Integer || t.kind == Type::Fixed ? t.bits : g.options.maxBits)))
        fail(at, "shift count is outside the operand width or solver bit budget",
             b.value.number<0 || t.kind!=Type::Exact ? Diagnostic::ValueTrap : Diagnostic::Exhausted);
    if ((shift || op == "/" || op == "%") && !b.value.known())
        effects = std::max(effects, Effect::Control); // A possible trap is observable control.
    if (!a.value.known() || !b.value.known()) return {out, Value::runtime(), effects};
    if (comparison) {
        bool v = comparisonResult(op,compareValues(t,a.value,b.value));
        return {out, Value::integer(v ? 1 : 0), effects};
    }
    const Big& x = a.value.number; const Big& y = b.value.number;
    Big v;
    if (op == "+") v = x + y;
    else if (op == "-") v = x - y;
    else if (op == "*") { v = x * y; if (t.kind == Type::Fixed) v /= Big(1) << t.fraction; }
    else if (op == "/") { v = x; if (t.kind == Type::Fixed) v <<= t.fraction; v /= y; }
    else if (op == "%") v = x % y;
    else if (op == "&") v = x & y;
    else if (op == "|") v = x | y;
    else if (op == "^") v = x ^ y;
    else if (op == "<<") v = x << y.convert_to<unsigned>();
    else if (op == ">>") v = x >> y.convert_to<unsigned>();
    else fail(at, "unsupported integer operator: " + op);
    v = wrap(v, out);
    Big magnitude = v < 0 ? -v : v;
    if (magnitude != 0 && boost::multiprecision::msb(magnitude) >= g.options.maxBits) fail(at, "integer exceeds solver bit budget",Diagnostic::Exhausted);
    return {out, Value::integer(v), effects};
}
} // namespace vx
