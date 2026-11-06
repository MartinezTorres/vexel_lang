#include "floating.hpp"
#include <algorithm>
#include <boost/multiprecision/integer.hpp>

namespace vx {
Type Solver::mathType(const std::string& name,const std::vector<Type>& inputs,Id at) const {
    unsigned op=mathOperation(name);
    bool binary=op==5 || op==6 || op==7 || op==15 || op==16;
    if(inputs.size()!=(binary ? 2u : 1u)) fail(at,"wrong argument count for "+name);
    auto numeric=[](const Type& t) {return t.kind==Type::Exact || t.kind==Type::Integer || t.kind==Type::Fixed || t.kind==Type::Float;};
    for(const Type& t:inputs) if(!numeric(t)) fail(at,name+" requires numeric scalar elements");
    Type result=inputs[0];
    if(binary && op!=16) result=common(result,inputs[1],at);
    if(op==0 || op==7 || (op>=8 && op<=11)) {
        if(result.kind!=Type::Float) fail(at,name+" requires floating-point arguments");
    }
    if(op==12 && result.kind!=Type::Integer && result.kind!=Type::Exact) fail(at,"isqrt requires an integer");
    if(op==16) (void)conversionType(inputs[1],Type::integer(32,false),false,at);
    return op>=8 && op<=11 ? Type::simple(Type::Boolean) : result;
}
Fact Solver::mathValue(const std::string& name,const std::vector<Fact>& inputs,Id at) const {
    std::vector<Type> types;Effect effect=Effect::Pure;bool symbolic=false,known=true;
    for(const Fact& input:inputs) {
        types.push_back(input.type);effect=std::max(effect,input.effect);
        symbolic=symbolic || input.value.symbolic();known=known && input.value.known();
    }
    Type output=mathType(name,types,at);unsigned op=mathOperation(name);
    if(symbolic || guarded(at)) return relation(inputs,at,[&](const std::vector<Fact>& values) {
        return mathValue(name,values,at);
    },output);
    if(op==14) return unary("|",inputs[0],at);
    if(op==15) return binary("%",inputs[0],inputs[1],at);
    if(op==16) {
        Fact exponent=convert(inputs[1],Type::integer(32,false),false,at);
        if(!known) return {output,Value::runtime(),effect};
        uint32_t count=exponent.value.number.convert_to<uint32_t>();
        Fact result=output.kind==Type::Exact ? Fact{output,Value::integer(1),effect} :
            convert({Type::simple(Type::Exact),Value::integer(1),effect},output,true,at),base=inputs[0];
        while(count) {
            if(count&1) result=binary("*",result,base,at);
            count>>=1;if(count) base=binary("*",base,base,at);
        }
        result.effect=effect;return result;
    }
    if(op==12) {
        if(known && inputs[0].value.number<0) fail(at,"isqrt requires a nonnegative value",Diagnostic::ValueTrap);
        if(!known) return {output,Value::runtime(),output.sign ? std::max(effect,Effect::Control) : effect};
        return {output,Value::integer(boost::multiprecision::sqrt(inputs[0].value.number)),effect};
    }
    Type operand=output.kind==Type::Boolean ? inputs[0].type : output;
    Fact a=convert(inputs[0],operand,false,at),b;
    if(inputs.size()==2) b=convert(inputs[1],operand,false,at);
    if(!known) return {output,Value::runtime(),effect};
    if(operand.kind==Type::Float)
        return {output,Value::integer(floatMath(op,a.value.number,b.value.number,operand.bits)),effect};
    if(op==5 || op==6) return {output,(op==5 ? a.value.number<b.value.number : a.value.number>=b.value.number) ? a.value : b.value,effect};
    if(operand.kind!=Type::Fixed) return {output,a.value,effect}; // Integers are already integral.
    Big scale=Big(1)<<operand.fraction,raw=a.value.number,absolute=raw<0 ? Big(-raw) : raw;
    Big integral=absolute/scale,remainder=absolute%scale;
    bool increment=remainder!=0 && ((op==1 && raw<0) || (op==2 && raw>0) ||
        (op==4 && remainder*2>=scale) || (op==13 && (remainder*2>scale || (remainder*2==scale && (integral&1)!=0))));
    if(increment) ++integral;
    Big result=integral*scale;if(raw<0) result=-result;
    // Rounding preserves the declared format, including its modular overflow.
    return binary("+",{output,Value::integer(result),effect},{output,Value::integer(0),Effect::Pure},at);
}
void Solver::mathRule(Id id,bool typesOnly) {
    auto& n=g.nodes[id];std::vector<Type> types;bool arrays=false;
    for(Id input:n.inputs) {
        const Type* t=needType(id,input);if(!t || !t->settled()) return;
        types.push_back(*t);arrays=arrays || t->kind==Type::Array;
    }
    if(arrays && n.expansion==none) {
        auto rank=[](Type t) {std::size_t r=0;while(t.kind==Type::Array) {++r;t=Type(*t.element);}return r;};
        std::function<Id(const std::vector<Id>&,const std::vector<Type>&)> expand;
        expand=[&](const std::vector<Id>& values,const std::vector<Type>& shapes)->Id {
            std::size_t depth=0;for(const Type& t:shapes) depth=std::max(depth,rank(t));
            if(depth==0) {
                (void)mathType(n.text,shapes,id);
                Id scalar=g.semantic(Rule::Math,n.site,values,n.scope);g.nodes[scalar].text=n.text;return scalar;
            }
            std::size_t count=1;
            for(const Type& t:shapes) if(rank(t)==depth && t.size!=1) {
                if(count!=1 && count!=t.size) fail(id,"incompatible math broadcast shapes");
                count=t.size;
            }
            std::vector<Id> items;
            for(std::size_t i=0;i<std::max<std::size_t>(count,1);++i) {
                std::vector<Id> elements;std::vector<Type> elementTypes;
                for(std::size_t j=0;j<values.size();++j) {
                    bool indexed=rank(shapes[j])==depth;
                    Type t=indexed ? *shapes[j].element : shapes[j];
                    Id value=values[j];
                    if(count==0) value=literal(n.site,t,Value::runtime());
                    else if(indexed) value=g.semantic(Rule::Index,n.site,{value,literal(n.site,Type::simple(Type::Exact),Value::integer(shapes[j].size==1 ? 0 : i))},n.scope);
                    elements.push_back(value);elementTypes.push_back(t);
                }
                items.push_back(expand(elements,elementTypes));
            }
            Id prototype=count==0 ? items[0] : none;if(count==0) items.clear();
            Id result=g.semantic(Rule::Array,n.site,std::move(items),n.scope);g.nodes[result].annotation=prototype;return result;
        };
        n.expansion=expand(n.inputs,types);
    }
    if(arrays) {
        if(typesOnly) {if(const Type* t=needType(id,n.expansion)) publishType(id,*t);return;}
        Effect effect=Effect::Pure;bool ready=true;
        for(Id input:n.inputs) {if(const Fact* f=need(id,input)) effect=std::max(effect,f->effect);else ready=false;}
        const Fact* result=need(id,n.expansion);if(!result || !ready) return;
        Fact fact=*result;fact.effect=std::max(fact.effect,effect);publish(id,std::move(fact));return;
    }
    Type result=mathType(n.text,types,id);
    if(typesOnly) {publishType(id,result);return;}
    std::vector<Fact> inputs;
    for(Id input:n.inputs) {const Fact* f=need(id,input);if(!f) return;inputs.push_back(*f);}
    publish(id,mathValue(n.text,inputs,id));
}
} // namespace vx
