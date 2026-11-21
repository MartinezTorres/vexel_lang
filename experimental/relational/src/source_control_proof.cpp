#include "source_control_proof.hpp"
#include "source_algebra.hpp"
#include <algorithm>

namespace vx::relational {
std::size_t ControlProof::Memo::hash(const Key& key) {
    // Proof handles fit 18 bits. Packing first removes serial hash-combine
    // dependencies; full triples still decide equality on every collision.
    std::uint64_t value=std::uint64_t(key[0]) | (std::uint64_t(key[1])<<21) |
        (std::uint64_t(key[2])<<42);
    value^=value>>30;value*=0xbf58476d1ce4e5b9ULL;
    value^=value>>27;value*=0x94d049bb133111ebULL;
    return static_cast<std::size_t>(value^(value>>31));
}
std::optional<ControlProof::Bit> ControlProof::Memo::find(const Key& key) const {
    if(entries.empty()) return {};
    const auto mask=entries.size()-1;auto at=hash(key)&mask;
    for(;;at=(at+1)&mask) {
        const auto& entry=entries[at];
        if(entry.value==empty) return {};
        if(entry.key==key) return entry.value;
    }
}
ControlProof::Bit ControlProof::Memo::insert(const Key& key,Bit value) {
    const auto mask=entries.size()-1;auto at=hash(key)&mask;
    while(entries[at].value!=empty) {
        if(entries[at].key==key) return entries[at].value;
        at=(at+1)&mask;
    }
    entries[at]={key,static_cast<Code>(value)};++count;return value;
}
ControlProof::Bit ControlProof::Memo::emplace(const Key& key,Bit value) {
    if(entries.empty() || (count+1)*4>entries.size()*3) {
        auto previous=std::move(entries);
        // Small proof tables benefit from fewer rehashes. Once capacity
        // exceeds the proof's node budget, retain the ordinary doubling rule
        // so large operation caches do not acquire fourfold unused capacity.
        const auto growth=previous.size()<nodeLimit ? 4u : 2u;
        entries=std::vector<Entry>(previous.empty() ? 16 : previous.size()*growth);count=0;
        for(const auto& entry:previous) if(entry.value!=empty) insert(entry.key,entry.value);
    }
    return insert(key,value);
}
ControlProof::Bit ControlProof::node(std::size_t identity,Bit low,Bit high) {
    if(low==high) return low;
    const Bit complement=low&1;low^=complement;high^=complement;
    Key key{static_cast<Code>(identity),static_cast<Code>(low),static_cast<Code>(high)};
    if(nodes.size()>=nodeLimit) {
        if(auto found=unique.find(key)) return *found^complement;
        throw ConstructionLimit("control proof budget exhausted");
    }
    const Bit next=nodes.size()<<1;
    auto id=unique.emplace(key,next);
    if(id==next) nodes.push_back({identity,low,high});
    return id^complement;
}
ControlProof::Bit ControlProof::atom(Ref source,Ref value,unsigned bit) {
    auto [found,inserted]=atomIds.try_emplace(std::pair{identities.intern(source),bit},atoms.size());
    if(inserted) {
        auto prefix=bit<32 ? "bit:"+std::string(bit<10 ? "0" : "")+std::to_string(bit)+":" : "";
        auto display=show(source,192);bool complete=display.size()<=192;
        display=prefix+display;
        atoms.push_back({std::move(source),std::move(value),std::move(prefix),std::move(display),bit,complete});
    }
    return node(found->second,0,1);
}
bool ControlProof::atomLess(std::size_t first,std::size_t second) const {
    if(first==second) return false;
    const auto& a=atoms[first];const auto& b=atoms[second];
    int order=a.complete && b.complete ? a.display.compare(b.display) :
        compareTermText(a.source,a.prefix,b.source,b.prefix);
    if(order) return order<0;
    // Display text is ordering policy, never identity. Distinct structure can
    // print alike when a constructor name contains punctuation. Break that
    // rare tie structurally, independently of discovery order.
    if(a.bit!=b.bit) return a.bit<b.bit;
    std::vector<std::pair<Ref,Ref>> pending{{a.source,b.source}};
    while(!pending.empty()) {
        auto [left,right]=std::move(pending.back());pending.pop_back();
        if(identical(left,right)) continue;
        if(left->kind!=right->kind) return left->kind<right->kind;
        if(left->kind==Term::Variable) return left->variable<right->variable;
        if(left->kind==Term::Number) return left->number<right->number;
        if(left->name!=right->name) return left->name<right->name;
        if(left->arguments.size()!=right->arguments.size()) return left->arguments.size()<right->arguments.size();
        for(std::size_t i=left->arguments.size();i>0;--i)
            pending.emplace_back(left->arguments[i-1],right->arguments[i-1]);
    }
    return false;
}
ControlProof::Bit ControlProof::select(Bit condition,Bit yes,Bit no) {
    if(condition<2) return condition ? yes : no;
    if(condition&1) return select(condition^1,no,yes);
    if(no&1) return select(condition,yes^1,no^1)^1;
    // Within the selected branch its condition is already known. Normalize
    // these exact Boolean identities before looking up the computed result.
    if(yes==condition) yes=1;
    if(no==condition) no=0;
    if(yes==no) return yes;
    if(yes==1 && no==0) return condition;
    // OR and AND are commutative. Share their operation cache independently
    // of operand order; BDD variable ordering still uses the atom names below.
    if(yes==1 && condition>no) std::swap(condition,no);
    else if(no==0 && condition>yes) std::swap(condition,yes);
    Key key{static_cast<Code>(condition),static_cast<Code>(yes),static_cast<Code>(no)};auto found=cache.find(key);
    if(found) return *found;
    auto identity=nodes[condition>>1].atom;
    // Numeric identities avoid copying atom text into every node/cache key.
    // Ordering still uses the original names, independent of discovery order.
    for(auto id:{yes,no}) if(id>1 && atomLess(nodes[id>>1].atom,identity)) identity=nodes[id>>1].atom;
    auto branch=[&](Bit id,bool high){return id>1 && nodes[id>>1].atom==identity ? ((high ? nodes[id>>1].high : nodes[id>>1].low)^(id&1)) : id;};
    auto low=select(branch(condition,false),branch(yes,false),branch(no,false));
    auto high=select(branch(condition,true),branch(yes,true),branch(no,true));
    auto id=node(identity,low,high);cache.emplace(key,id);return id;
}
ControlProof::Bit ControlProof::parity(Bit a,Bit b) {
    if(a==b) return 0;
    if(!a) return b;
    if(!b) return a;
    if(a==1) return negate(b);
    if(b==1) return negate(a);
    return select(a,negate(b),b);
}
ControlProof::Bit ControlProof::any(const Word& value) {
    Bit result=0;for(auto bit:value) result=disjunction(result,bit);return result;
}
ControlProof::Bit ControlProof::equal(const Word& a,const Word& b) {
    Bit result=1;for(unsigned i=0;i<32;++i) result=conjunction(result,equivalent(a[i],b[i]));return result;
}
ControlProof::Bit ControlProof::less(const Word& a,const Word& b) {
    Bit result=0;
    for(unsigned i=0;i<32;++i) {
        if(a[i]==b[i]) continue;
        if(!a[i]) result=disjunction(result,b[i]);
        else if(!b[i]) result=conjunction(negate(a[i]),result);
        else if(a[i]==1) result=conjunction(b[i],result);
        else if(b[i]==1) result=disjunction(negate(a[i]),result);
        else result=select(equivalent(a[i],b[i]),result,conjunction(negate(a[i]),b[i]));
    }
    return result;
}
ControlProof::Word ControlProof::constant(Integer value) {
    value&=(Integer(1)<<32)-1;Word result{};
    const auto bits=value.convert_to<std::uint32_t>();
    for(unsigned i=0;i<32;++i) result[i]=(bits>>i)&1U;
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
        result[i]=parity(difference,carry);
        // Wrapping arithmetic discards the final carry. Constructing its
        // Boolean relation can be much harder than any demanded result bit.
        if(i+1<width) carry=select(difference,carry,a[i]);
    }
    return result;
}
std::optional<ControlProof::Word> ControlProof::primitive(const Ref& runtime) {
    const auto& args=runtime->arguments;
    if(args.size()!=3 || args[1]->kind!=Term::Number || args[2]->kind!=Term::Number) return {};
    const auto& lo=args[1]->number;const auto& hi=args[2]->number;
    if(lo>hi || lo<-(Integer(1)<<31) || hi>=(Integer(1)<<32) || (lo<0 && hi>=(Integer(1)<<31))) return {};
    const auto& source=args[0];Word result{};
    if(lo==hi) return constant(lo);
    // Every encoding denotes an in-range value. Clamping spare encodings to
    // the lower bound preserves a surjection onto the full declared interval,
    // without global assumptions that could leak across loop quantification.
    unsigned width=1;bool sign=lo<0;
    while(width<32 && (sign ? (lo<-(Integer(1)<<(width-1)) || hi>=(Integer(1)<<(width-1))) : hi>=(Integer(1)<<width))) ++width;
    for(unsigned i=0;i<width;++i) {
        auto bit=generated.make("RepresentationPart",{runtime,generated.number(i),generated.number(1)});
        result[i]=atom(source,bit,i);
    }
    result=wrap(result,width,sign);
    if(lo==(sign ? -(Integer(1)<<(width-1)) : Integer(0)) &&
       hi==(Integer(1)<<(width-(sign ? 1 : 0)))-1) return result;
    auto minimum=constant(lo),maximum=constant(hi),ordered=result;
    if(sign) {ordered[31]=negate(ordered[31]);minimum[31]=negate(minimum[31]);maximum[31]=negate(maximum[31]);}
    auto valid=conjunction(negate(less(ordered,minimum)),negate(less(maximum,ordered)));
    return choose(valid,result,constant(lo));
}
std::optional<ControlProof::Word> ControlProof::word(const Ref& value) {
    auto key=identities.intern(value);if(auto found=words.find(key);found!=words.end()) return found->second;
    auto result=expression(value);
    if(!result && value->name=="Runtime") result=primitive(value);
    words.emplace(std::move(key),result);return result;
}
std::optional<ControlProof::Bit> ControlProof::choiceComparison(const Ref& value) {
    const auto& a=value->arguments;bool sign=value->name=="SignedCompare";
    const std::size_t first=sign ? 2 : 1;
    auto unwrap=[](Ref input) {
        while(input->name=="Runtime" || input->name=="After")
            input=input->arguments.at(input->name=="Runtime" ? 0 : 1);
        return input;
    };
    auto left=unwrap(a.at(first)),right=unwrap(a.at(first+1));
    bool reversed=left->kind==Term::Number;
    auto constant=reversed ? left : right,choice=reversed ? right : left;
    if(constant->kind!=Term::Number || choice->name!="Select") return {};
    unsigned width=sign ? a.at(1)->number.convert_to<unsigned>() : 32;
    if(width<1 || width>32) return {};
    const auto& op=a.at(0)->name;
    if(op!="==" && op!="!=" && op!="<" && op!="<=" && op!=">" && op!=">=") return {};
    auto native=[](const Ref& v){return v->kind==Term::Number && v->number>=-(Integer(1)<<31) && v->number<(Integer(1)<<32);};
    if(!native(constant)) return {};
    // Recognize a bounded DAG of conditional constants before constructing
    // any predicate. An unsupported leaf falls back to ordinary word proof.
    std::set<const Term*> seen;std::vector<Ref> pending{choice};
    while(!pending.empty()) {
        auto node=unwrap(pending.back());pending.pop_back();
        if(!seen.insert(node.get()).second) continue;
        if(seen.size()>256) return {};
        if(native(node)) continue;
        if(node->name!="Select" || node->arguments.size()!=3) return {};
        pending.push_back(node->arguments[1]);pending.push_back(node->arguments[2]);
    }
    auto representation=[&](Integer number) {
        number&=(Integer(1)<<width)-1;
        if(sign && number>=(Integer(1)<<(width-1))) number-=Integer(1)<<width;
        return number;
    };
    const auto fixed=representation(constant->number);
    std::map<const Term*,Bit> memo;
    auto visit=[&](auto&& self,const Ref& input)->Bit {
        auto node=unwrap(input);if(auto found=memo.find(node.get());found!=memo.end()) return found->second;
        Bit result;
        if(node->kind==Term::Number) {
            auto moving=representation(node->number);const auto& l=reversed ? fixed : moving;const auto& r=reversed ? moving : fixed;
            result=(op=="==" ? l==r : op=="!=" ? l!=r : op=="<" ? l<r : op=="<=" ? l<=r : op==">" ? l>r : l>=r) ? 1 : 0;
        } else {
            auto yes=self(self,node->arguments[1]),no=self(self,node->arguments[2]);
            // Equal outcomes need no information about this condition. In
            // particular, a low-bit-only alternative need not enter a proof
            // that observes an independent high-bit threshold.
            result=yes==no ? yes : select(read(node->arguments[0]),yes,no);
        }
        memo.emplace(node.get(),result);return result;
    };
    return visit(visit,choice);
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
        Word result{};result[0]=atom(value,value);return result;
    }
    if(name=="Not") {Word result{};result[0]=negate(read(a[0]));return result;}
    if(name=="And") {Word result{};result[0]=conjunction(read(a[0]),read(a[1]));return result;}
    if(name=="Select") {
        auto yes=word(a[1]),no=word(a[2]);if(!yes || !no) return {};
        return choose(read(a[0]),*yes,*no);
    }
    if(name=="Compare" || name=="SignedCompare") {
        if(auto choice=choiceComparison(value)) {Word result{};result[0]=*choice;return result;}
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
std::optional<Integer> ControlProof::alternativeConstant(const Ref& input) {
    auto value=input;
    while(value->name=="Runtime" || value->name=="After")
        value=value->arguments[value->name=="Runtime" ? 0 : 1];
    std::optional<Integer> result=modularConstant(value,identities);
    if(!result && (value->name=="Compare" || value->name=="SignedCompare")) {
        const auto& a=value->arguments;const std::size_t first=value->name=="SignedCompare" ? 2 : 1;
        if(modularEquivalent(a[first],a[first+1],identities)) {
            const auto& op=a[0]->name;
            if(op=="==" || op=="<=" || op==">=") result=Integer(1);
            else if(op=="!=" || op=="<" || op==">") result=Integer(0);
        }
    }
    if(result) {
        // Words carry 32 representation bits. Only the outer Runtime envelope
        // asks constantValue to interpret the result as signed, just as below.
        *result&=(Integer(1)<<32)-1;
        if(input->name=="Runtime" && input->arguments[1]->kind==Term::Number &&
           input->arguments[1]->number<0 && *result>=(Integer(1)<<31)) *result-=Integer(1)<<32;
    }
    return result;
}
ControlProof::Bit ControlProof::read(const Ref& value) {
    if(value->kind==Term::Number) return value->number!=0 ? 1 : 0;
    auto key=identities.intern(value);if(auto found=booleans.find(key);found!=booleans.end()) return found->second;
    Bit result;
    try {
        if(auto bits=word(value)) result=any(*bits);
        else if(value->name=="Runtime") result=read(value->arguments[0]);
        else if(value->name=="After") result=read(value->arguments[1]);
        else if(value->name=="Select") result=select(read(value->arguments[0]),read(value->arguments[1]),read(value->arguments[2]));
        else result=atom(value,value);
    } catch(const ConstructionLimit&) {
        // Try one bounded alternative at an observation boundary, rather than
        // expanding polynomials at every node during ordinary bit construction.
        auto alternative=alternativeConstant(value);if(!alternative) throw;
        result=*alternative!=0 ? 1 : 0;
    }
    booleans.emplace(std::move(key),result);return result;
}
std::optional<Integer> ControlProof::constantValue(const Ref& value) {
    try {
        auto bits=word(value);if(!bits) return {};
        Integer result=0;
        for(unsigned i=0;i<32;++i) {if((*bits)[i]>1) return {};if((*bits)[i]) result|=Integer(1)<<i;}
        if(value->name=="Runtime" && value->arguments[1]->kind==Term::Number && value->arguments[1]->number<0 && (*bits)[31]) result-=Integer(1)<<32;
        return result;
    } catch(const ConstructionLimit&) {
        auto alternative=alternativeConstant(value);if(alternative) return alternative;
        throw;
    }
}
ControlProof::Bit ControlProof::exists(Bit value,const std::function<bool(const Ref&)>& remove) {
    if(value<2) return value;
    const Bit absent=~Bit(0);
    // Children predate their parent. Quantification can create new nodes, but
    // visits only the original graph: its root bounds this local memo exactly.
    std::vector<Bit> memo(value+1,absent);
    std::vector<signed char> removed(atoms.size(),-1);
    auto visit=[&](auto&& self,Bit id)->Bit {
        if(id<2) return id;
        if(memo[id]!=absent) return memo[id];
        const auto current=branch(id);auto low=self(self,current.low),high=self(self,current.high);
        // Classification is a predicate on an atom, independent of the BDD
        // node that uses it. Do not traverse its provenance for every use.
        auto& erase=removed[current.atom];
        if(erase<0) erase=remove(atoms.at(current.atom).value) ? 1 : 0;
        return memo[id]=erase ? disjunction(low,high) : node(current.atom,low,high);
    };return visit(visit,value);
}
ControlProof::Bit ControlProof::substitute(Bit value,const Renaming& replacements) {
    if(value<2) return value;
    const Bit absent=~Bit(0);
    std::vector<Bit> memo(value+1,absent),conditions(atoms.size(),absent);
    auto visit=[&](auto&& self,Bit id)->Bit {
        if(id<2) return id;
        if(memo[id]!=absent) return memo[id];
        const auto current=branch(id);auto& condition=conditions[current.atom];
        if(condition==absent) {
            auto original=node(current.atom,0,1);auto found=replacements.find(original);
            condition=found==replacements.end() ? original : found->second;
        }
        return memo[id]=select(condition,self(self,current.high),self(self,current.low));
    };return visit(visit,value);
}
std::optional<ControlProof::Renaming> ControlProof::variableRenaming(const Ref& from,const Ref& to) {
    auto source=word(from),destination=word(to);if(!source || !destination) return {};
    Renaming replacements;
    for(unsigned i=0;i<32;++i) {
        auto bit=(*source)[i],target=(*destination)[i];
        if(bit<2) {if(bit!=target) return {};continue;}
        const auto item=branch(bit);if(item.low!=0 || item.high!=1) return {};
        auto [found,inserted]=replacements.emplace(bit,target);
        if(!inserted && found->second!=target) return {};
    }
    return replacements;
}
Ref ControlProof::term(Bit value,Terms& terms) const {
    if(value<2) return terms.number(value);
    std::vector<Ref> memo(value+1);
    auto visit=[&](auto&& self,Bit id)->Ref {
        if(id<2) return terms.number(id);
        if(memo[id]) return memo[id];
        const auto current=branch(id);
        auto test=terms.make("Runtime",{atoms.at(current.atom).value,terms.number(0),terms.number(1)});
        auto result=current.low==0 && current.high==1 ? test :
            terms.make("Runtime",{terms.make("Select",{test,self(self,current.high),self(self,current.low)}),terms.number(0),terms.number(1)});
        return memo[id]=result;
    };return visit(visit,value);
}
}
