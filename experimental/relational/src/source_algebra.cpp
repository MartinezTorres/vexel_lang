#include "source_algebra.hpp"
#include <algorithm>
#include <cstdint>
#include <iterator>

namespace vx::relational {
namespace {
struct Limit {};
struct Format {unsigned width;bool sign;};
std::optional<Format> operationFormat(const Ref& input) {
    auto value=input;
    if(value->name=="Runtime") value=value->arguments[0];
    const auto& name=value->name;const auto& args=value->arguments;
    std::size_t at;
    if(name=="Arithmetic" || name=="SignedArithmetic") {
        if(args.size()!=4 || (args[0]->name!="+" && args[0]->name!="-" && args[0]->name!="*")) return {};
        at=1;
    } else if(name=="Cast" || name=="SignedCast") {
        if(args.size()!=2) return {};
        at=0;
    } else return {};
    if(args[at]->kind!=Term::Number || args[at]->number<1 || args[at]->number>32) return {};
    return Format{args[at]->number.convert_to<unsigned>(),name.starts_with("Signed")};
}
std::optional<Format> valueFormat(const Ref& input) {
    if(input->name!="Runtime" || input->arguments.size()!=3) return {};
    const auto& lo=input->arguments[1];const auto& hi=input->arguments[2];
    if(lo->kind!=Term::Number || hi->kind!=Term::Number || lo->number>hi->number) return {};
    const bool sign=lo->number<0;
    for(unsigned width=1;width<=32;++width)
        if(lo->number>=(sign ? -(Integer(1)<<(width-1)) : Integer(0)) &&
           hi->number<(Integer(1)<<(width-(sign ? 1 : 0)))) return Format{width,sign};
    return {};
}
bool fits(const Integer& value,Format format) {
    return value>=(format.sign ? -(Integer(1)<<(format.width-1)) : Integer(0)) &&
        value<(Integer(1)<<(format.width-(format.sign ? 1 : 0)));
}
class Polynomial {
    using Coefficient=std::uint32_t;
    using Monomial=std::vector<ModularForm::Atom>;
    using Sum=std::map<Monomial,Coefficient>;
    unsigned width;
    Coefficient mask;
    TermIndex& identities;
    std::map<Id,Ref>* leaves;
    std::map<Id,Sum> cache;
    std::map<Id,Sum> predicates;
    std::size_t work=0;
    void step() {if(++work>4096) throw Limit{};}
    Coefficient reduce(const Integer& number) const {
        const Integer modulus=Integer(1)<<width;
        Integer remainder=number%modulus;
        if(remainder<0) remainder+=modulus;
        return remainder.convert_to<Coefficient>();
    }
    void add(Sum& sum,const Monomial& powers,Coefficient value) {
        step();if(!value) return;
        auto found=sum.find(powers);
        if(found==sum.end()) {
            if(sum.size()>=64 || powers.size()>16) throw Limit{};
            sum.emplace(powers,value);
        } else {
            found->second=(found->second+value)&mask;
            if(!found->second) sum.erase(found);
        }
    }
    Sum atom(const Ref& input,bool truth=false) {
        auto id=identities.intern(input);
        if(leaves && !truth) leaves->try_emplace(id,input);
        return {{{{id,truth}},1}};
    }
    Sum combine(Sum left,const Sum& right,bool subtract=false) {
        for(const auto& [powers,coefficient]:right)
            add(left,powers,subtract ? (Coefficient(0)-coefficient)&mask : coefficient);
        return left;
    }
    Sum multiply(const Sum& left,const Sum& right) {
        Sum result;
        for(const auto& [aPowers,aCoefficient]:left) for(const auto& [bPowers,bCoefficient]:right) {
            if(aPowers.size()+bPowers.size()>16) throw Limit{};
            Monomial powers;powers.reserve(aPowers.size()+bPowers.size());
            std::merge(aPowers.begin(),aPowers.end(),bPowers.begin(),bPowers.end(),std::back_inserter(powers));
            // Truth atoms are zero or one, so repeated factors are idempotent.
            std::size_t retained=0;
            for(const auto& factor:powers) {
                if(factor.second && retained && factor==powers[retained-1]) continue;
                powers[retained++]=factor;
            }
            powers.resize(retained);
            auto coefficient=static_cast<Coefficient>(std::uint64_t(aCoefficient)*bCoefficient)&mask;
            add(result,powers,coefficient);
        }
        return result;
    }
    Sum truth(const Ref& input) {
        step();auto key=identities.intern(input);
        if(auto found=predicates.find(key);found!=predicates.end()) return found->second;
        auto value=input;
        if(value->name=="Runtime") value=value->arguments[0];
        const auto& a=value->arguments;Sum result;
        if(value->kind==Term::Number) {if(value->number!=0) result={{ {},1 }};}
        else if(value->name=="After") result=truth(a[1]);
        else if(value->name=="Not") result=combine(Sum{{{},1}},truth(a[0]),true);
        else if(value->name=="And") result=multiply(truth(a[0]),truth(a[1]));
        else result=atom(input,true);
        predicates.emplace(key,result);return result;
    }
    Sum expression(const Ref& input) {
        if(input->kind==Term::Number) {auto n=reduce(input->number);return n ? Sum{{{},n}} : Sum{};}
        auto value=input;
        if(value->name=="Runtime") value=value->arguments[0];
        if(value->kind==Term::Number) return normalize(value);
        const auto& a=value->arguments;
        if(value->name=="After") return normalize(a[1]);
        if(value->name=="Select") {
            // A conditional value is f + truth(c)*(t-f). Truth atoms cannot
            // alias raw integer atoms. The resulting expression is proof-only;
            // no branch evaluation is moved into the emitted program.
            auto condition=truth(a[0]),yes=normalize(a[1]),no=normalize(a[2]);
            return combine(no,multiply(condition,combine(std::move(yes),no,true)));
        }
        auto format=operationFormat(value);
        // Low bits survive a wider wrap and either signed interpretation. A
        // narrower wrap loses information needed by this ring: keep it opaque.
        if(!format || format->width<width) return atom(input);
        if(value->name=="Cast" || value->name=="SignedCast") return normalize(a[1]);
        auto left=normalize(a[2]),right=normalize(a[3]);
        return a[0]->name=="*" ? multiply(left,right) : combine(std::move(left),right,a[0]->name=="-");
    }
public:
    Polynomial(unsigned bits,TermIndex& index,std::map<Id,Ref>* atoms=nullptr):width(bits),mask(bits==32 ? ~Coefficient(0) : (Coefficient(1)<<bits)-1),identities(index),leaves(atoms) {}
    Sum normalize(const Ref& value) {
        step();auto key=identities.intern(value);
        if(auto found=cache.find(key);found!=cache.end()) return found->second;
        auto result=expression(value);cache.emplace(key,result);return result;
    }
    std::optional<Integer> constant(const Ref& input,bool sign) {
        const auto sum=normalize(input);
        if(sum.empty()) return Integer(0);
        if(sum.size()!=1 || !sum.begin()->first.empty()) return {};
        Integer result=sum.begin()->second;
        if(sign && result>=(Integer(1)<<(width-1))) result-=Integer(1)<<width;
        return result;
    }
    bool equivalent(const Ref& left,const Ref& right) {return normalize(left)==normalize(right);}
};
}
std::optional<Integer> modularConstant(const Ref& value,TermIndex& identities) {
    auto format=operationFormat(value);if(!format) return {};
    try {return Polynomial(format->width,identities).constant(value,format->sign);}
    catch(const Limit&) {return {};}
}
bool modularEquivalent(const Ref& left,const Ref& right,TermIndex& identities) {
    if(left->kind==Term::Number && right->kind==Term::Number) return left->number==right->number;
    auto first=valueFormat(left),second=valueFormat(right);
    if(!first && left->kind==Term::Number && second && fits(left->number,*second)) first=second;
    if(!second && right->kind==Term::Number && first && fits(right->number,*first)) second=first;
    if(!first || !second || first->width!=second->width || first->sign!=second->sign) return false;
    try {return Polynomial(first->width,identities).equivalent(left,right);}
    catch(const Limit&) {return false;}
}
std::optional<ModularForm> modularForm(const Ref& value,unsigned width,bool sign,TermIndex& identities) {
    if(!width || width>32) return {};
    try {return ModularForm{width,sign,Polynomial(width,identities).normalize(value)};}
    catch(const Limit&) {return {};}
}
std::optional<Ref> modularReduction(Terms& terms,const Ref& value,TermIndex& identities) {
    auto format=operationFormat(value);if(!format) return {};
    try {
        std::map<Id,Ref> leaves;
        const auto sum=Polynomial(format->width,identities,&leaves).normalize(value);
        if(sum.empty()) return terms.number(0);
        if(sum.size()!=1) return {};
        const auto& [powers,coefficient]=*sum.begin();
        if(powers.empty()) {
            Integer constant=coefficient;
            if(format->sign && constant>=(Integer(1)<<(format->width-1))) constant-=Integer(1)<<format->width;
            return terms.number(constant);
        }
        if(coefficient!=1 || powers.size()!=1 || powers[0].second) return {};
        auto result=terms.make(format->sign ? "SignedCast" : "Cast",
            {terms.number(format->width),leaves.at(powers[0].first)});
        return identical(result,value) ? std::optional<Ref>{} : result;
    } catch(const Limit&) {return {};}
}
}
