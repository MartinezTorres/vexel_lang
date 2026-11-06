#include "source_control_proof.hpp"
#include <algorithm>

namespace vx::relational {
ControlProof::Bit ControlProof::node(const std::string& name,Bit low,Bit high) {
    if(low==high) return low;
    auto key=std::make_tuple(name,low,high);auto found=unique.find(key);
    if(found!=unique.end()) return found->second;
    if(nodes.size()>=100000) throw ConstructionLimit("control proof budget exhausted");
    auto id=nodes.size();nodes.push_back({name,low,high});unique.emplace(key,id);return id;
}
ControlProof::Bit ControlProof::atom(const std::string& name,Ref value) {
    atoms.emplace(name,std::move(value));return node(name,0,1);
}
ControlProof::Bit ControlProof::select(Bit condition,Bit yes,Bit no) {
    if(condition<2) return condition ? yes : no;
    if(yes==no) return yes;
    if(yes==1 && no==0) return condition;
    auto key=std::make_tuple(condition,yes,no);auto found=cache.find(key);
    if(found!=cache.end()) return found->second;
    std::string name=nodes[condition].atom;
    for(auto id:{yes,no}) if(id>1 && nodes[id].atom<name) name=nodes[id].atom;
    auto branch=[&](Bit id,bool high){return id>1 && nodes[id].atom==name ? (high ? nodes[id].high : nodes[id].low) : id;};
    auto low=select(branch(condition,false),branch(yes,false),branch(no,false));
    auto high=select(branch(condition,true),branch(yes,true),branch(no,true));
    auto id=node(name,low,high);cache.emplace(key,id);return id;
}
ControlProof::Bit ControlProof::parity(Bit a,Bit b) {return select(a,negate(b),b);}
ControlProof::Bit ControlProof::any(const Word& value) {
    Bit result=0;for(auto bit:value) result=disjunction(result,bit);return result;
}
ControlProof::Bit ControlProof::equal(const Word& a,const Word& b) {
    Bit result=1;for(unsigned i=0;i<32;++i) result=conjunction(result,equivalent(a[i],b[i]));return result;
}
ControlProof::Bit ControlProof::less(const Word& a,const Word& b) {
    Bit result=0;
    for(unsigned i=0;i<32;++i) result=select(equivalent(a[i],b[i]),result,conjunction(negate(a[i]),b[i]));
    return result;
}
ControlProof::Word ControlProof::constant(Integer value) {
    value&=(Integer(1)<<32)-1;Word result{};
    for(unsigned i=0;i<32;++i) result[i]=((value>>i)&1).convert_to<Bit>();
    return result;
}
ControlProof::Word ControlProof::choose(Bit condition,const Word& yes,const Word& no) {
    Word result{};for(unsigned i=0;i<32;++i) result[i]=select(condition,yes[i],no[i]);return result;
}
ControlProof::Word ControlProof::wrap(Word value,unsigned width,bool sign) {
    for(unsigned i=width;i<32;++i) value[i]=sign ? value[width-1] : 0;
    return value;
}
ControlProof::Word ControlProof::add(const Word& a,const Word& b,unsigned width,bool subtract) {
    Word result{};Bit carry=subtract ? 1 : 0;
    for(unsigned i=0;i<width;++i) {
        auto right=subtract ? negate(b[i]) : b[i];auto difference=parity(a[i],right);
        result[i]=parity(difference,carry);carry=select(difference,carry,a[i]);
    }
    return result;
}
std::optional<ControlProof::Word> ControlProof::primitive(const Ref& runtime) {
    const auto& args=runtime->arguments;
    if(args.size()!=3 || args[1]->kind!=Term::Number || args[2]->kind!=Term::Number) return {};
    const auto& lo=args[1]->number;const auto& hi=args[2]->number;
    if(lo>hi || lo<-(Integer(1)<<31) || hi>=(Integer(1)<<32) || (lo<0 && hi>=(Integer(1)<<31))) return {};
    const auto& source=args[0];auto identity=show(source);Word result{};
    auto bitName=[&](unsigned i){return "bit:"+std::string(i<10 ? "0" : "")+std::to_string(i)+":"+identity;};
    if(lo==hi) return constant(lo);
    // Every encoding denotes an in-range value. Clamping spare encodings to
    // the lower bound preserves a surjection onto the full declared interval,
    // without global assumptions that could leak across loop quantification.
    unsigned width=1;bool sign=lo<0;
    while(width<32 && (sign ? (lo<-(Integer(1)<<(width-1)) || hi>=(Integer(1)<<(width-1))) : hi>=(Integer(1)<<width))) ++width;
    for(unsigned i=0;i<width;++i) {
        auto bit=generated.make("RepresentationPart",{runtime,generated.number(i),generated.number(1)});
        result[i]=atom(bitName(i),bit);
    }
    result=wrap(result,width,sign);
    auto minimum=constant(lo),maximum=constant(hi),ordered=result;
    if(sign) {ordered[31]=negate(ordered[31]);minimum[31]=negate(minimum[31]);maximum[31]=negate(maximum[31]);}
    auto valid=conjunction(negate(less(ordered,minimum)),negate(less(maximum,ordered)));
    return choose(valid,result,constant(lo));
}
std::optional<ControlProof::Word> ControlProof::word(const Ref& value) {
    auto key=show(value);if(auto found=words.find(key);found!=words.end()) return found->second;
    auto result=expression(value);
    if(!result && value->name=="Runtime") result=primitive(value);
    words.emplace(std::move(key),result);return result;
}
std::optional<ControlProof::Word> ControlProof::expression(const Ref& value) {
    const auto& a=value->arguments;const auto& name=value->name;
    if(value->kind==Term::Number) {
        if(value->number<-(Integer(1)<<31) || value->number>=(Integer(1)<<32)) return {};
        return constant(value->number);
    }
    if(name=="Runtime") return word(a[0]);
    if(name=="After") return word(a[1]);
    if(name=="Index") {
        auto index=constantValue(a[0]);
        if(!index || *index<0 || *index>=a[1]->arguments.size()) return {};
        return word(a[1]->arguments[index->convert_to<std::size_t>()]);
    }
    if(name=="LoopReady" || name=="NextLoopReady" || name=="LoopFlag") {
        Word result{};result[0]=atom(show(value),value);return result;
    }
    if(name=="Not") {Word result{};result[0]=negate(read(a[0]));return result;}
    if(name=="And") {Word result{};result[0]=conjunction(read(a[0]),read(a[1]));return result;}
    if(name=="Select") {
        auto yes=word(a[1]),no=word(a[2]);if(!yes || !no) return {};
        return choose(read(a[0]),*yes,*no);
    }
    if(name=="Compare" || name=="SignedCompare") {
        bool sign=name=="SignedCompare";std::size_t first=sign ? 2 : 1;
        auto left=word(a[first]),right=word(a[first+1]);if(!left || !right) return {};
        if(sign) {
            auto width=a[1]->number.convert_to<unsigned>();if(width<1 || width>32) return {};
            *left=wrap(*left,width,true);*right=wrap(*right,width,true);
            (*left)[31]=negate((*left)[31]);(*right)[31]=negate((*right)[31]);
        }
        const auto& op=a[0]->name;Word result{};
        if(op=="==") result[0]=equal(*left,*right);
        else if(op=="!=") result[0]=negate(equal(*left,*right));
        else if(op=="<") result[0]=less(*left,*right);
        else if(op==">") result[0]=less(*right,*left);
        else if(op=="<=") result[0]=negate(less(*right,*left));
        else if(op==">=") result[0]=negate(less(*left,*right));
        else return {};
        return result;
    }
    if(name=="Cast" || name=="SignedCast" || name=="SignedAbsolute") {
        auto width=a[0]->number.convert_to<unsigned>();if(width<1 || width>32) return {};
        auto result=word(a[1]);if(!result) return {};
        if(name=="SignedAbsolute") *result=choose((*result)[width-1],add(constant(0),*result,width,true),*result);
        return wrap(*result,width,name!="Cast");
    }
    if(name=="Arithmetic" || name=="SignedArithmetic") {
        auto width=a[1]->number.convert_to<unsigned>();if(width<1 || width>32) return {};
        auto left=word(a[2]),right=word(a[3]);if(!left || !right) return {};
        const auto& op=a[0]->name;Word result{};bool sign=name=="SignedArithmetic";
        if(op=="+" || op=="-") result=add(*left,*right,width,op=="-");
        else if(op=="&" || op=="|" || op=="^") {
            for(unsigned i=0;i<width;++i) result[i]=op=="&" ? conjunction((*left)[i],(*right)[i]) :
                op=="|" ? disjunction((*left)[i],(*right)[i]) : parity((*left)[i],(*right)[i]);
        } else if(op=="*") {
            for(unsigned j=0;j<width;++j) {
                Word partial{};for(unsigned i=j;i<width;++i) partial[i]=conjunction((*left)[i-j],(*right)[j]);
                result=add(result,partial,width);
            }
        } else if(op=="/" || op=="%") {
            // A value relation cannot remove the definedness obligation. If
            // zero remains possible, keep the existing opaque expression.
            auto numerator=wrap(*left,width,false),denominator=wrap(*right,width,false);
            if(any(denominator)!=1) return {};
            const auto numeratorSign=sign ? numerator[width-1] : 0;
            const auto denominatorSign=sign ? denominator[width-1] : 0;
            numerator=choose(numeratorSign,add(constant(0),numerator,width,true),numerator);
            denominator=choose(denominatorSign,add(constant(0),denominator,width,true),denominator);
            Word remainder{},quotient{};
            for(unsigned step=width;step-->0;) {
                // The partial numerator contains at most width bits, so its
                // shifted remainder fits even for a 32-bit division.
                Word shifted{};shifted[0]=numerator[step];
                for(unsigned i=1;i<width;++i) shifted[i]=remainder[i-1];
                auto subtract=negate(less(shifted,denominator));
                remainder=choose(subtract,add(shifted,denominator,width,true),shifted);
                quotient[step]=subtract;
            }
            result=op=="%" ? remainder : quotient;
            auto negative=op=="%" ? numeratorSign : parity(numeratorSign,denominatorSign);
            result=choose(negative,add(constant(0),result,width,true),result);
        } else if(op=="<<" || op==">>") {
            // Check the complete count before truncating its representation;
            // negative and oversized counts still carry their trap obligation.
            if(less(*right,constant(width))!=1) return {};
            result=wrap(*left,width,sign);
            for(unsigned bit=0;(1U<<bit)<width;++bit) {
                const unsigned shift=1U<<bit;Word shifted{};
                for(unsigned i=0;i<width;++i) shifted[i]=op=="<<" ? (i>=shift ? result[i-shift] : 0) :
                    (i+shift<width ? result[i+shift] : sign ? result[width-1] : 0);
                result=choose((*right)[bit],shifted,result);
            }
        } else return {};
        return wrap(result,width,sign);
    }
    if(name=="RepresentationPart") {
        auto input=word(a[0]);if(!input || a[1]->kind!=Term::Number || a[2]->kind!=Term::Number) return {};
        auto shift=a[1]->number.convert_to<unsigned>();if(shift>=32) return {};
        auto mask=constant(a[2]->number);Word result{};
        for(unsigned i=0;i+shift<32;++i) result[i]=mask[i] ? (*input)[i+shift] : 0;
        return result;
    }
    return {};
}
ControlProof::Bit ControlProof::read(const Ref& value) {
    if(value->kind==Term::Number) return value->number!=0 ? 1 : 0;
    auto key=show(value);if(auto found=booleans.find(key);found!=booleans.end()) return found->second;
    Bit result;
    if(auto bits=word(value)) result=any(*bits);
    else if(value->name=="Runtime") result=read(value->arguments[0]);
    else if(value->name=="After") result=read(value->arguments[1]);
    else if(value->name=="Select") result=select(read(value->arguments[0]),read(value->arguments[1]),read(value->arguments[2]));
    else result=atom(key,value);
    booleans.emplace(std::move(key),result);return result;
}
std::optional<Integer> ControlProof::constantValue(const Ref& value) {
    auto bits=word(value);if(!bits) return {};
    Integer result=0;
    for(unsigned i=0;i<32;++i) {if((*bits)[i]>1) return {};if((*bits)[i]) result|=Integer(1)<<i;}
    if(value->name=="Runtime" && value->arguments[1]->kind==Term::Number && value->arguments[1]->number<0 && (*bits)[31]) result-=Integer(1)<<32;
    return result;
}
ControlProof::Bit ControlProof::exists(Bit value,const std::function<bool(const Ref&)>& remove) {
    std::map<Bit,Bit> memo;
    std::function<Bit(Bit)> visit=[&](Bit id) {
        if(id<2) return id;
        if(auto found=memo.find(id);found!=memo.end()) return found->second;
        const auto current=nodes[id];auto low=visit(current.low),high=visit(current.high);
        auto result=remove(atoms.at(current.atom)) ? disjunction(low,high) : node(current.atom,low,high);
        memo.emplace(id,result);return result;
    };return visit(value);
}
ControlProof::Bit ControlProof::substitute(Bit value,const std::map<std::string,Bit>& replacements) {
    std::map<Bit,Bit> memo;
    std::function<Bit(Bit)> visit=[&](Bit id) {
        if(id<2) return id;
        if(auto found=memo.find(id);found!=memo.end()) return found->second;
        const auto current=nodes[id];auto found=replacements.find(current.atom);
        auto condition=found==replacements.end() ? node(current.atom,0,1) : found->second;
        auto result=select(condition,visit(current.high),visit(current.low));memo.emplace(id,result);return result;
    };return visit(value);
}
std::optional<std::map<std::string,ControlProof::Bit>> ControlProof::variableRenaming(const Ref& from,const Ref& to) {
    auto source=word(from),destination=word(to);if(!source || !destination) return {};
    std::map<std::string,Bit> replacements;
    for(unsigned i=0;i<32;++i) {
        auto bit=(*source)[i],target=(*destination)[i];
        if(bit<2) {if(bit!=target) return {};continue;}
        const auto& item=nodes[bit];if(item.low!=0 || item.high!=1) return {};
        auto [found,inserted]=replacements.emplace(item.atom,target);
        if(!inserted && found->second!=target) return {};
    }
    return replacements;
}
Ref ControlProof::term(Bit value,Terms& terms) const {
    std::map<Bit,Ref> memo;
    std::function<Ref(Bit)> visit=[&](Bit id)->Ref {
        if(id<2) return terms.number(id);
        if(auto found=memo.find(id);found!=memo.end()) return found->second;
        const auto& current=nodes[id];
        auto test=terms.make("Runtime",{atoms.at(current.atom),terms.number(0),terms.number(1)});
        auto result=current.low==0 && current.high==1 ? test :
            terms.make("Runtime",{terms.make("Select",{test,visit(current.high),visit(current.low)}),terms.number(0),terms.number(1)});
        memo.emplace(id,result);return result;
    };return visit(value);
}
}
