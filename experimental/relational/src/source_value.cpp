#include "source_value.hpp"
#include "source_numeric.hpp"
#include <algorithm>
#include <sstream>
#include <tuple>

namespace vx::relational {
namespace {
struct Unavailable { std::string reason; };
struct Format {
    unsigned bits=8;
    bool sign=false;
    bool operator==(const Format&) const = default;
    std::string c() const {return std::string(sign ? "int" : "uint")+std::to_string(bits)+"_t";}
};
Format format(const Ref& type) {
    if(type->name=="Bool") return {};
    if(type->name!="UInt" && type->name!="SInt") throw Unavailable{"non-integer interface"};
    auto bits=scalarBits(type);
    if(bits!=8 && bits!=16 && bits!=32) throw Unavailable{"non-native integer width"};
    return {bits,scalarSigned(type)};
}
Format rangeFormat(const Integer& lo,const Integer& hi) {
    for(unsigned bits:{8u,16u,32u}) {
        bool sign=lo<0;
        if(lo>=(sign ? -(Integer(1)<<(bits-1)) : Integer(0)) && hi<(Integer(1)<<(bits-(sign ? 1 : 0))))
            return {bits,sign};
    }
    throw Unavailable{"integer value exceeds native representation"};
}
std::string cast(Format f,const std::string& x) {
    return f.sign ? "VX_VALUE_I"+std::to_string(f.bits)+"("+x+")" : "(("+f.c()+")("+x+"))";
}
struct Node {
    std::string op, data;
    Format type;
    Integer lo,hi;
    std::vector<std::size_t> args;
    bool materialized=false;
};
struct Graph {
    std::vector<Node> nodes;
    std::map<std::string,std::size_t> interned,inputs;
    std::map<const Term*,std::size_t> seen;
    std::set<const Term*> checked;
    bool region=false,calculation=false;
    unsigned linearTermLimit=16;
    std::size_t linearCombinations=0,narrowedOperations=0,sharedDivisions=0;
    bool combiningLinear=false;
    std::function<std::optional<CalculationInput>(const Ref&)> input={};
    std::map<std::string,std::size_t> runtime;
    static std::string key(const Node& n) {
        std::string result=n.op+":"+n.data+":"+n.type.c()+":"+(n.materialized ? "stored" : "value");
        for(auto a:n.args) result+=":"+std::to_string(a);
        return result;
    }
    void reindex() {
        interned.clear();
        for(std::size_t id=0;id<nodes.size();++id) interned.emplace(key(nodes[id]),id);
    }
    std::size_t add(Node n) {
        refine(n);
        // Every admitted operation is total at this evaluation site. An exact
        // interval can therefore publish its value without retaining operand
        // reads; producer calls and other effects remain in their own trace.
        if(n.op!="input" && n.op!="constant" && n.lo==n.hi)
            n={"constant",n.lo.str(),n.type,n.lo,n.lo,{}};
        if(!combiningLinear) if(auto combined=combineLinear(n)) return *combined;
        const auto identity=key(n);
        if(auto it=interned.find(identity);it!=interned.end()) return it->second;
        if(nodes.size()==(region ? 8192u : 512u)) throw Unavailable{"value graph exceeds inline policy budget"};
        auto id=nodes.size();interned.emplace(identity,id);nodes.push_back(std::move(n));return id;
    }
    std::optional<std::size_t> combineLinear(const Node& root) {
        if(!linearTermLimit || (root.op!="Arithmetic" && root.op!="SignedArithmetic") ||
           (root.data!="+" && root.data!="-")) return {};
        const Integer modulus=Integer(1)<<root.type.bits;
        std::map<std::size_t,Integer> coefficients;
        std::vector<std::optional<std::size_t>> order;
        Integer constant=0;unsigned constants=0,visits=0;bool duplicate=false;
        auto collect=[&](auto&& self,std::size_t id,Integer coefficient)->bool {
            if(++visits>linearTermLimit*8u || order.size()>linearTermLimit) return false;
            coefficient%=modulus;
            if(coefficient==0) return true;
            const auto& n=nodes[id];
            if(n.lo==n.hi) {
                constant+=coefficient*n.lo;if(!constants++) order.push_back({});
                return true;
            }
            // Arithmetic at this exact modular width can be reassociated.
            // Widening/narrowing conversions remain opaque: an intervening
            // wrap is not an identity in a different integer ring.
            if(n.op=="cast" && n.type.bits==root.type.bits && nodes[n.args[0]].type.bits==root.type.bits)
                return self(self,n.args[0],std::move(coefficient));
            if((n.op=="Arithmetic" || n.op=="SignedArithmetic") && n.type.bits==root.type.bits &&
               (n.data=="+" || n.data=="-"))
                return self(self,n.args[0],coefficient) && self(self,n.args[1],n.data=="-" ? Integer(-coefficient) : coefficient);
            // Earlier collection may already have formed 2*x. Re-enter its
            // constant coefficient so a later +x can become 3*x rather than
            // treating the first normalization as an opaque new atom.
            if((n.op=="Arithmetic" || n.op=="SignedArithmetic") && n.type.bits==root.type.bits && n.data=="*")
                for(unsigned side=0;side<2;++side) {
                    const auto& factor=nodes[n.args[side]];
                    if(factor.lo==factor.hi)
                        return self(self,n.args[1-side],Integer(coefficient*factor.lo));
                }
            auto [it,inserted]=coefficients.try_emplace(id,0);
            if(inserted) order.push_back(id);else duplicate=true;
            it->second+=coefficient;return true;
        };
        if(!collect(collect,root.args[0],1) || !collect(collect,root.args[1],root.data=="-" ? -1 : 1) ||
           order.size()>linearTermLimit || (!duplicate && constants<2)) return {};
        // Optional normalization must not consume the projection's remaining
        // graph budget. New nodes need at most cast/constant/product/sum per
        // atom. Existing interned nodes can only reduce that upper bound.
        if(nodes.size()+4*order.size()+1>=(region ? 8192u : 512u)) return {};
        struct Guard {bool& flag;explicit Guard(bool& f):flag(f){flag=true;}~Guard(){flag=false;}} guard(combiningLinear);
        auto residue=[&](Integer value) {value%=modulus;if(value<0) value+=modulus;return value;};
        auto literal=[&](Integer value) {
            value=residue(std::move(value));
            if(root.type.sign && value>=(modulus>>1)) value-=modulus;
            return add({"constant",value.str(),root.type,value,value,{}});
        };
        std::optional<std::size_t> result;
        for(const auto& term:order) {
            Integer coefficient=residue(term ? coefficients.at(*term) : constant);
            if(coefficient==0) continue;
            auto value=term ? convert(*term,root.type) : literal(coefficient);
            if(term && coefficient!=1)
                value=add({root.op,"*",root.type,0,0,{value,literal(coefficient)}});
            if(result) result=add({root.op,"+",root.type,0,0,{*result,value}});
            else result=value;
        }
        ++linearCombinations;
        return result ? *result : literal(0);
    }
    void refine(Node& n) const {
        auto range=[&](std::size_t i) {const auto& a=nodes.at(n.args.at(i));return IntegerRange{a.lo,a.hi};};
        IntegerBound result;
        if(n.op=="cast") result=convertedIntegerRange(range(0),n.type.bits,n.type.sign);
        else if(n.op=="Arithmetic" || n.op=="SignedArithmetic") {
            result=arithmeticIntegerRange(n.data,range(0),range(1),n.type.bits,n.type.sign);
        } else if(n.op=="Select") {
            const auto a=convertedIntegerRange(range(1),n.type.bits,n.type.sign).value();
            const auto b=convertedIntegerRange(range(2),n.type.bits,n.type.sign).value();
            result=IntegerRange{std::min(a.lower,b.lower),std::max(a.upper,b.upper)};
        } else if(n.op=="Compare" || n.op=="SignedCompare") {
            auto a=range(0),b=range(1);
            if(n.op=="Compare") {
                a=convertedIntegerRange(a,32,false).value();b=convertedIntegerRange(b,32,false).value();
            }
            unsigned possible=(a.lower<b.upper ? 1u : 0u) |
                (a.lower<=b.upper && b.lower<=a.upper ? 2u : 0u) | (a.upper>b.lower ? 4u : 0u);
            if(n.args[0]==n.args[1]) possible=2;
            const unsigned wanted=n.data=="<" ? 1 : n.data=="==" ? 2 : n.data==">" ? 4 :
                n.data=="<=" ? 3 : n.data==">=" ? 6 : n.data=="!=" ? 5 : 7;
            if(!(possible&wanted)) result=IntegerRange{0,0};
            else if((possible&wanted)==possible) result=IntegerRange{1,1};
        }
        if(result) {n.lo=result->lower;n.hi=result->upper;}
    }
    void trace(const Ref& t) {
        if(!checked.insert(t.get()).second) return;
        if(t->name=="Done") return;
        if(t->name=="Sequence") {for(const auto& part:t->arguments) trace(part);return;}
        if(t->name=="If") {value(t->arguments[0]);trace(t->arguments[1]);trace(t->arguments[2]);return;}
        if(t->name=="Evaluate") {value(t->arguments[0]);return;}
        throw Unavailable{"execution requires "+t->name};
    }
    std::size_t value(const Ref& v) {
        if(auto it=seen.find(v.get());it!=seen.end()) return it->second;
        auto id=build(v);seen.emplace(v.get(),id);return id;
    }
    std::size_t build(const Ref& v) {
        if(v->kind==Term::Number) return add({"constant",v->number.str(),rangeFormat(v->number,v->number),v->number,v->number,{}});
        const auto& a=v->arguments;const auto& op=v->name;
        if(op=="Runtime") return value(a.at(0));
        if(calculation) {
            // Conditional arms and potentially trapping/effectful values are
            // separate intervals. The ordinary renderer visits them in place.
            if(op=="Select" || op=="And" || op=="After") throw Unavailable{"calculation boundary"};
            if(auto leaf=input(v)) {
                Node n{"input",leaf->expression,format(leaf->type),0,0,{},leaf->materialized};
                bounds(n);
                if(leaf->range) {
                    const auto range=convertedIntegerRange(leaf->range,n.type.bits,n.type.sign).value();
                    n.lo=range.lower;n.hi=range.upper;
                }
                return add(std::move(n));
            }
        }
        if(op=="After") {if(!region) trace(a.at(0));return value(a.at(1));}
        if(region && (op=="LoopSlotValue" || op=="LoopReady" || op=="LoopIndex" || op=="CallValue")) {
            auto key=op+":"+show(a.at(0));
            if(op!="LoopIndex") key+=":"+show(a.at(1));
            if(op=="LoopSlotValue") key+=":"+show(a.at(2));
            auto it=runtime.find(key);
            if(it==runtime.end()) throw Unavailable{"undeclared region state: "+key};
            return it->second;
        }
        if(op=="Input") {
            auto it=inputs.find(a.at(0)->name);
            if(it==inputs.end()) throw Unavailable{"unknown input"};
            return it->second;
        }
        Node n;n.op=op;
        if(op=="Select") {
            for(const auto& item:a) n.args.push_back(value(item));
            const auto& yes=nodes[n.args[1]];const auto& no=nodes[n.args[2]];
            n.lo=std::min(yes.lo,no.lo);n.hi=std::max(yes.hi,no.hi);n.type=rangeFormat(n.lo,n.hi);
            if(n.args[1]==n.args[2]) return n.args[1]; // condition is certified total
            return add(std::move(n));
        }
        if(op=="Not" || op=="And") {
            for(const auto& item:a) n.args.push_back(value(item));
            n.lo=0;n.hi=1;return add(std::move(n));
        }
        if(op=="Compare" || op=="SignedCompare") {
            n.data=a[0]->name;auto first=op=="Compare" ? 1u : 2u;
            n.args={value(a[first]),value(a[first+1])};n.lo=0;n.hi=1;
            // SignedCompare explicitly interprets operands at its own width.
            if(op=="SignedCompare") {
                auto bits=a[1]->number.convert_to<unsigned>();
                if(bits!=8 && bits!=16 && bits!=32) throw Unavailable{"non-native signed comparison"};
                for(auto& id:n.args) id=convert(id,{bits,true});
            }
            return add(std::move(n));
        }
        if(op=="Cast" || op=="SignedCast") {
            auto bits=a[0]->number.convert_to<unsigned>();
            if(bits!=8 && bits!=16 && bits!=32) throw Unavailable{"non-native integer cast"};
            return convert(value(a[1]),{bits,op=="SignedCast"});
        }
        if(op=="Arithmetic" || op=="SignedArithmetic") {
            n.data=a[0]->name;n.type={a[1]->number.convert_to<unsigned>(),op=="SignedArithmetic"};
            if(n.type.bits!=8 && n.type.bits!=16 && n.type.bits!=32) throw Unavailable{"non-native arithmetic width"};
            n.args={value(a[2]),value(a[3])};
            if(n.data=="<<" || n.data==">>") {
                const auto& count=nodes[n.args[1]];
                if(count.lo<0 || count.hi>=n.type.bits)
                    throw Unavailable{"shift count needs a runtime check"};
                n.args[1]=convert(n.args[1],{8,false});
            } else if(n.data=="/" || n.data=="%") {
                n.args[1]=convert(n.args[1],n.type);
                const auto& divisor=nodes[n.args[1]];
                if(divisor.lo<=0 && divisor.hi>=0)
                    throw Unavailable{"division needs a proved nonzero divisor at its arithmetic width"};
            } else if(n.data!="+" && n.data!="-" && n.data!="*" && n.data!="&" && n.data!="|" && n.data!="^")
                throw Unavailable{"unsupported integer operator"};
            bounds(n);return add(std::move(n));
        }
        throw Unavailable{"value requires "+op};
    }
    static void bounds(Node& n) {
        n.lo=n.type.sign ? -(Integer(1)<<(n.type.bits-1)) : Integer(0);
        n.hi=(Integer(1)<<(n.type.bits-(n.type.sign ? 1 : 0)))-1;
    }
    std::size_t convert(std::size_t id,Format f) {
        if(nodes[id].type==f) return id;
        Node n{"cast","",f,0,0,{id}};bounds(n);return add(std::move(n));
    }
    std::vector<std::size_t> shareDivision(const std::vector<std::size_t>& roots) {
        if(region) return roots;
        std::set<std::size_t> live;
        auto mark=[&](auto&& self,std::size_t id)->void {
            if(!live.insert(id).second) return;
            for(auto child:nodes[id].args) self(self,child);
        };
        for(auto root:roots) mark(mark,root);
        std::map<std::string,std::size_t> quotients;
        for(auto id:live) {
            const auto& n=nodes[id];
            if((n.op=="Arithmetic" || n.op=="SignedArithmetic") && n.data=="/") quotients.emplace(key(n),id);
        }
        std::map<std::size_t,std::size_t> pairs;
        for(auto id:live) {
            auto n=nodes[id];
            if((n.op!="Arithmetic" && n.op!="SignedArithmetic") || n.data!="%") continue;
            const auto& divisor=nodes[n.args[1]];
            if(divisor.lo==divisor.hi) {
                Integer magnitude=divisor.lo<0 ? -divisor.lo : divisor.lo;
                if((magnitude&(magnitude-1))==0) continue; // A mask/shift is already cheap.
            }
            n.data="/";
            if(auto q=quotients.find(key(n));q!=quotients.end()) pairs.emplace(id,q->second);
        }
        if(pairs.empty()) return roots;
        Graph before=*this;
        try {
            std::map<std::size_t,std::size_t> rewritten;
            auto visit=[&](auto&& self,std::size_t id)->std::size_t {
                if(auto found=rewritten.find(id);found!=rewritten.end()) return found->second;
                auto n=nodes[id];bool changed=false;
                for(auto& child:n.args) {auto next=self(self,child);changed|=next!=child;child=next;}
                auto result=id;
                if(auto pair=pairs.find(id);pair!=pairs.end()) {
                    // Truncating division satisfies r=x-q*d, including signed
                    // minimum/-1 after wrapping. Both values are certified
                    // total and use exactly the same arithmetic representation.
                    auto quotient=self(self,pair->second);
                    auto product=add({n.op,"*",n.type,0,0,{quotient,n.args[1]}});
                    result=add({n.op,"-",n.type,0,0,{n.args[0],product}});
                    // Keep the established remainder envelope. Independent
                    // interval multiplication loses the relation between q
                    // and x, although this exact identity preserves it.
                    auto& replacement=nodes[result];
                    const Integer lower=std::max(n.lo,replacement.lo),upper=std::min(n.hi,replacement.hi);
                    if(lower<=upper) {replacement.lo=lower;replacement.hi=upper;}
                    ++sharedDivisions;
                } else if(changed) result=add(std::move(n));
                rewritten.emplace(id,result);return result;
            };
            std::vector<std::size_t> result;
            for(auto root:roots) result.push_back(visit(visit,root));
            return result;
        } catch(const Unavailable&) {*this=std::move(before);return roots;}
    }
    void lowerWidths(const std::vector<std::size_t>& roots,const std::vector<Format>& outputs) {
        if(region) return;
        // Complete-value proofs can narrow arithmetic even when every result
        // bit is observed. Both operands and the complete result must fit in
        // the smaller representation with the original signedness. This is
        // separate from the residue-demand reduction below.
        for(auto& n:nodes) {
            if(n.op!="Arithmetic" && n.op!="SignedArithmetic") continue;
            auto bound=[&](std::size_t i) {
                const auto& child=nodes[n.args[i]];
                return convertedIntegerRange(IntegerRange{child.lo,child.hi},n.type.bits,n.type.sign).value();
            };
            const auto left=bound(0);
            const bool shift=n.data=="<<" || n.data==">>";
            const auto right=shift ? IntegerRange{nodes[n.args[1]].lo,nodes[n.args[1]].hi} : bound(1);
            for(unsigned bits:{8u,16u}) {
                if(bits>=n.type.bits) break;
                const auto domain=integerDomain(bits,n.type.sign);
                auto fits=[&](const IntegerRange& v){return v.lower>=domain.lower && v.upper<=domain.upper;};
                if(!fits(left) || !fits({n.lo,n.hi}) || (shift ? right.lower<0 || right.upper>=bits : !fits(right))) continue;
                n.type.bits=bits;++narrowedOperations;break;
            }
            refine(n);
        }
        // Low result bits of modular +,-,*, bitwise operations and left
        // shifts depend only on the corresponding low operand bits. Join
        // every consumer's demand before choosing any narrower realization:
        // a shared value still observed at full width must remain full width.
        auto modular=[](const Node& n) {
            return (n.op=="Arithmetic" || n.op=="SignedArithmetic") &&
                (n.data=="+" || n.data=="-" || n.data=="*" || n.data=="&" ||
                 n.data=="|" || n.data=="^" || n.data=="<<");
        };
        std::vector<unsigned> demand(nodes.size());
        std::vector<std::pair<std::size_t,unsigned>> pending;
        for(std::size_t i=0;i<roots.size();++i) pending.emplace_back(roots[i],outputs[i].bits);
        while(!pending.empty()) {
            auto [id,bits]=pending.back();pending.pop_back();
            const auto& n=nodes[id];bits=std::min(bits,n.type.bits);
            if(demand[id]>=bits) continue;
            demand[id]=bits;
            for(std::size_t i=0;i<n.args.size();++i) {
                auto child=n.args[i];auto required=nodes[child].type.bits;
                if(n.op=="cast" || (n.op=="Select" && i>0) ||
                   (modular(n) && !(n.data=="<<" && i==1))) required=std::min(required,bits);
                pending.emplace_back(child,required);
            }
        }
        for(std::size_t id=0;id<nodes.size();++id) {
            auto& n=nodes[id];
            if(demand[id] && demand[id]<n.type.bits &&
               (n.op=="cast" || n.op=="Select" || modular(n))) {
                ++narrowedOperations;
                n.type.bits=demand[id];
                // The original shift count was proved valid at its original
                // width. A count past the retained low bits produces zero;
                // it must not become an invalid narrower C shift.
                if(n.data=="<<" && nodes[n.args[1]].lo>=n.type.bits)
                    n={"constant","0",n.type,0,0,{}};
            }
            refine(n);
        }
        reindex();
    }
};
struct SparseProduct {
    unsigned left,right;
    bool subtract,negate;
};
std::optional<SparseProduct> sparseProduct(unsigned bits,unsigned factor) {
    std::optional<SparseProduct> best;
    unsigned bestCost=~0u;
    const auto mask=bits==32 ? ~0u : (1u<<bits)-1;
    for(unsigned left=0;left<bits;++left) for(unsigned right=0;right<bits;++right)
        for(bool subtract:{false,true}) for(bool negate:{false,true}) {
            auto value=subtract ? (1u<<left)-(1u<<right) : (1u<<left)+(1u<<right);
            if(negate) value=0u-value;
            if((value&mask)!=factor) continue;
            auto cost=std::max(left,right)*2+std::min(left,right)+(negate ? 3u : 0u);
            if(cost<bestCost) {best={left,right,subtract,negate};bestCost=cost;}
        }
    return best;
}
#include "source_value_tables.inc"
#include "source_value_choices.inc"
struct CaseGuard {std::size_t condition;bool simple;};
std::optional<CaseGuard> caseGuard(const Graph& graph,const std::vector<std::size_t>& roots) {
    std::optional<CaseGuard> result;bool simple=true;
    for(auto id:roots) {
        const auto& node=graph.nodes[id];
        if(node.op=="constant") continue;
        if(node.op!="Select") {simple=false;continue;}
        // The first selected result supplies a deterministic partition. Other
        // result guards may remain independent: caseValues specializes their
        // full expressions and retains every still-needed input. Existing
        // common-guard interfaces keep exactly their established partition.
        if(result) simple&=result->condition==node.args[0];
        else result=CaseGuard{node.args[0],true};
    }
    if(result) result->simple=simple;
    return result;
}
std::vector<std::size_t> caseValues(Graph& graph,const std::vector<std::size_t>& roots,CaseGuard guard,bool yes) {
    auto result=roots;
    if(guard.simple) {
        // Preserve the established interface and realization for the common
        // case where every nonconstant output directly selects this guard.
        for(auto& id:result) if(graph.nodes[id].op=="Select") id=graph.nodes[id].args[yes ? 1 : 2];
        return result;
    }
    const std::map<std::size_t,std::string> locals;
    FiniteTables finite{graph,{},{},0};
    // A chosen Boolean result can establish several facts. For example,
    // !(a || b) establishes !a and !b. Keep the proof separate from physical
    // expression normalization, which might discard useful shared branches.
    std::vector<std::pair<std::size_t,bool>> premises{{guard.condition,yes}};
    std::map<std::size_t,bool> known;
    auto truth=[&](std::size_t id)->std::optional<bool> {
        if(auto it=known.find(id);it!=known.end()) return it->second;
        const auto& node=graph.nodes[id];
        if(node.lo==0 && node.hi==0) return false;
        if(node.lo>0 || node.hi<0) return true;
        return {};
    };
    for(std::size_t i=0;i<premises.size();++i) {
        const auto [id,value]=premises[i];
        if(!known.emplace(id,value).second) continue;
        const auto& node=graph.nodes[id];
        if(node.op=="Not") premises.emplace_back(node.args[0],!value);
        else if(node.op=="And") {
            if(value) {premises.emplace_back(node.args[0],true);premises.emplace_back(node.args[1],true);}
            else {
                if(truth(node.args[0])==true) premises.emplace_back(node.args[1],false);
                if(truth(node.args[1])==true) premises.emplace_back(node.args[0],false);
            }
        } else if(node.op=="Select") {
            const auto a=truth(node.args[1]),b=truth(node.args[2]);
            if(a && *a!=value && (!b || *b==value)) {
                premises.emplace_back(node.args[0],false);premises.emplace_back(node.args[2],value);
            } else if(b && *b!=value && (!a || *a==value)) {
                premises.emplace_back(node.args[0],true);premises.emplace_back(node.args[1],value);
            }
        }
    }
    // Apply the established outer condition first, then its local consequences.
    // Each pass retains conversions and computes dependencies after rewriting.
    std::set<std::size_t> applied;
    for(const auto& [condition,value]:premises) if(applied.insert(condition).second) {
        ValueChoice choice{graph,{},condition,value,locals,finite,{}};
        for(auto& id:result) id=choice.value(id);
    }
    return result;
}
std::string sparseProductHelper(const std::string& name) {
    const auto split=name.find('_');
    auto bits=static_cast<unsigned>(std::stoul(name.substr(5,split-5)));
    auto factor=static_cast<unsigned>(std::stoul(name.substr(split+1)));
    const auto plan=sparseProduct(bits,factor);
    if(!plan) throw std::logic_error("invalid sparse product demand");
    auto shifted=[](unsigned shift) {
        return shift ? "((uint32_t)x << "+std::to_string(shift)+")" : "(uint32_t)x";
    };
    auto type="uint"+std::to_string(bits)+"_t";
    auto result="#ifndef VX_SUPPORT_"+name+"\n#define VX_SUPPORT_"+name+"\n";
    result+="#ifdef __SDCC\nstatic "+type+" vx_value_"+name+"("+type+" x) __sdcccall(1) VX_VALUE_LOCATION {\n";
    auto expression="("+shifted(plan->left)+(plan->subtract ? " - " : " + ")+shifted(plan->right)+")";
    if(plan->negate) expression="(0UL - "+expression+")";
    result+="    return ("+type+")"+expression+";\n}\n";
    result+="#else\nstatic inline "+type+" vx_value_"+name+"("+type+" x) {\n";
    result+="    return ("+type+")((uint32_t)x * "+std::to_string(factor)+"U);\n}\n#endif\n#endif\n";
    return result;
}
std::string constantDivisionHelper(const std::string& name) {
    const bool remainder=name.starts_with("rem"),sign=name.at(3)=='s';
    const auto split=name.find('_');
    const auto bits=static_cast<unsigned>(std::stoul(name.substr(4,split-4)));
    const bool negative=name.at(split+1)=='n';
    const Integer divisor(name.substr(split+2));
    if((bits!=8 && bits!=16 && bits!=32) || divisor<=0 || (!sign && negative))
        throw std::logic_error("invalid constant division demand");
    const auto type=Format{bits,sign}.c(),utype=Format{bits,false}.c();
    const Integer maximum=sign ? Integer(1)<<(bits-1) : (Integer(1)<<bits)-1;
    std::string result="#ifndef VX_SUPPORT_"+name+"\n#define VX_SUPPORT_"+name+"\n";
    result+="#ifdef __SDCC_z80\nstatic "+type+" vx_value_"+name+"("+type+" x) __sdcccall(1) VX_VALUE_LOCATION\n";
    result+="#else\nstatic inline "+type+" vx_value_"+name+"("+type+" x)\n#endif\n{\n";
    if(sign) result+="    uint8_t negative=(x<0);\n";
    result+="    "+utype+" rest=("+utype+")x;\n";
    if(!remainder) result+="    "+utype+" quotient=0;\n";
    if(sign) result+="    if(negative) rest=("+utype+")(0UL-rest);\n";
    // Binary long division specialized to the known divisor. Every threshold
    // fits the unsigned carrier; no overflowing trial shift or implicit SDCC
    // library division is needed. The input is evaluated once by this leaf.
    for(unsigned shift=bits;shift-->0;) {
        const Integer threshold=divisor<<shift;
        if(threshold>maximum) continue;
        result+="    if(rest>="+threshold.str()+"UL) {rest=("+utype+")(rest-"+threshold.str()+"UL);";
        if(!remainder) result+="quotient|="+Integer(Integer(1)<<shift).str()+"UL;";
        result+="}\n";
    }
    const std::string answer=remainder ? "rest" : "quotient";
    std::string expression=answer;
    if(sign) expression="(("+std::string(negative && !remainder ? "!negative" : "negative")+") ? 0UL-"+answer+" : "+answer+")";
    result+="    return "+cast({bits,sign},expression)+";\n}\n#endif\n";
    return result;
}
std::string variableDivisionHelper(const std::string& name) {
    const bool remainder=name.starts_with("rem"),sign=name.at(3)=='s';
    const auto bits=static_cast<unsigned>(std::stoul(name.substr(4,name.find('_')-4)));
    if(bits!=8 && bits!=16 && bits!=32) throw std::logic_error("invalid variable division demand");
    const auto type=Format{bits,sign}.c(),utype=Format{bits,false}.c();
    std::string result="#ifndef VX_SUPPORT_"+name+"\n#define VX_SUPPORT_"+name+"\n";
    result+="#ifdef __SDCC_z80\nstatic "+type+" vx_value_"+name+"("+type+" x, "+type+" y) __sdcccall(1) VX_VALUE_LOCATION\n";
    result+="#else\nstatic inline "+type+" vx_value_"+name+"("+type+" x, "+type+" y)\n#endif\n{\n";
    if(sign) {
        result+="    uint8_t negative_x=(x<0);\n";
        if(!remainder) result+="    uint8_t negative_y=(y<0);\n";
    }
    result+="    "+utype+" a=("+utype+")x, b=("+utype+")y, rest=0;\n";
    if(!remainder) result+="    "+utype+" quotient=0;\n";
    result+="    uint8_t i;\n";
    if(sign) {
        result+="    if(negative_x) a=("+utype+")(0UL-a);\n";
        result+="    if(y<0) b=("+utype+")(0UL-b);\n";
    }
    // The graph certifies a nonzero divisor before selecting this leaf. Use
    // exactly the physical width, retaining the carry of the partial remainder
    // instead of needing a wider intermediate or an implicit target library.
    result+="    for(i=0;i<"+std::to_string(bits)+";++i) {\n";
    result+="        uint8_t carry=(rest >> "+std::to_string(bits-1)+")!=0;\n";
    result+="        rest=("+utype+")((rest << 1) | (a >> "+std::to_string(bits-1)+"));\n";
    result+="        a=("+utype+")(a << 1);\n";
    if(!remainder) result+="        quotient=("+utype+")(quotient << 1);\n";
    result+="        if(carry || rest>=b) {rest=("+utype+")(rest-b);";
    if(!remainder) result+="quotient|=1;";
    result+="}\n    }\n";
    const std::string answer=remainder ? "rest" : "quotient";
    auto expression=answer;
    if(sign) expression="("+std::string(remainder ? "negative_x" : "negative_x!=negative_y")+" ? 0UL-"+answer+" : "+answer+")";
    result+="    return "+cast({bits,sign},expression)+";\n}\n#endif\n";
    return result;
}
std::string runtimeIncludes(const std::set<std::string>& helpers) {
    std::string result="#include \"vx_support_value_base.h\"\n";
    for(const auto& helper:helpers) {
        if(helper.starts_with("ror8_")) {
            const auto count=static_cast<unsigned>(std::stoul(helper.substr(5)));
            if(!count || count>=8) throw std::logic_error("invalid byte rotation demand");
            result+="#ifndef VX_SUPPORT_"+helper+"\n#define VX_SUPPORT_"+helper+"\n#ifdef __SDCC_z80\n"
                "static inline uint8_t vx_value_"+helper+"(uint8_t x) __sdcccall(1) VX_VALUE_LOCATION\n#else\n"
                "static inline uint8_t vx_value_"+helper+"(uint8_t x)\n#endif\n{\n";
            // SDCC recognizes one-bit byte rotations. Composing them avoids
            // the shifts, masks and spills used for the equivalent wider pair.
            for(unsigned i=0;i<std::min(count,8-count);++i)
                result+=count<=4 ? "    x=(uint8_t)((x >> 1) | (x << 7));\n" :
                                  "    x=(uint8_t)((x << 1) | (x >> 7));\n";
            result+="    return x;\n}\n#endif\n";
        } else result+=helper.starts_with("scale") ? sparseProductHelper(helper) :
                helper.ends_with("_runtime") ? variableDivisionHelper(helper) :
                helper.starts_with("div") || helper.starts_with("rem") ? constantDivisionHelper(helper) :
                "#include \"vx_support_value_"+helper+".h\"\n";
    }
    return result;
}
std::string selectRuntime(std::string header,const std::set<std::string>& helpers) {
    const std::string marker="#include \"vx_value_runtime.h\"\n";
    auto at=header.find(marker);
    if(at==std::string::npos) throw std::logic_error("value projection lacks runtime include");
    header.replace(at,marker.size(),runtimeIncludes(helpers));return header;
}
struct Emitter {
    Graph& g;
    std::ostream& out;
    std::size_t serial=0,shared=0,bytes=0,blocks=0;
    bool reuseScratch=true;
    bool scheduleOutputs=true;
    bool shareDivisions=false;
    bool unrollDivisions=true;
    bool rotationHelpers=false;
    std::size_t earlyOutputs=0;
    std::size_t scratchReuses=0;
    std::size_t narrowedValues=0;
    std::size_t wideSelectBranches=0,wideByteCopies=0;
    std::size_t sharedSelectBranches=0;
    std::size_t predicateBranches=0;
    unsigned choiceBranchLimit=2;
    FiniteTables finite{g,{},{},0};
    bool scalarReturn=false;
    std::string inputPrefix="vxp_",resultPrefix="vxo->v",temporaryPrefix={};
    std::ostream* fixedFields=nullptr;
    std::function<std::string(Format)> temporary={};
    std::vector<std::string> destinations={};
    bool mutableInputs=false;
    using Locals=std::map<std::size_t,std::string>;
    using Choices=std::map<std::size_t,std::size_t>;
    mutable std::map<std::size_t,bool> wideConditions={};
    mutable std::set<std::string> runtimeHelpers={};
    void configure(ValueOptions options) {
        choiceBranchLimit=options.choiceBranches;
        finite.byteLimit=options.tableBytes;finite.searchLimit=options.tableSearch;
        reuseScratch=options.reuseScratch;
        scheduleOutputs=options.scheduleOutputs;
        shareDivisions=options.shareDivisions;
        unrollDivisions=options.unrollDivisions;
        rotationHelpers=options.rotationHelpers;
    }
    std::string helper(const std::string& name,const std::string& a,const std::string& b) const {
        runtimeHelpers.insert(name);
        std::string macro="VX_VALUE_";
        for(char c:name) macro+=c>='a' && c<='z' ? static_cast<char>(c-'a'+'A') : c;
        return macro+"("+a+", "+b+")";
    }
    bool wideCondition(std::size_t id) const {
        if(auto it=wideConditions.find(id);it!=wideConditions.end()) return it->second;
        const auto& n=g.nodes[id];
        return wideConditions[id]=n.type.bits>8 || std::any_of(n.args.begin(),n.args.end(),
            [&](auto child){return wideCondition(child);});
    }
    std::string lvalue(std::size_t id,const Locals& locals) const {
        if(auto it=locals.find(id);it!=locals.end())
            return it->second=="0" || it->second=="1" ? "" : it->second;
        return g.nodes[id].op=="input" ? inputPrefix+g.nodes[id].data : "";
    }
    Format scratchFormat(std::size_t id) const {
        const auto& n=g.nodes[id];const auto narrow=rangeFormat(n.lo,n.hi);
        return narrow.bits<n.type.bits ? narrow : n.type;
    }
    Format lvalueFormat(std::size_t id,const Locals& locals) const {
        return locals.contains(id) && g.nodes[id].op!="input" ? scratchFormat(id) : g.nodes[id].type;
    }
    std::string conversion(Format target,std::size_t source,std::string value) const {
        const auto type=g.nodes[source].type;
        // SDCC can discard a narrowing unsigned cast while forwarding a
        // shifted signed byte, then sign-extend it into the wider result.
        // State the zero extension as a bit operation too. This preserves
        // the source-width wrap independently of its physical storage type.
        if(!type.sign && type.bits<target.bits)
            value="("+cast(target,value)+" & "+Integer((Integer(1)<<type.bits)-1).str()+"UL)";
        return cast(target,value);
    }
    std::optional<Integer> literalValue(std::size_t id,const Choices& choices) const {
        std::vector<Format> conversions;
        for(;;) {
            const auto& node=g.nodes[id];
            if(auto chosen=choices.find(id);chosen!=choices.end()) {
                conversions.push_back(node.type);id=node.args[chosen->second];
            } else if(node.op=="cast") {
                conversions.push_back(node.type);id=node.args[0];
            } else if(node.op=="constant") {
                Integer value=node.lo;
                for(auto it=conversions.rbegin();it!=conversions.rend();++it)
                    value=FiniteTables::wrap(std::move(value),*it);
                return value;
            } else return {};
        }
    }
    std::string expression(std::size_t id,const Locals& locals,const Choices& choices={},bool test=false,
                           unsigned byteConsumerMask=255u) const {
        const auto& n=g.nodes[id];
        if(n.op!="constant" && n.lo==n.hi) return cast(n.type,FiniteTables::literal(n.lo,n.type));
        // Conversion chains and established choices can expose a literal.
        // Preserve each conversion, then give C a real immediate operand.
        // The logical graph and its case partitions remain intact.
        if(n.op!="constant") if(auto value=literalValue(id,choices))
            return cast(n.type,FiniteTables::literal(*value,n.type));
        if(auto it=locals.find(id);it!=locals.end()) return it->second;
        if(auto it=finite.lookups.find(id);it!=finite.lookups.end()) {
            const auto& lookup=it->second;const auto& table=finite.tables[lookup.table];
            if(table.entries.size()==1 && !table.guarded)
                return cast(n.type,FiniteTables::literal(table.entries[0],table.storage));
            auto index=cast({8,false},expression(lookup.index.value_or(lookup.input),locals,choices));
            if(!lookup.index) {
                if(lookup.shift) index="("+index+" >> "+std::to_string(lookup.shift)+")";
                if(lookup.mask!=(255u>>lookup.shift)) index="("+index+" & "+std::to_string(lookup.mask)+")";
            }
            auto read=table.entries.size()==1 ? FiniteTables::literal(table.entries[0],table.storage) :
                "vx_lut_"+std::to_string(lookup.table)+"["+index+"]";
            if(table.guarded) {
                auto predicate=table.limit-table.lower==1 ? index+" == "+std::to_string(table.lower) :
                    (table.lower ? cast({8,false},index+" - "+std::to_string(table.lower)) : index)+" < "+std::to_string(table.limit-table.lower);
                read="("+predicate+" ? "+read+" : "+FiniteTables::literal(table.fallback,table.storage)+")";
            }
            return cast(n.type,read);
        }
        auto arg=[&](std::size_t i){return expression(n.args[i],locals,choices);};
        auto condition=[&](std::size_t i){return expression(n.args[i],locals,choices,true);};
        if(auto it=choices.find(id);it!=choices.end()) return cast(n.type,arg(it->second));
        if(n.op=="constant") {
            if(n.lo==-2147483648LL) return "(-2147483647L - 1L)";
            return n.data+(n.type.bits==32 ? (n.type.sign ? "L" : "UL") : "");
        }
        if(n.op=="input") return inputPrefix+n.data;
        if(n.op=="cast") {
            // A directly enclosing byte mask can provide the shift's unsigned
            // correction. Carry that promise only through byte-preserving
            // conversions, never across a stored producer or other arithmetic.
            if(n.type==Format{8,false} && g.nodes[n.args[0]].type==n.type)
                return conversion(n.type,n.args[0],expression(n.args[0],locals,choices,false,byteConsumerMask));
            return conversion(n.type,n.args[0],arg(0));
        }
        if(n.op=="Not") return "(!("+condition(0)+"))";
        if(n.op=="And") return "("+condition(0)+" && "+condition(1)+")";
        if(n.op=="Select") {
            const auto& yes=g.nodes[n.args[1]];const auto& no=g.nodes[n.args[2]];
            // A wide control predicate need not materialize intermediate booleans.
            // Do not apply truth conversion to a stored value: (c ? 7 : 0)
            // must still store 7. Keep byte-only paths in their existing form:
            // materialized &&/|| can increase SDCC spills in those paths.
            if(test && wideCondition(id) && no.lo==0 && no.hi==0)
                return "("+condition(0)+" && "+condition(1)+")";
            if(test && wideCondition(id) && (yes.lo>0 || yes.hi<0))
                return "("+condition(0)+" || "+condition(2)+")";
            return "("+condition(0)+" ? "+conversion(n.type,n.args[1],arg(1))+" : "+conversion(n.type,n.args[2],arg(2))+")";
        }
        // Stored computations may still be forwarded through registers by
        // SDCC despite volatile loads. Protect the dynamic operand families
        // known to lose one operand when the accumulator is reused.
        std::map<std::size_t,bool> storedOperands;
        auto usesStoredComputation=[&](auto&& self,std::size_t operand)->bool {
            if(auto it=storedOperands.find(operand);it!=storedOperands.end()) return it->second;
            const auto& value=g.nodes[operand];
            bool stored=value.materialized || (value.op!="input" && locals.contains(operand));
            // An index or a selecting predicate is not the published value.
            // A table load produces a new operand; a selection forwards only
            // its arms. Following all dependencies here adds barriers to
            // independent constant arms merely because their guard was saved.
            if(!stored && !finite.lookups.contains(operand)) {
                if(value.op=="Select") {
                    if(auto selected=choices.find(operand);selected!=choices.end())
                        stored=self(self,value.args[selected->second]);
                    else stored=self(self,value.args[1]) || self(self,value.args[2]);
                } else for(auto child:value.args) if(self(self,child)) {stored=true;break;}
            }
            return storedOperands[operand]=stored;
        };
        auto storedComputation=[&](std::size_t operand) {
            return usesStoredComputation(usesStoredComputation,operand);
        };
        auto directStoredComputation=[&](std::size_t operand) {
            while(g.nodes[operand].op=="cast") operand=g.nodes[operand].args[0];
            return g.nodes[operand].materialized ||
                (g.nodes[operand].op!="input" && locals.contains(operand));
        };
        if(n.op=="Compare" || n.op=="SignedCompare") {
            auto a=arg(0),b=arg(1);
            const auto& left=g.nodes[n.args[0]];const auto& right=g.nodes[n.args[1]];
            if((n.data=="==" || n.data=="!=") && left.type==right.type && left.type.bits>8) {
                auto x=lvalue(n.args[0],locals),y=lvalue(n.args[1],locals);
                if(!x.empty() && !y.empty() && lvalueFormat(n.args[0],locals)==left.type &&
                   lvalueFormat(n.args[1],locals)==right.type)
                    return std::string(n.data=="!=" ? "(!" : "(")+"VX_VALUE_EQ"+
                        std::to_string(left.type.bits)+"("+x+", "+y+"))";
            }
            if(n.op=="Compare") {
                auto width=left.lo<0 || right.lo<0 ? 32u : std::max(left.type.bits,right.type.bits);
                a=cast({width,false},a);b=cast({width,false},b);
                if(width==8 && !literalValue(n.args[0],choices) && !literalValue(n.args[1],choices) &&
                   (mutableInputs || storedComputation(n.args[0]) || storedComputation(n.args[1]))) {
                    if(n.data=="<") return helper("lt8",a,b);
                    if(n.data==">") return helper("lt8",b,a);
                    if(n.data=="<=") return "(!"+helper("lt8",b,a)+")";
                    if(n.data==">=") return "(!"+helper("lt8",a,b)+")";
                }
            }
            return "("+a+" "+n.data+" "+b+")";
        }
        auto a=arg(0),b=arg(1);Format u{n.type.bits,false};
        const auto leftLiteral=literalValue(n.args[0],choices),rightLiteral=literalValue(n.args[1],choices);
        if(n.data=="&" && n.type==Format{8,false}) {
            if(rightLiteral) a=expression(n.args[0],locals,choices,false,
                FiniteTables::wrap(*rightLiteral,n.type).convert_to<unsigned>());
            if(leftLiteral) b=expression(n.args[1],locals,choices,false,
                FiniteTables::wrap(*leftLiteral,n.type).convert_to<unsigned>());
        }
        if(n.args[0]==n.args[1]) {
            if(n.data=="-" || n.data=="^") return cast(n.type,"0");
            if(n.data=="&" || n.data=="|") return cast(n.type,a);
            if(n.data=="+") {
                auto wide=n.type.bits<=8 ? Format{16,false} : Format{32,false};
                return cast(n.type,"("+cast(wide,cast(u,a))+" << 1)");
            }
        }
        if(rotationHelpers && n.data=="|" && n.type==Format{8,false}) {
            auto inlineNode=[&](std::size_t node)->std::optional<std::size_t> {
                while(!locals.contains(node) && !finite.lookups.contains(node)) {
                    const auto& child=g.nodes[node];
                    if(child.op!="cast" || child.type!=Format{8,false} || g.nodes[child.args[0]].type!=child.type)
                        return node;
                    node=child.args[0];
                }
                return {};
            };
            for(unsigned side=0;side<2;++side) {
                const auto right=inlineNode(n.args[side]),left=inlineNode(n.args[1-side]);
                if(!right || !left) continue;
                const auto& r=g.nodes[*right];const auto& l=g.nodes[*left];
                if(r.op!="Arithmetic" || l.op!="Arithmetic" || r.data!=">>" || l.data!="<<" ||
                   r.type!=Format{8,false} || l.type!=r.type || r.args[0]!=l.args[0]) continue;
                const auto rc=literalValue(r.args[1],choices),lc=literalValue(l.args[1],choices);
                if(!rc || !lc || *rc<=0 || *rc>=8 || *rc+*lc!=8) continue;
                // Both shifts still belong to this expression. Reading their
                // common stable producer once cannot extend a freed scratch
                // lifetime. The unsigned-byte leaf gives SDCC a real rotate,
                // without two forwarded operands or promoted shift bits.
                const auto name="ror8_"+rc->str();runtimeHelpers.insert(name);
                return "vx_value_"+name+"("+cast(n.type,expression(r.args[0],locals,choices))+")";
            }
        }
        if((n.data=="+" || n.data=="-") && rightLiteral && FiniteTables::wrap(*rightLiteral,n.type)==0)
            return cast(n.type,a);
        if(n.data=="+" && leftLiteral && FiniteTables::wrap(*leftLiteral,n.type)==0)
            return cast(n.type,b);
        auto shift=[&](const std::string& x,unsigned count) {
            if(!count) return cast(n.type,x);
            // SDCC can sign-extend an inlined wrapped byte before shifting.
            // Mask the unsigned byte result explicitly. For a fixed count
            // this keeps byte instructions and can join a consumer's mask,
            // without widening the operation to two registers.
            if(!n.type.sign && n.type.bits==8) {
                auto value="("+cast(u,x)+" >> "+std::to_string(count)+")";
                const auto mask=255u>>count;
                if(byteConsumerMask&~mask) value="("+value+" & "+std::to_string(mask)+"U)";
                return value;
            }
            return n.type.sign ? "VX_VALUE_ASR"+std::to_string(n.type.bits)+"("+cast(n.type,x)+", "+std::to_string(count)+")" :
                "("+cast(u,x)+" >> "+std::to_string(count)+")";
        };
        if(n.data==">>") {
            const auto& count=g.nodes[n.args[1]];
            if(count.lo==count.hi) return cast(n.type,shift(a,count.lo.convert_to<unsigned>()));
            const auto amount=cast({8,false},b);
            const auto unsignedOperand=n.type.bits==8 ? "("+cast({16,false},a)+" & 255U)" : cast(u,a);
            return cast(n.type,n.type.sign ? "VX_VALUE_ASR"+std::to_string(n.type.bits)+"("+cast(n.type,a)+", "+amount+")" :
                        "("+unsignedOperand+" >> "+amount+")");
        }
        if(n.data=="/" || n.data=="%") {
            auto compact=[&]() {
                const auto name=std::string(n.data=="/" ? "div" : "rem")+(n.type.sign ? "s" : "u")+
                    std::to_string(n.type.bits)+"_runtime";
                runtimeHelpers.insert(name);
                return "vx_value_"+name+"("+cast(n.type,a)+", "+cast(n.type,b)+")";
            };
            if(g.nodes[n.args[1]].lo!=g.nodes[n.args[1]].hi) return compact();
            const bool negative=g.nodes[n.args[1]].lo<0;
            Integer divisor=negative ? -g.nodes[n.args[1]].lo : g.nodes[n.args[1]].lo;
            if((divisor&(divisor-1))!=0) {
                if(!unrollDivisions) return compact();
                const auto name=std::string(n.data=="/" ? "div" : "rem")+(n.type.sign ? "s" : "u")+
                    std::to_string(n.type.bits)+"_"+(negative ? "n" : "p")+divisor.str();
                runtimeHelpers.insert(name);
                return "vx_value_"+name+"("+cast(n.type,a)+")";
            }
            unsigned count=0;
            for(auto d=divisor;d>1;d>>=1) ++count;
            std::string biased=a;
            if(n.type.sign && count) biased=cast(u,a)+" + ("+cast(n.type,a)+" < 0 ? "+Integer(divisor-1).str()+"U : 0U)";
            auto quotient=shift(biased,count);
            if(n.data=="/") return cast(n.type,negative ? "(0UL - "+cast(u,quotient)+")" : quotient);
            return cast(n.type,cast(u,a)+" - ("+cast(u,quotient)+" << "+std::to_string(count)+")");
        }
        // The host promotes uint16_t to signed int: a 16-bit product or left
        // shift can overflow that int. Other 8/16-bit operations fit, while
        // uint32_t arithmetic is already unsigned on both targets.
        if(n.data=="*") {
            // Modular multiplication only needs the result width. A power of
            // two needs one evaluation and one unsigned shift, including the
            // signed minimum whose bit pattern is the top power of two.
            for(unsigned constant=0;constant<2;++constant) {
                const auto& coefficient=constant ? rightLiteral : leftLiteral;
                if(!coefficient) continue;
                Integer factor=*coefficient&((Integer(1)<<n.type.bits)-1);
                if(factor==0) return cast(n.type,"0");
                bool negative=false;
                if(factor!=0 && (factor&(factor-1))!=0) {
                    factor=((Integer(1)<<n.type.bits)-factor);negative=true;
                }
                if(factor==0 || (factor&(factor-1))!=0) continue;
                unsigned count=0;for(auto value=factor;value>1;value>>=1) ++count;
                auto operand=cast(u,constant ? a : b);
                auto wide=n.type.bits<=8 ? Format{16,false} : Format{32,false};
                auto product=count ? "("+cast(wide,operand)+" << "+std::to_string(count)+")" : operand;
                if(negative) product="(0U - "+product+")";
                return cast(n.type,product);
            }
            for(unsigned constant=0;constant<2;++constant) {
                const auto& coefficient=constant ? rightLiteral : leftLiteral;
                if(!coefficient) continue;
                auto factor=(*coefficient&((Integer(1)<<n.type.bits)-1)).convert_to<unsigned>();
                const auto plan=sparseProduct(n.type.bits,factor);
                if(!plan) continue;
                // Immutable incoming values can be read twice without an
                // activation barrier. Keep computed/published operands in the
                // single-evaluation helper: forwarding those reads through
                // SDCC's accumulator has different correctness constraints.
                const auto& operand=g.nodes[n.args[1-constant]];
                // Word/byte inputs benefit from direct shifts; dword copies
                // otherwise increase register spills in the measured backend.
                if(n.type.bits<=16 && !mutableInputs && operand.op=="input" && !operand.materialized) {
                    auto wide=n.type.bits<=8 ? Format{16,false} : Format{32,false};
                    auto x=cast(wide,cast(u,constant ? a : b));
                    auto shifted=[&](unsigned count){return count ? "("+x+" << "+std::to_string(count)+")" : x;};
                    auto product="("+shifted(plan->left)+(plan->subtract ? " - " : " + ")+shifted(plan->right)+")";
                    if(plan->negate) product="(0U - "+product+")";
                    return cast(n.type,product);
                }
                auto name="scale"+std::to_string(n.type.bits)+"_"+std::to_string(factor);
                runtimeHelpers.insert(name);
                return cast(n.type,"vx_value_"+name+"("+cast(u,constant ? a : b)+")");
            }
            return cast(n.type,n.type.bits<=16 ? helper("mul"+std::to_string(n.type.bits),cast(u,a),cast(u,b)) :
                "VX_VALUE_MUL("+cast(u,a)+", "+cast(u,b)+")");
        }
        // SDCC can overwrite the accumulator when forwarding a computed
        // operand just stored by this function. Parameter publication already
        // has a function boundary; local computed values need one here too.
        if((n.data=="+" || n.data=="-") && n.type.bits==8 && !leftLiteral && !rightLiteral &&
           (mutableInputs || directStoredComputation(n.args[0]) || directStoredComputation(n.args[1]) ||
            (g.calculation && (storedComputation(n.args[0]) || storedComputation(n.args[1])))))
            return cast(n.type,helper(n.data=="+" ? "add8" : "sub8",cast(u,a),cast(u,b)));
        // Bitwise operators are subject to the same accumulator forwarding
        // failure. In particular, publishing a quotient/remainder can leave
        // the remainder in a register that SDCC then uses for both operands.
        if((n.data=="&" || n.data=="|" || n.data=="^") && n.type.bits==8 && !leftLiteral && !rightLiteral &&
           (mutableInputs || storedComputation(n.args[0]) || storedComputation(n.args[1])))
            return cast(n.type,helper(n.data=="&" ? "and8" : n.data=="|" ? "or8" : "xor8",cast(u,a),cast(u,b)));
        // A byte promoted into word arithmetic can corrupt the other word's
        // high byte during forwarding too. Look through conversions and
        // arithmetic (notably a negated displacement) to its stored producer.
        if(n.data=="+" && n.type.bits==16 && !leftLiteral && !rightLiteral &&
           (mutableInputs || storedComputation(n.args[0]) || storedComputation(n.args[1]))) {
            auto unsignedByte=[&](std::size_t operand)->std::string {
                while(g.nodes[operand].op=="cast" && g.nodes[operand].type.bits>=16) operand=g.nodes[operand].args[0];
                return g.nodes[operand].type==Format{8,false} ? lvalue(operand,locals) : "";
            };
            // Read stable byte cells inside the helper. Publishing their
            // values as byte OR word arguments can make SDCC forward the
            // preceding stored byte into both arguments. These private RAM
            // borrows last only for this pure operation and never escape.
            const auto left=unsignedByte(n.args[0]),right=unsignedByte(n.args[1]);
            if(!left.empty() && !right.empty()) return cast(n.type,helper("add8wide",left,right));
            return cast(n.type,helper("add16",cast(u,a),cast(u,b)));
        }
        if(n.data=="<<") {
            auto wide=n.type.bits<=8 ? Format{16,false} : Format{32,false};
            auto expression=cast(n.type,cast(wide,cast(u,a))+" << "+b);
            // Low-bit demand may select a width below a valid original count.
            // Those cases have zero retained bits, not an invalid C shift.
            if(g.nodes[n.args[1]].hi>=n.type.bits)
                expression="("+cast({8,false},b)+" >= "+std::to_string(n.type.bits)+" ? 0 : "+expression+")";
            return expression;
        }
        return cast(n.type,cast(u,a)+" "+n.data+" "+cast(u,b));
    }
    void store(std::size_t id,Format type,const std::string& destination,
               const Locals& locals,const std::string& pad,Choices choices={}) {
        // SDCC otherwise keeps a selected word live in a stack slot while
        // computing the surrounding expression. Select the path first, then
        // calculate directly into its destination. Do not acquire inactive
        // operands or change the width at the eliminated Select node.
        std::set<std::size_t> visited,selects;
        std::function<void(std::size_t)> visit=[&](std::size_t at) {
            if(locals.contains(at) || finite.lookups.contains(at) || !visited.insert(at).second) return;
            const auto& n=g.nodes[at];
            if(auto it=choices.find(at);it!=choices.end()) {visit(n.args[it->second]);return;}
            if(n.op=="Select") selects.insert(at);
            for(auto child:n.args) visit(child);
        };
        if(type.bits>8) visit(id);
        // Bound duplication per store. Larger expressions retain the existing
        // realization; this is a target cost policy, not a language limit.
        if(!selects.empty() && selects.size()<=3) {
            auto selected=*selects.rbegin();const auto& n=g.nodes[selected];
            ++wideSelectBranches;
            out<<pad<<"if ("<<expression(n.args[0],locals,choices,true)<<") {\n";
            auto yes=choices;yes[selected]=1;
            store(id,type,destination,locals,pad+"    ",std::move(yes));
            out<<pad<<"} else {\n";choices[selected]=2;
            store(id,type,destination,locals,pad+"    ",std::move(choices));
            out<<pad<<"}\n";return;
        }
        // A stable same-width leaf can be transferred byte by byte without
        // making SDCC allocate a wide intermediate. Inputs, temporaries and
        // result fields are distinct; the source is never an effectful lvalue.
        auto leaf=id;
        for(;;) {
            const auto& n=g.nodes[leaf];
            if(n.type.bits!=type.bits || locals.contains(leaf)) break;
            if(auto it=choices.find(leaf);it!=choices.end()) leaf=n.args[it->second];
            else if(n.op=="cast") leaf=n.args[0];
            else break;
        }
        std::string source;
        if(g.nodes[leaf].type==type && lvalueFormat(leaf,locals)==type) source=lvalue(leaf,locals);
        if(type.bits>8 && !destination.empty() && !source.empty()) {
            ++wideByteCopies;
            out<<pad<<"VX_VALUE_COPY"<<type.bits<<'('<<destination<<", "<<source<<");\n";
        } else out<<pad<<(destination.empty() ? "return " : destination+" = ")
                  <<conversion(type,id,expression(id,locals,choices))<<";\n";
    }
    std::map<std::size_t,unsigned> countUses(const std::vector<std::size_t>& values,const Locals& locals) const {
        std::map<std::size_t,unsigned> uses;
        std::set<std::size_t> visited;
        std::function<void(std::size_t)> count=[&](std::size_t id) {
            ++uses[id];if(locals.contains(id)) return;
            if(auto it=finite.lookups.find(id);it!=finite.lookups.end()) {
                const auto& table=finite.tables[it->second.table];
                if((table.entries.size()!=1 || table.guarded) && visited.insert(id).second)
                    count(it->second.index.value_or(it->second.input));
                return;
            }
            // Cheap casts stay inline. Their reuse belongs to the underlying
            // calculation; otherwise skipping a cast temporary duplicates it.
            if(g.nodes[id].op=="cast") {count(g.nodes[id].args[0]);return;}
            if(!visited.insert(id).second) return;
            for(auto child:g.nodes[id].args) count(child);
            if(g.nodes[id].data=="<<" && g.nodes[g.nodes[id].args[1]].hi>=g.nodes[id].type.bits)
                count(g.nodes[id].args[1]);
            if((g.nodes[id].data=="/" && g.nodes[id].type.sign) || g.nodes[id].data=="%") {
                const auto& operand=g.nodes[g.nodes[id].args[1]];
                Integer divisor=operand.lo;
                if(divisor<0) divisor=-divisor;
                if(operand.lo==operand.hi && (divisor&(divisor-1))==0) count(g.nodes[id].args[0]);
            }
        };
        for(auto id:values) count(id);
        return uses;
    }
    void scheduleShared(const std::map<std::size_t,unsigned>& uses,const Locals& locals,
                        const std::function<void(std::size_t)>& materialize) const {
        // Semantic nodes predate their parents, but a table's physical index
        // may have been added later. Follow those implementation dependencies
        // before publishing a shared value, preserving the old order elsewhere.
        std::set<std::size_t> scheduled;
        auto schedule=[&](auto&& self,std::size_t id)->void {
            if(locals.contains(id) || !scheduled.insert(id).second) return;
            if(auto found=finite.lookups.find(id);found!=finite.lookups.end()) {
                const auto& lookup=found->second;const auto& table=finite.tables[lookup.table];
                if(table.entries.size()!=1 || table.guarded) self(self,lookup.index.value_or(lookup.input));
            } else for(auto child:g.nodes[id].args) self(self,child);
            const auto& n=g.nodes[id];
            if(!uses.contains(id) || uses.at(id)<2 || n.op=="constant" || n.op=="input" || n.op=="cast") return;
            materialize(id);
        };
        for(auto [id,use]:uses) {(void)use;schedule(schedule,id);}
    }
    void scheduled(const std::vector<std::size_t>& values,Locals& locals,
                   const std::vector<std::size_t>& liveOut,
                   const std::function<std::string(Format)>& allocate,
                   const std::function<void(std::size_t,const std::string&)>& assign,
                   const std::function<void(std::size_t)>& output) {
        std::vector<std::size_t> definitions;
        scheduleShared(countUses(values,locals),locals,[&](std::size_t id){definitions.push_back(id);});
        const std::set<std::size_t> retained(definitions.begin(),definitions.end());
        auto references=[&](std::size_t root,bool defining) {
            std::set<std::size_t> reads,visited;
            auto visit=[&](auto&& self,std::size_t id,bool rhs)->void {
                if(!rhs && (retained.contains(id) || locals.contains(id))) {reads.insert(id);return;}
                if(!visited.insert(id).second) return;
                if(auto it=finite.lookups.find(id);it!=finite.lookups.end()) {
                    const auto& table=finite.tables[it->second.table];
                    if(table.entries.size()!=1 || table.guarded) self(self,it->second.index.value_or(it->second.input),false);
                } else for(auto child:g.nodes[id].args) self(self,child,false);
            };
            visit(visit,root,defining);return reads;
        };
        // Each consumer is a complete store, possibly several byte copies or
        // branches. Count consumers, not occurrences within its expression.
        std::vector<std::set<std::size_t>> reads;
        std::map<std::size_t,std::size_t> remaining;
        for(auto id:definitions) reads.push_back(references(id,true));
        for(auto id:values) reads.push_back(references(id,false));
        for(const auto& consumer:reads) for(auto id:consumer) ++remaining[id];
        for(auto root:liveOut) for(auto id:references(root,false)) ++remaining[id];
        std::map<std::string,std::vector<std::string>> free;
        std::set<std::size_t> owned;
        auto finish=[&](std::size_t consumer) {
            for(auto id:reads[consumer]) if(--remaining.at(id)==0 && owned.erase(id)) {
                free[scratchFormat(id).c()].push_back(locals.at(id));locals.erase(id);
            }
        };
        auto define=[&](std::size_t consumer) {
            const auto id=definitions[consumer];
            auto type=scratchFormat(id);auto& available=free[type.c()];std::string name;
            if(!reuseScratch || available.empty()) name=allocate(type);
            else {name=available.back();available.pop_back();++scratchReuses;}
            assign(id,name);locals.emplace(id,name);owned.insert(id);++shared;
            if(type.bits<g.nodes[id].type.bits) ++narrowedValues;
            // Never overwrite an operand while a wide store still reads it.
            finish(consumer);
        };
        if(g.region || !scheduleOutputs) {
            // A region's destinations can also be its mutable input cells.
            // Parallel transfer planning owns their read-before-write order.
            for(std::size_t i=0;i<definitions.size();++i) define(i);
            for(std::size_t i=0;i<values.size();++i) {output(i);finish(definitions.size()+i);}
        } else {
            // Pure outputs have disjoint destinations. Publish each result as
            // soon as its shared dependencies exist, shortening lifetimes
            // without changing output order or recomputing a shared producer.
            std::map<std::size_t,std::size_t> positions;
            for(std::size_t i=0;i<definitions.size();++i) positions.emplace(definitions[i],i);
            std::set<std::size_t> emitted;
            auto demand=[&](auto&& self,std::size_t id)->void {
                auto found=positions.find(id);
                if(found==positions.end() || !emitted.insert(id).second) return;
                for(auto dependency:reads[found->second]) self(self,dependency);
                define(found->second);
            };
            for(std::size_t i=0;i<values.size();++i) {
                for(auto dependency:reads[definitions.size()+i]) demand(demand,dependency);
                if(emitted.size()<definitions.size()) ++earlyOutputs;
                output(i);finish(definitions.size()+i);
            }
        }
        // Only live-out mappings survive. Inherited locals belong to the
        // enclosing scope and can never enter this block's reusable pool.
    }
    Locals block(const std::vector<std::size_t>& originalValues,const std::vector<Format>& types,
               Locals locals,const std::string& pad,std::vector<std::size_t> slots={},
               const std::vector<std::size_t>& liveOut={}) {
        auto values=originalValues;
        if(values.empty()) return locals;
        if(blocks==0) {
            g.lowerWidths(values,types);
            if(shareDivisions) values=g.shareDivision(values);
            finite.plan(values);values=finite.lowerNative(values);finite.prepareIndices();finite.declare(out,pad);
        }
        if(slots.empty()) for(std::size_t i=0;i<values.size();++i) slots.push_back(i);
        if(++blocks>(g.region ? 4096u : 64u)) throw Unavailable{"conditional value projection exceeds inline policy budget"};
        auto uses=countUses(values,locals);
        auto allocate=[&](Format type) {
            auto name="vxv_"+std::to_string(serial++);
            if(temporary) name=temporary(type);
            else if(fixedFields) {
                *fixedFields<<"    VX_STABLE "<<type.c()<<' '<<name<<";\n";
                name=temporaryPrefix+name;
            } else out<<pad<<"VX_STABLE "<<type.c()<<' '<<name<<";\n";
            bytes+=type.bits/8;return name;
        };
        auto materialize=[&](std::size_t id) {
            auto name=allocate(scratchFormat(id));
            store(id,scratchFormat(id),name,locals,pad);
            locals.emplace(id,name);++shared;
            if(scratchFormat(id).bits<g.nodes[id].type.bits) ++narrowedValues;
        };
        auto following=[&](std::size_t guard,const std::vector<std::size_t>& yes,const std::vector<std::size_t>& no) {
            auto roots=liveOut;roots.push_back(guard);
            roots.insert(roots.end(),yes.begin(),yes.end());roots.insert(roots.end(),no.begin(),no.end());
            return roots;
        };
        // A shared selected value need not be written and reloaded merely to
        // feed several outputs. Choose its path once, then specialize those
        // outputs. Keep complex alternatives and mutable regions on their
        // existing schedule, and bound code growth independently of semantics.
        if(!g.region && sharedSelectBranches+predicateBranches<choiceBranchLimit &&
           blocks<(choiceBranchLimit>2 ? 48u : 24u) && g.nodes.size()<(choiceBranchLimit>2 ? 256u : 128u)) {
            auto specialize=[&](std::size_t guard,std::optional<std::size_t> selected) {
                Graph before=g;auto beforeLookups=finite.lookups;
                std::vector<std::size_t> yes,no,fixed,branchSlots,fixedSlots;
                std::vector<Format> branchTypes,fixedTypes;
                try {
                    ValueChoice positive{g,selected,guard,true,locals,finite,{}},negative{g,selected,guard,false,locals,finite,{}};
                    for(std::size_t i=0;i<values.size();++i) {
                        const auto a=positive.value(values[i]),b=negative.value(values[i]);
                        if(a==b) {fixed.push_back(a);fixedSlots.push_back(slots[i]);fixedTypes.push_back(types[i]);}
                        else {yes.push_back(a);no.push_back(b);branchSlots.push_back(slots[i]);branchTypes.push_back(types[i]);}
                    }
                } catch(const Unavailable&) {g=std::move(before);finite.lookups=std::move(beforeLookups);return false;}
                if(yes.size()<2 || g.nodes.size()-before.nodes.size()>64) {
                    g=std::move(before);finite.lookups=std::move(beforeLookups);return false;
                }
                if(selected) ++sharedSelectBranches;else ++predicateBranches;
                // These values execute before either branch. Retain the
                // stable temporaries declared in this scope so consumers in
                // both branches can reuse their already computed producers.
                locals=block(fixed,fixedTypes,locals,pad,fixedSlots,following(guard,yes,no));
                out<<pad<<"if ("<<expression(guard,locals,{},true)<<") {\n";
                block(yes,branchTypes,locals,pad+"    ",branchSlots);
                out<<pad<<"} else {\n";
                block(no,branchTypes,locals,pad+"    ",branchSlots);
                out<<pad<<"}\n";return true;
            };
            // Different values can share the same decision. Select their path
            // together before wide arithmetic consumes them. Only selectors
            // demanded in this block contribute; never inspect an inactive arm
            // to justify hoisting its condition. Require two wide consumers
            // with a cheap alternative each: expanding nested lookup trees or
            // comparison-only paths did not repay its measured code/stack cost.
            std::map<std::size_t,std::set<std::size_t>> decisions;
            std::set<std::pair<std::size_t,bool>> decisionVisits;
            auto cheapAlternative=[&](std::size_t root) {
                std::set<std::size_t> armNodes;
                std::function<bool(std::size_t)> cheap=[&](std::size_t id) {
                    if(!armNodes.insert(id).second) return true;
                    if(armNodes.size()>8) return false;
                    if(locals.contains(id) || finite.lookups.contains(id)) return true;
                    const auto& n=g.nodes[id];
                    if(n.op=="Select" || n.op=="And") return false;
                    for(auto child:n.args) if(!cheap(child)) return false;
                    return true;
                };
                return cheap(root);
            };
            std::function<void(std::size_t,bool)> discover=[&](std::size_t id,bool wide) {
                if(locals.contains(id) || finite.lookups.contains(id) || !decisionVisits.emplace(id,wide).second) return;
                const auto& n=g.nodes[id];
                if(n.op=="Select") {
                    if(wide && (cheapAlternative(n.args[1]) || cheapAlternative(n.args[2])))
                        decisions[n.args[0]].insert(id);
                    discover(n.args[0],wide);return;
                }
                if(n.op=="And") {discover(n.args[0],wide);return;}
                if((n.op=="Arithmetic" || n.op=="SignedArithmetic") && n.type.bits>8) wide=true;
                for(auto child:n.args) discover(child,wide);
            };
            for(auto id:values) discover(id,false);
            for(const auto& [guard,selectors]:decisions) {
                const auto& predicate=g.nodes[guard];
                if(selectors.size()<2 || predicate.lo<0 || predicate.hi>1 ||
                   (locals.contains(guard) && (locals.at(guard)=="0" || locals.at(guard)=="1"))) continue;
                if(specialize(guard,std::nullopt)) return locals;
            }
            std::set<std::size_t> unconditional;
            std::function<void(std::size_t)> demand=[&](std::size_t id) {
                if(!unconditional.insert(id).second || locals.contains(id) || finite.lookups.contains(id)) return;
                const auto& n=g.nodes[id];
                if(n.op=="Select" || n.op=="And") demand(n.args[0]);
                else for(auto child:n.args) demand(child);
            };
            for(auto id:values) demand(id);
            for(auto [selected,use]:uses) {
                const auto choice=g.nodes[selected];
                if(use<2 || choice.op!="Select" || !unconditional.contains(selected) ||
                   finite.lookups.contains(selected) || locals.contains(selected)) continue;
                // Boolean selections already serve control flow. Hoisting a
                // shared predicate can make a formerly inactive path eager.
                if(choice.lo>=0 && choice.hi<=1) continue;
                // Splitting a value used only in constant comparisons often
                // duplicates control flow without removing a target barrier.
                // Require a consumer whose stored operand would need ADD/LT
                // isolation. This is a cost policy, not an eligibility proof.
                auto operand=[&](std::size_t id) {
                    while(g.nodes[id].op=="cast") id=g.nodes[id].args[0];
                    return id==selected;
                };
                bool avoidsBarrier=false;
                for(auto [consumer,consumerUses]:uses) {
                    (void)consumerUses;const auto& n=g.nodes[consumer];
                    if(n.args.size()!=2 || g.nodes[n.args[0]].op=="constant" || g.nodes[n.args[1]].op=="constant" ||
                       (!operand(n.args[0]) && !operand(n.args[1]))) continue;
                    if((n.op=="Arithmetic" || n.op=="SignedArithmetic") &&
                       ((n.data=="+" && n.type.bits<=16) || (n.data=="-" && n.type.bits==8))) avoidsBarrier=true;
                    if(n.op=="Compare" && (n.data=="<" || n.data==">" || n.data=="<=" || n.data==">=") &&
                       g.nodes[n.args[0]].lo>=0 && g.nodes[n.args[1]].lo>=0 &&
                       g.nodes[n.args[0]].type.bits==8 && g.nodes[n.args[1]].type.bits==8) avoidsBarrier=true;
                }
                if(!avoidsBarrier) continue;
                std::set<std::size_t> arms;
                std::function<bool(std::size_t)> cheap=[&](std::size_t id) {
                    if(!arms.insert(id).second) return true;
                    if(arms.size()>8 || g.nodes[id].op=="Select") return false;
                    for(auto child:g.nodes[id].args) if(!cheap(child)) return false;
                    return true;
                };
                if(!cheap(choice.args[1]) || !cheap(choice.args[2])) continue;
                if(specialize(choice.args[0],selected)) return locals;
            }
        }
        // Factor a common result guard before scheduling its alternatives.
        // In particular, an inactive update does no active-branch arithmetic.
        std::map<std::size_t,unsigned> guards;
        for(auto id:values) if(g.nodes[id].op=="Select" && !finite.lookups.contains(id)) ++guards[g.nodes[id].args[0]];
        for(auto [guard,occurrences]:guards) if(occurrences>1) {
            // Shared calculations still need stable storage across a guard.
            // Inputs already have volatile parameter storage under SDCC; a
            // second snapshot adds no protection. A factored guard is tested
            // once, unless another result also consumes its value.
            std::set<std::size_t> needed;
            std::function<void(std::size_t)> collect=[&](std::size_t id) {
                if(!needed.insert(id).second) return;
                if(auto it=finite.lookups.find(id);it!=finite.lookups.end()) {
                    const auto& table=finite.tables[it->second.table];
                    if(table.entries.size()!=1 || table.guarded) collect(it->second.index.value_or(it->second.input));
                    return;
                }
                for(auto child:g.nodes[id].args) collect(child);
            };collect(guard);
            for(auto id:needed) {
                const auto& n=g.nodes[id];
                if(uses[id]>1 && n.op!="constant" && n.op!="input" && n.op!="cast" &&
                   (id!=guard || uses[id]>occurrences) && !locals.contains(id)) materialize(id);
            }
            std::vector<std::size_t> yes,no,branchSlots,fixed,fixedSlots;
            std::vector<Format> branchTypes,fixedTypes;
            // Every pure output can use this established condition, even if
            // its original root tests a different threshold. Keep the facts
            // local to each arm; never narrow the shared graph globally.
            std::vector<std::size_t> positive,negative;
            if(!g.region) {
                Graph before=g;auto beforeLookups=finite.lookups;
                try {
                    ValueChoice a{g,{},guard,true,locals,finite,{}},b{g,{},guard,false,locals,finite,{}};
                    for(auto value:values) {positive.push_back(a.value(value));negative.push_back(b.value(value));}
                    if(g.nodes.size()-before.nodes.size()>64) throw Unavailable{"branch fact growth exceeds policy"};
                } catch(const Unavailable&) {
                    g=std::move(before);finite.lookups=std::move(beforeLookups);
                    positive.clear();negative.clear();
                }
            }
            for(std::size_t i=0;i<values.size();++i) {
                const auto& n=g.nodes[values[i]];
                const bool selectedRoot=n.op=="Select" && n.args[0]==guard && !finite.lookups.contains(values[i]);
                // Moving a partially simplified result into both arms made
                // SDCC retain more registers with frame projections. Keep
                // that placement unless both arms become immediate values;
                // direct value functions and roots already on this branch use
                // the complete specialization.
                const bool useFacts=!positive.empty() && (!fixedFields || selectedRoot ||
                    positive[i]==negative[i] ||
                    (g.nodes[positive[i]].op=="constant" && g.nodes[negative[i]].op=="constant"));
                if(useFacts) {
                    if(positive[i]==negative[i]) {
                        fixed.push_back(positive[i]);fixedSlots.push_back(slots[i]);fixedTypes.push_back(types[i]);
                    } else {
                        yes.push_back(positive[i]);no.push_back(negative[i]);
                        branchSlots.push_back(slots[i]);branchTypes.push_back(types[i]);
                    }
                } else if(selectedRoot) {
                    yes.push_back(n.args[1]);no.push_back(n.args[2]);
                    branchSlots.push_back(slots[i]);branchTypes.push_back(types[i]);
                } else {
                    fixed.push_back(values[i]);fixedSlots.push_back(slots[i]);fixedTypes.push_back(types[i]);
                }
            }
            // These leaves are required regardless of this guard. Emit them
            // once, retaining their original result slots through nested
            // splits. Every calculation is certified total and effect-free.
            if(g.region) block(fixed,fixedTypes,locals,pad,fixedSlots);
            else locals=block(fixed,fixedTypes,locals,pad,fixedSlots,following(guard,yes,no));
            out<<pad<<"if ("<<expression(guard,locals,{},true)<<") {\n";
            auto yesLocals=locals,noLocals=locals;
            if(g.nodes[guard].lo>=0 && g.nodes[guard].hi<=1) yesLocals[guard]="1";
            noLocals[guard]="0";
            block(yes,branchTypes,yesLocals,pad+"    ",branchSlots);out<<pad<<"} else {\n";
            block(no,branchTypes,noLocals,pad+"    ",branchSlots);out<<pad<<"}\n";return locals;
        }
        scheduled(values,locals,liveOut,allocate,[&](std::size_t id,const std::string& name) {
            store(id,scratchFormat(id),name,locals,pad);
        },[&](std::size_t i) {
            store(values[i],types[i],scalarReturn ? "" : (destinations.empty() ? resultPrefix+std::to_string(slots[i]) : destinations.at(slots[i])),locals,pad);
        });
        return locals;
    }
};
}

CalculationProjection projectCalculations(const std::vector<Ref>& values,
    const std::function<std::optional<CalculationInput>(const Ref&)>& input,bool reuseScratch,unsigned linearTerms,bool shareDivisions,bool unrollDivisions,bool rotationHelpers) {
    try {
        if(values.empty() || values.size()>64) return {};
        Graph graph;graph.calculation=true;graph.input=input;graph.linearTermLimit=linearTerms;
        std::vector<std::size_t> roots;std::vector<Format> types;
        for(const auto& value:values) {roots.push_back(graph.value(value));types.push_back(graph.nodes[roots.back()].type);}
        graph.lowerWidths(roots,types);
        if(shareDivisions) roots=graph.shareDivision(roots);
        std::ostringstream unused;
        Emitter emitter{graph,unused};emitter.inputPrefix="";
        emitter.reuseScratch=reuseScratch;
        emitter.unrollDivisions=unrollDivisions;
        emitter.rotationHelpers=rotationHelpers;
        Emitter::Locals locals;
        CalculationProjection result;std::string sequence;
        emitter.scheduled(roots,locals,{},[&](Format type) {
            auto name="vxcse_"+std::to_string(result.scratch.size());
            result.scratch.push_back({name,type.c(),type.bits/8});return name;
        },[&](std::size_t id,const std::string& name) {
            sequence+=name+" = "+cast(graph.nodes[id].type,emitter.expression(id,locals))+", ";
        },[&](std::size_t i) {
            const auto root=roots[i];
            auto expression=emitter.expression(root,locals);
            // Ordinary rendering can widen this value outside the projected
            // interval. Publish unsigned low bits explicitly before crossing
            // that boundary, retaining the same SDCC zero-extension safeguard
            // used for typed value stores.
            if(!graph.nodes[root].type.sign && graph.nodes[root].type.bits<32)
                expression=emitter.conversion({32,false},root,std::move(expression));
            result.expressions.push_back("("+sequence+expression+")");sequence.clear();
        });
        // A group is useful only when it shares a retained producer. Otherwise
        // keep the established independent intervals and their narrower local
        // demands, avoiding a new storage schedule without any common work.
        if(values.size()>1 && !emitter.shared) return {};
        // A certified division or shift has a native realization even
        // when there is no shared scratch or narrower result to publish.
        // Keeping the generic renderer here would retain a checked 32-bit
        // runtime operation despite an already established native domain.
        const bool domain=std::any_of(graph.nodes.begin(),graph.nodes.end(),[](const Node& n) {
            return (n.op=="Arithmetic" || n.op=="SignedArithmetic") &&
                (n.data=="/" || n.data=="%" || n.data=="<<" || n.data==">>");
        });
        bool constant=false;
        for(std::size_t i=0;i<values.size();++i) constant|=graph.nodes[roots[i]].op=="constant" && values[i]->kind!=Term::Number;
        if(result.scratch.empty() && !graph.linearCombinations && !graph.narrowedOperations && !domain && !constant) return {};
        result.sharedValues=emitter.shared;
        result.scratchReuses=emitter.scratchReuses;
        result.narrowedValues=emitter.narrowedValues;
        result.narrowedOperations=graph.narrowedOperations;
        result.linearCombinations=graph.linearCombinations;
        result.sharedDivisions=graph.sharedDivisions;
        result.helpers=std::move(emitter.runtimeHelpers);
        return result;
    } catch(const Unavailable&) {return {};}
}
std::string calculationRuntimeIncludes(const std::set<std::string>& helpers) {
    return runtimeIncludes(helpers);
}

ValueProjection projectValue(const std::string& name,const std::vector<Ref>& inputs,
                             const std::vector<Ref>& types,const std::vector<Ref>& values,
                             const std::vector<Ref>& paths,const Ref& execution,bool exclusive,ValueOptions options) {
    try {
        if(values.empty()) throw Unavailable{"empty value interface"};
        Graph graph;graph.linearTermLimit=options.linearTerms;std::ostringstream out;std::string parameters;
        for(const auto& leaf:inputs) {
            const auto& a=leaf->arguments;auto f=format(a[2]);
            graph.inputs.emplace(a[0]->name,graph.add({"input",a[0]->name,f,a[3]->number,a[4]->number,{}}));
            parameters+=", VX_STABLE "+f.c()+" vxp_"+a[0]->name;
        }
        graph.trace(execution);
        std::vector<std::size_t> roots;std::vector<Format> formats;
        for(std::size_t i=0;i<values.size();++i) {roots.push_back(graph.value(values[i]));formats.push_back(format(types[i]));}
        std::ostringstream resultType;
        resultType<<"#ifndef VX_VALUE_"<<name<<"_RESULT\n#define VX_VALUE_"<<name<<"_RESULT\n"
                  <<"typedef struct {\n";
        for(std::size_t i=0;i<formats.size();++i) resultType<<"    "<<formats[i].c()<<" v"<<i<<"; /* "<<paths[i]->name<<" */\n";
        resultType<<"} vx_"<<name<<"_result;\n#endif\n";
        out<<"/* Pure, total integer value projection. Caller owns the result. */\n"
           <<"#ifndef VX_VALUE_"<<name<<"_H\n#define VX_VALUE_"<<name<<"_H\n"
           <<"#include \"vexel.h\"\n#include \"vx_value_runtime.h\"\n"
           <<resultType.str()
           <<"static inline void vx_"<<name<<"_value(vx_"<<name<<"_result *vxo"<<parameters<<") {\n";
        // Preserve the ordinary source's input-domain checks. Arguments are
        // evaluated once by the C call before these checks or any result write.
        for(const auto& leaf:inputs) {
            const auto& a=leaf->arguments;auto f=format(a[2]);
            Integer lo=f.sign ? -(Integer(1)<<(f.bits-1)) : Integer(0),hi=(Integer(1)<<(f.bits-(f.sign ? 1 : 0)))-1;
            if(a[3]->number>lo) out<<"    if (vxp_"<<a[0]->name<<" < "<<a[3]->number<<"L) vx_trap(16);\n";
            if(a[4]->number<hi) out<<"    if (vxp_"<<a[0]->name<<" > "<<a[4]->number<<(f.sign ? "L" : "UL")<<") vx_trap(16);\n";
        }
        Graph valueGraph=graph;
        Emitter emitter{valueGraph,out};emitter.configure(options);
        emitter.block(roots,formats,{},"    ");
        out<<"}\n#endif\n";
        ValueProjection result;result.header=selectRuntime(out.str(),emitter.runtimeHelpers);result.reason="closed total integer graph";
        result.sharedValues=emitter.shared;result.localBytes=emitter.bytes;
        result.scratchReuses=emitter.scratchReuses;
        result.narrowedValues=emitter.narrowedValues;
        result.narrowedOperations=valueGraph.narrowedOperations;result.earlyOutputs=emitter.earlyOutputs;
        result.linearCombinations=valueGraph.linearCombinations;
        result.sharedDivisions=valueGraph.sharedDivisions;
        bool fullDomain=true;
        for(const auto& leaf:inputs) {
            const auto& a=leaf->arguments;Node n;n.type=format(a[2]);Graph::bounds(n);
            if(n.lo!=a[3]->number || n.hi!=a[4]->number) fullDomain=false;
        }
        result.fixedReason=!exclusive ? "activation ownership is not exclusive" :
            (!fullDomain ? "input checks can cross a trap boundary" : "closed exclusive total integer graph");
        if(exclusive && fullDomain) {
            std::ostringstream fixed,fields,body;
            const auto frame="vx_"+name+"_fixed_frame";
            for(const auto& leaf:inputs) {
                const auto& a=leaf->arguments;auto f=format(a[2]);
                fields<<"    VX_STABLE "<<f.c()<<' '<<a[0]->name<<";\n";
                result.fixedBytes+=f.bits/8;
            }
            Graph fixedGraph=graph;
            Emitter fixedEmitter{fixedGraph,body};
            fixedEmitter.configure(options);
            fixedEmitter.inputPrefix=frame+".";fixedEmitter.temporaryPrefix=frame+".";
            fixedEmitter.resultPrefix=frame+".result.v";fixedEmitter.fixedFields=&fields;
            fixedEmitter.block(roots,formats,{},"    ");
            result.fixedBytes+=fixedEmitter.bytes;
            for(auto f:formats) result.fixedBytes+=f.bits/8;
            fixed<<"/* Exclusive fixed-address alternative. Set every input before calling;\n"
                 <<" * consume/copy the result before this occurrence is invoked again.\n"
                 <<" * No source effects, callbacks or recursive calls occur inside.\n"
                 <<" * Each including translation unit owns its own frame. */\n"
                 <<"#ifndef VX_FIXED_"<<name<<"_H\n#define VX_FIXED_"<<name<<"_H\n"
                 <<"#include \"vexel.h\"\n#include \"vx_value_runtime.h\"\n"<<resultType.str()
                 <<"static struct {\n"<<fields.str()<<"    vx_"<<name<<"_result result;\n} "<<frame<<";\n"
                 <<"#ifdef __SDCC_z80\ntypedef char vx_"<<name<<"_fixed_size[sizeof("<<frame<<")=="<<result.fixedBytes<<" ? 1 : -1];\n#endif\n"
                 <<"static void vx_"<<name<<"_fixed(void) {\n"<<body.str()<<"}\n#endif\n";
            result.fixedHeader=selectRuntime(fixed.str(),fixedEmitter.runtimeHelpers);
        }
        // Also expose a dependency split around a shared result guard, carrying
        // invariant outputs and the guard's established value alongside it.
        // The adapter can acquire only the guard inputs before choosing
        // an arm. This is a projection of the SAME graph, not another source
        // evaluation or a guessed predicate. Restricted input domains would
        // require checks even for an unused arm, so retain the full API there.
        auto guard=caseGuard(graph,roots);bool split=true;
        for(const auto& leaf:inputs) {
            const auto& a=leaf->arguments;Node n;n.type=format(a[2]);Graph::bounds(n);
            if(n.lo!=a[3]->number || n.hi!=a[4]->number) split=false;
        }
        if(split && guard) try {
            std::ostringstream stages;
            std::set<std::string> runtimeHelpers;
            stages<<"/* Dependency-split value API. Evaluate branch, then ONLY its selected\n"
                  <<" * case with the same input snapshot. No effects may intervene. */\n"
                  <<"#ifndef VX_VALUE_"<<name<<"_CASES_H\n#define VX_VALUE_"<<name<<"_CASES_H\n"
                  <<"#include \"vexel.h\"\n#include \"vx_value_runtime.h\"\n"<<resultType.str();
            auto arguments=[&](const Graph& inputGraph,const std::vector<std::size_t>& selected,std::vector<std::size_t>& indices) {
                std::set<std::size_t> needed;
                std::function<void(std::size_t)> visit=[&](std::size_t id) {
                    if(!needed.insert(id).second) return;
                    for(auto child:inputGraph.nodes[id].args) visit(child);
                };
                for(auto id:selected) visit(id);
                std::string text;
                for(std::size_t i=0;i<inputs.size();++i) {
                    const auto& a=inputs[i]->arguments;
                    if(!needed.contains(graph.inputs.at(a[0]->name))) continue;
                    indices.push_back(i);
                    if(!text.empty()) text+=", ";
                    text+="VX_STABLE "+format(a[2]).c()+" vxp_"+a[0]->name;
                }
                return text;
            };
            auto predicateArgs=arguments(graph,{guard->condition},result.branchInputs);
            stages<<"static inline uint8_t vx_"<<name<<"_branch("<<(predicateArgs.empty() ? "void" : predicateArgs)<<") {\n";
            Graph predicateGraph=graph;
            Emitter predicate{predicateGraph,stages};predicate.scalarReturn=true;
            predicate.configure(options);
            predicate.block({guard->condition},{Format{}},{},"    ");stages<<"}\n";
            runtimeHelpers.insert(predicate.runtimeHelpers.begin(),predicate.runtimeHelpers.end());
            for(unsigned arm=0;arm<2;++arm) {
                Graph caseGraph=graph;
                auto selected=caseValues(caseGraph,roots,*guard,arm!=0);
                result.caseInputs.emplace_back();auto args=arguments(caseGraph,selected,result.caseInputs.back());
                stages<<"static inline void vx_"<<name<<"_case"<<arm<<"_value(vx_"<<name<<"_result *vxo";
                if(!args.empty()) stages<<", "<<args;
                stages<<") {\n";
                Emitter body{caseGraph,stages};body.configure(options);
                body.block(selected,formats,{},"    ");stages<<"}\n";
                runtimeHelpers.insert(body.runtimeHelpers.begin(),body.runtimeHelpers.end());
            }
            stages<<"#endif\n";result.casesHeader=selectRuntime(stages.str(),runtimeHelpers);
        } catch(const Unavailable&) {
            // An optional case projection must not discard the already
            // certified full-value interface when its local budget runs out.
            result.branchInputs.clear();result.caseInputs.clear();result.casesHeader.clear();
        }
        return result;
    } catch(const Unavailable& unavailable) {ValueProjection result;result.reason=unavailable.reason;return result;}
}

ValueBindingProjection projectValueBinding(const std::string& name,const std::string& source,
    const std::string& entry,const std::vector<Ref>& inputs,const std::vector<Ref>& types,
    const std::vector<Ref>& values,const std::vector<Ref>& paths,const Ref& execution,ValueOptions options) {
    try {
        Graph graph;graph.linearTermLimit=options.linearTerms;
        for(const auto& leaf:inputs) {
            const auto& a=leaf->arguments;
            graph.inputs.emplace(a[0]->name,graph.add({"input",a[0]->name,format(a[2]),a[3]->number,a[4]->number,{}}));
        }
        graph.trace(execution);
        std::vector<std::size_t> roots;std::vector<Format> formats;
        for(std::size_t i=0;i<values.size();++i) {roots.push_back(graph.value(values[i]));formats.push_back(format(types[i]));}
        // The caller checked that the existing case interface certifies this
        // split. A case is part of that source, not a new source observation.
        if(entry!="full") {
            const auto guard=caseGuard(graph,roots);
            if(!guard) throw Unavailable{"source has no result guard"};
            roots=caseValues(graph,roots,*guard,entry=="case1");
        }
        std::set<std::size_t> needed;
        std::function<void(std::size_t)> visit=[&](std::size_t id) {
            if(!needed.insert(id).second) return;
            for(auto child:graph.nodes[id].args) visit(child);
        };
        for(auto id:roots) visit(id);
        ValueBindingProjection result;
        const auto frame="vx_bind_"+name+"_frame",type=frame+"_type";
        std::ostringstream fields,body,common,typesHeader;
        for(std::size_t i=0;i<inputs.size();++i) {
            const auto& a=inputs[i]->arguments;
            if(entry!="full" && !needed.contains(graph.inputs.at(a[0]->name))) continue;
            auto f=format(a[2]);result.inputs.push_back(i);result.bytes+=f.bits/8;
            fields<<"    VX_STABLE "<<f.c()<<' '<<a[0]->name<<";\n";
            if(entry=="full") {
                Node n;n.type=f;Graph::bounds(n);
                if(a[3]->number>n.lo) body<<"    if ("<<frame<<'.'<<a[0]->name<<" < "<<a[3]->number<<"L) vx_trap(16);\n";
                if(a[4]->number<n.hi) body<<"    if ("<<frame<<'.'<<a[0]->name<<" > "<<a[4]->number<<(f.sign ? "L" : "UL")<<") vx_trap(16);\n";
            }
        }
        Emitter emitter{graph,body};emitter.inputPrefix=frame+".";
        emitter.configure(options);
        emitter.temporaryPrefix=frame+".";emitter.resultPrefix=frame+".result.v";emitter.fixedFields=&fields;
        emitter.block(roots,formats,{},"    ");result.bytes+=emitter.bytes;
        common<<"/* Selected C binding. Declare LOCAL in each owning scope, set all reported\n"
              <<" * inputs, run, and consume/copy results before reusing its workspace.\n"
              <<" * Case entries require the matching guard and one unchanged snapshot. */\n"
              <<"#ifndef VX_BIND_"<<name<<"_H\n#define VX_BIND_"<<name<<"_H\n"
              <<"#include \"vx_binding_types.h\"\n"<<runtimeIncludes(emitter.runtimeHelpers);
        typesHeader<<"#ifndef VX_VALUE_"<<source<<"_RESULT\n#define VX_VALUE_"<<source<<"_RESULT\ntypedef struct {\n";
        for(std::size_t i=0;i<formats.size();++i) {
            typesHeader<<"    "<<formats[i].c()<<" v"<<i<<"; /* "<<paths[i]->name<<" */\n";
            result.bytes+=formats[i].bits/8;
        }
        typesHeader<<"} vx_"<<source<<"_result;\n#endif\n"
              <<"typedef struct {\n"<<fields.str()<<"    vx_"<<source<<"_result result;\n} "<<type<<";\n"
              <<"#ifdef __SDCC_z80\ntypedef char vx_bind_"<<name<<"_size[sizeof("<<type<<")=="<<result.bytes<<" ? 1 : -1];\n#endif\n";
        // Keep publication separate from computation. SDCC can forward a
        // just-published byte while clobbering another still-live operand in
        // an expanded C macro, even with volatile fields. A real function
        // boundary prevents that propagation; fixed calls need no parameters.
        auto automaticBody=body.str();const auto prefix=frame+".";
        for(std::size_t at=0;(at=automaticBody.find(prefix,at))!=std::string::npos;at+=5)
            automaticBody.replace(at,prefix.size(),"vxf->");
        result.types=typesHeader.str();result.headerPrefix=common.str();
        result.automaticHeader=common.str()+"#define VX_BIND_"+name+"_LOCAL "+type+" "+frame+"\n"
            +"static void vx_bind_"+name+"_compute("+type+" * VX_STABLE vxf) {\n"+automaticBody+"}\n"
            +"#define vx_bind_"+name+"_run() vx_bind_"+name+"_compute(&"+frame+")\n#endif\n";
        result.fixedFunction="static void vx_bind_"+name+"_run(void) {\n"+body.str()+"}\n";
        result.reuseScratch=emitter.reuseScratch;result.scratchReuses=emitter.scratchReuses;
        result.scheduleOutputs=emitter.scheduleOutputs;result.earlyOutputs=emitter.earlyOutputs;
        result.narrowedValues=emitter.narrowedValues;
        result.narrowedOperations=graph.narrowedOperations;
        result.linearCombinations=graph.linearCombinations;result.linearTerms=graph.linearTermLimit;
        result.shareDivisions=emitter.shareDivisions;result.sharedDivisions=graph.sharedDivisions;
        result.unrollDivisions=emitter.unrollDivisions;
        result.rotationHelpers=emitter.rotationHelpers;
        result.wideSelectBranches=emitter.wideSelectBranches;
        result.wideByteCopies=emitter.wideByteCopies;
        result.sharedSelectBranches=emitter.sharedSelectBranches;
        result.predicateBranches=emitter.predicateBranches;
        result.finiteConstants=emitter.finite.nativeConstants;
        result.finiteGuards=emitter.finite.nativeGuards;
        result.finiteArithmetic=emitter.finite.nativeArithmetic;
        result.finiteTables=emitter.finite.storedTables().size();
        result.finiteTableBytes=emitter.finite.bytes;
        for(const auto& [id,lookup]:emitter.finite.lookups) {
            (void)id;const auto& table=emitter.finite.tables[lookup.table];
            if(table.entries.size()>1) ++result.finiteLookups;
            else if(table.guarded) ++result.finiteGuards;
            else ++result.finiteConstants;
        }
        return result;
    } catch(const Unavailable& failure) {throw std::invalid_argument("C binding "+name+": "+failure.reason);}
}

#include "source_region.inc"

std::map<std::string,std::string> valueRuntimeFiles() {
    std::map<std::string,std::string> files;
    files["vx_support_value_base.h"]=R"C(/* The Z80 realization uses SDCC's two's-complement casts and arithmetic
 * signed shifts. Host formulas express the same values without signed overflow
 * or negative shifts. Arithmetic macro operands are evaluated once. Internal
 * copy macros require stable, non-overlapping lvalues of the same width. */
#ifndef VX_SUPPORT_VALUE_BASE_H
#define VX_SUPPORT_VALUE_BASE_H
#include <stdint.h>
#ifndef VX_VALUE_LOCATION
#define VX_VALUE_LOCATION
#endif
#if defined(__SDCC_z80)
#define VX_VALUE_I8(x) ((int8_t)(uint8_t)(x))
#define VX_VALUE_I16(x) ((int16_t)(uint16_t)(x))
#define VX_VALUE_I32(x) ((int32_t)(uint32_t)(x))
#define VX_VALUE_ASR8(x,n) ((int8_t)(x) >> (n))
#define VX_VALUE_ASR16(x,n) ((int16_t)(x) >> (n))
#define VX_VALUE_ASR32(x,n) ((int32_t)(x) >> (n))
/* Stable byte transfers avoid wide register spills. The caller owns all
 * fields for this computation; these are not atomic or external IO copies. */
#define VX_VALUE_COPY16(d,s) do { \
    ((volatile uint8_t *)&(d))[0]=((const volatile uint8_t *)&(s))[0]; \
    ((volatile uint8_t *)&(d))[1]=((const volatile uint8_t *)&(s))[1]; \
} while(0)
#define VX_VALUE_COPY32(d,s) do { \
    ((volatile uint8_t *)&(d))[0]=((const volatile uint8_t *)&(s))[0]; \
    ((volatile uint8_t *)&(d))[1]=((const volatile uint8_t *)&(s))[1]; \
    ((volatile uint8_t *)&(d))[2]=((const volatile uint8_t *)&(s))[2]; \
    ((volatile uint8_t *)&(d))[3]=((const volatile uint8_t *)&(s))[3]; \
} while(0)
/* Equality needs no word-sized intermediate. Only stable same-type lvalues
 * reach these macros; short-circuit reads cannot observe external effects. */
