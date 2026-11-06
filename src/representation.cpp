#include "vexel.hpp"
#include <algorithm>
#include <limits>

namespace vx {
namespace {
bool bytes(const Type& t) { return t.kind == Type::Array && *t.element == Type::integer(8,false); }
bool bits(const Type& t) { return t.kind == Type::Array && t.element->kind == Type::Boolean; }
bool unsignedInteger(const Type& t) { return t.kind == Type::Integer && !t.sign; }
bool hasBoolean(const Type& t) {
    if (t.kind == Type::Boolean) return true;
    if (t.kind == Type::Array) return t.size && hasBoolean(*t.element);
    return t.kind == Type::Record && std::any_of(t.fields.begin(),t.fields.end(),hasBoolean);
}
}
bool representationCast(const Type& from,const Type& to) {
    if ((bits(from) && unsignedInteger(to)) || (unsignedInteger(from) && bits(to))) return true;
    return (bytes(from) != bytes(to)) && from.concrete() && to.concrete();
}
std::optional<std::size_t> representationBytes(const Type& type) {
    if (type.kind == Type::Integer || type.kind == Type::Fixed || type.kind == Type::Float) return (static_cast<std::size_t>(type.bits)+7)/8;
    if (type.kind == Type::Boolean) return 1;
    if (type.kind == Type::Array) {
        auto element = representationBytes(*type.element);
        if (!element || (type.size && *element > std::numeric_limits<std::size_t>::max()/type.size)) return {};
        return *element*type.size;
    }
    if (type.kind == Type::Record) {
        std::size_t result=0;
        for (const Type& field : type.fields) {
            auto size=representationBytes(field);
            if (!size || *size>std::numeric_limits<std::size_t>::max()-result) return {};
            result+=*size;
        }
        return result;
    }
    return {};
}
Fact Solver::representation(const Fact& source,const Type& target,Id at) const {
    bool packed = (bits(source.type) && unsignedInteger(target)) || (unsignedInteger(source.type) && bits(target));
    if (packed) {
        std::size_t count = bits(source.type) ? source.type.size : target.size;
        unsigned width = unsignedInteger(source.type) ? source.type.bits : target.bits;
        if (count != width) fail(at,"boolean array cast requires exactly one element per integer bit");
    } else {
        auto from=representationBytes(source.type), to=representationBytes(target);
        if (!from || !to) fail(at,"byte representation requires concrete fixed-size scalar, array or record fields");
        if (*from != *to) fail(at,"byte representation size mismatch: "+std::to_string(*from)+" versus "+std::to_string(*to));
        if (*from > g.options.maxElements) fail(at,"byte representation exceeds element budget",Diagnostic::Exhausted);
    }
    if (source.value.symbolic()) return relation({source},at,[&](const std::vector<Fact>& inputs) {
        return representation(inputs[0],target,at);
    },target);
    if (!source.value.known()) return {target,Value::runtime(),
        bytes(source.type) && hasBoolean(target) ? std::max(source.effect,Effect::Control) : source.effect};
    if (packed) {
        if (bits(source.type)) {
            Big value=0;
            for (std::size_t i=0;i<source.type.size;++i) if (source.value.elements[i].number != 0) value|=Big(1)<<i;
            return {target,Value::integer(value),source.effect};
        }
        std::vector<Value> result;
        for (std::size_t i=0;i<target.size;++i) result.push_back(Value::integer((source.value.number>>i)&1));
        return {target,Value::array(std::move(result)),source.effect};
    }
    std::vector<unsigned> buffer;
    std::function<void(const Type&,const Value&)> encode = [&](const Type& type,const Value& value) {
        if (type.kind == Type::Array) {
            for (const Value& item : value.elements) encode(*type.element,item);
        } else if (type.kind == Type::Record) {
            for (std::size_t i=0;i<type.fields.size();++i) encode(type.fields[i],value.elements[i]);
        } else {
            unsigned width=type.kind==Type::Boolean ? 1 : type.bits;
            Big raw=value.number & ((Big(1)<<width)-1);
            for (std::size_t i=0;i<*representationBytes(type);++i) buffer.push_back(((raw>>(i*8))&255).convert_to<unsigned>());
        }
    };
    encode(source.type,source.value);
    std::size_t offset=0;
    std::function<Value(const Type&)> decode = [&](const Type& type) -> Value {
        if (type.kind==Type::Array || type.kind==Type::Record) {
            std::vector<Value> values;
            for (std::size_t i=0;i<type.size;++i) values.push_back(decode(type.kind==Type::Array ? *type.element : type.fields[i]));
            return Value::array(std::move(values));
        }
        Big raw=0;
        std::size_t count=*representationBytes(type);
        for (std::size_t i=count;i>0;--i) { raw<<=8;raw+=buffer.at(offset+i-1); }
        offset+=count;
        if (type.kind==Type::Boolean) {
            if (raw>1) fail(at,"value "+raw.str()+" is not representable as a boolean byte",Diagnostic::ValueTrap);
        } else {
            raw &= (Big(1)<<type.bits)-1;
            if (type.sign && (raw & (Big(1)<<(type.bits-1)))!=0) raw-=Big(1)<<type.bits;
        }
        return Value::integer(raw);
    };
    return {target,decode(target),source.effect};
}
} // namespace vx