#define VX_VALUE_EQ16(a,b) ( \
    ((const volatile uint8_t *)&(a))[0]==((const volatile uint8_t *)&(b))[0] && \
    ((const volatile uint8_t *)&(a))[1]==((const volatile uint8_t *)&(b))[1])
#define VX_VALUE_EQ32(a,b) (VX_VALUE_EQ16(a,b) && \
    ((const volatile uint8_t *)&(a))[2]==((const volatile uint8_t *)&(b))[2] && \
    ((const volatile uint8_t *)&(a))[3]==((const volatile uint8_t *)&(b))[3])
/* The freestanding project supplies vx_mul, not SDCC's implicit lib helpers. */
#define VX_VALUE_MUL(a,b) vx_mul((uint32_t)(a),(uint32_t)(b))
#else
#define VX_VALUE_I8(x) ((int8_t)((int16_t)((uint8_t)(x) ^ 128u) - 128))
#define VX_VALUE_I16(x) ((int16_t)((int32_t)((uint16_t)(x) ^ 32768u) - 32768L))
#define VX_VALUE_I32(x) ((int32_t)((int64_t)((uint32_t)(x) ^ 2147483648UL) - 2147483648LL))
#define VX_VALUE_ASR8(x,n) ((int8_t)(((int16_t)((uint8_t)(x) ^ 128u) >> (n)) - (int16_t)(128u >> (n))))
#define VX_VALUE_ASR16(x,n) ((int16_t)(((int32_t)((uint16_t)(x) ^ 32768u) >> (n)) - (int32_t)(32768u >> (n))))
#define VX_VALUE_ASR32(x,n) ((int32_t)(((int64_t)((uint32_t)(x) ^ 2147483648UL) >> (n)) - (int64_t)(2147483648UL >> (n))))
#define VX_VALUE_MUL(a,b) ((uint32_t)(a)*(uint32_t)(b))
#define VX_VALUE_COPY16(d,s) ((d)=(s))
#define VX_VALUE_COPY32(d,s) ((d)=(s))
#define VX_VALUE_EQ16(a,b) ((a)==(b))
#define VX_VALUE_EQ32(a,b) ((a)==(b))
#endif
#endif
)C";
    // Each helper has its own guard: independent generated headers can demand
    // different subsets in any order. Keep the umbrella as the complete public
    // runtime for handwritten C, including code that mixes both interfaces.
    std::string umbrella="/* Complete value runtime for handwritten C. Generated headers select components. */\n"
        "#ifndef VX_VALUE_RUNTIME_H\n#define VX_VALUE_RUNTIME_H\n#include \"vx_support_value_base.h\"\n";
    for(const auto& [name,macro,type,operation]:std::vector<std::tuple<std::string,std::string,std::string,std::string>>{
            {"add8","ADD8","uint8_t","+"},{"sub8","SUB8","uint8_t","-"},
            {"add16","ADD16","uint16_t","+"},{"lt8","LT8","uint8_t","<"},
            {"and8","AND8","uint8_t","&"},{"or8","OR8","uint8_t","|"},{"xor8","XOR8","uint8_t","^"}}) {
        auto file="vx_support_value_"+name+".h";
        files[file]="/* Keep operand publication separate from arithmetic. These private leaf\n"
            " * helpers use register arguments; their immutable parameters need no spill. */\n"
            "#ifndef VX_VALUE_"+macro+"_H\n#define VX_VALUE_"+macro+"_H\n#include \"vx_support_value_base.h\"\n"
            "#ifdef __SDCC_z80\nstatic "+type+" vx_value_"+name+"("+type+" a, "+type+" b) __sdcccall(1) VX_VALUE_LOCATION {\n"
            "    return ("+type+")(a"+operation+"b);\n}\n"
            "#define VX_VALUE_"+macro+"(a,b) vx_value_"+name+"(("+type+")(a),("+type+")(b))\n"
            "#else\n#define VX_VALUE_"+macro+"(a,b) (("+type+")(("+type+")(a)"+operation+"("+type+")(b)))\n#endif\n#endif\n";
        umbrella+="#include \""+file+"\"\n";
    }
    for(unsigned width:{8u,16u}) {
        auto name="mul"+std::to_string(width),macro="MUL"+std::to_string(width);
        auto type="uint"+std::to_string(width)+"_t",file="vx_support_value_"+name+".h";
        files[file]="/* Private register ABI; the low product bits need no 32-bit runtime call. */\n"
            "#ifndef VX_VALUE_"+macro+"_H\n#define VX_VALUE_"+macro+"_H\n#include \"vx_support_value_base.h\"\n"
            "#ifdef __SDCC_z80\nstatic "+type+" vx_value_"+name+"("+type+" a, "+type+" b) __sdcccall(1) VX_VALUE_LOCATION {\n"
            "    "+type+" result=0;\n"
            "    while(b) {\n        if(b&1) result=("+type+")(result+a);\n"
            "        b>>=1; a=("+type+")(a<<1);\n    }\n    return result;\n}\n"
            "#define VX_VALUE_"+macro+"(a,b) vx_value_"+name+"(("+type+")(a),("+type+")(b))\n"
            "#else\n#define VX_VALUE_"+macro+"(a,b) (("+type+")((uint32_t)(a)*(uint32_t)(b)))\n#endif\n#endif\n";
        umbrella+="#include \""+file+"\"\n";
    }
    files["vx_support_value_add8wide.h"]=R"C(/* Read two stable byte lvalues before widening their sum. */
#ifndef VX_VALUE_ADD8WIDE_H
#define VX_VALUE_ADD8WIDE_H
#include "vx_support_value_base.h"
#ifdef __SDCC_z80
static uint16_t vx_value_add8wide(const volatile uint8_t *a, const volatile uint8_t *b) VX_VALUE_LOCATION {
    return (uint16_t)*a+(uint16_t)*b;
}
#define VX_VALUE_ADD8WIDE(a,b) vx_value_add8wide(&(a),&(b))
#else
#define VX_VALUE_ADD8WIDE(a,b) ((uint16_t)((uint16_t)(uint8_t)(a)+(uint16_t)(uint8_t)(b)))
#endif
#endif
)C";
    umbrella+="#include \"vx_support_value_add8wide.h\"\n";
    files["vx_value_runtime.h"]=umbrella+"#endif\n";return files;
}
}
