#include "source_demand.hpp"
#include "source_numeric.hpp"

namespace vx::relational {
namespace {
Ref rangeStorageType(Terms& terms,const Ref& logical,const InterfaceDemand::Bound& range) {
    const auto format=scalarFormat(logical);
    if(!range || !format || format->kind!=ScalarFormat::IntegerKind || !format->bits || format->bits>32)
        return logical;
    // Only reduce storage. In particular, do not change signedness at the same
    // width just because a subset of that representation is currently used.
    const auto originalBytes=(format->bits+7)/8;
    for(unsigned bits:{8u,16u}) {
        if(bits/8>=originalBytes) break;
        bool sign=range->lower<0;
        Integer lower=sign ? -(Integer(1)<<(bits-1)) : Integer(0);
        Integer upper=(Integer(1)<<(bits-(sign ? 1 : 0)))-1;
        if(range->lower>=lower && range->upper<=upper)
            return terms.make(sign ? "SInt" : "UInt",{terms.number(bits)});
    }
    return logical;
}
Ref storageType(Terms& terms,const Ref& logical,const InterfaceDemand::Bound& range,unsigned bits) {
    auto storage=rangeStorageType(terms,logical,range);
    const auto format=scalarFormat(logical);
    if(bits && format && format->kind==ScalarFormat::IntegerKind && format->bits<=32 && bits<scalarBits(storage))
        // This is a residue, not a smaller range of the original value. Its
        // high bits are unobserved; using an unsigned carrier keeps that
        // distinction separate from range-proved sign extension.
        return terms.make("UInt",{terms.number(bits)});
    return storage;
}
}

void InterfaceDemand::planStorage() {
    for(auto& [id,fn]:functions_) {
        (void)id;
        for(auto [types,ranges,storage,bits]:{
            std::tuple{&fn.argumentTypes,&fn.argumentRanges,&fn.argumentStorageTypes,&fn.argumentBits},
            std::tuple{&fn.captureTypes,&fn.captureRanges,&fn.captureStorageTypes,&fn.captureBits},
            std::tuple{&fn.resultTypes,&fn.resultRanges,&fn.resultStorageTypes,&fn.resultBits}})
            for(std::size_t i=0;i<types->size();++i)
                storage->push_back(storageType(terms_,types->at(i),ranges->at(i),bits->at(i)));
    }
    for(auto& [id,loop]:loops_) {
        std::size_t at=0;
        for(const auto& slot:loop.slots) for(std::size_t i=0;i<slot.count;++i,++at)
            loop.storageTypes.push_back(storageType(terms_,loop.trace->arguments[10]->arguments.at(at),loopRanges_.at({id,slot.root,i}),loop.bits.at(at)));
    }
}

Ref InterfaceDemand::narrowTransport(Ref value,const Ref& logical,const Ref& storage) {
    const auto from=scalarFormat(logical),to=scalarFormat(storage);
    if(from && to && from->kind==ScalarFormat::IntegerKind && to->kind==ScalarFormat::IntegerKind && to->bits<from->bits) {
        std::map<const Term*,Ref> converted;
        std::map<const Term*,bool> arithmetic;
        auto benefits=[&](auto&& self,const Ref& v)->bool {
            if(auto found=arithmetic.find(v.get());found!=arithmetic.end()) return found->second;
            bool result=v->name=="Arithmetic" || v->name=="SignedArithmetic";
            if(v->name=="Cast" || v->name=="SignedCast") result=self(self,v->arguments[1]);
            else if(v->name=="Select") result=self(self,v->arguments[1]) || self(self,v->arguments[2]);
            arithmetic.emplace(v.get(),result);return result;
        };
        auto convert=[&](auto&& self,const Ref& v)->Ref {
            if(auto found=converted.find(v.get());found!=converted.end()) return found->second;
            Ref result;
            // Keep the condition and each arm at its original evaluation
            // site. Each branch can then select its own narrow arithmetic
            // interval without hoisting a snapshot from an inactive path.
            // A selection of already materialized values has no arithmetic
            // to narrow. Retain its single outer conversion: distributing
            // it needlessly widens SDCC's conditional temporaries.
            if(v->name=="Select" && benefits(benefits,v)) result=terms_.make("Select",{v->arguments[0],self(self,v->arguments[1]),self(self,v->arguments[2])});
            else result=terms_.make(to->sign ? "SignedCast" : "Cast",{terms_.number(to->bits),v});
            converted.emplace(v.get(),result);return result;
        };
        return convert(convert,value);
    }
    return value;
}

Ref InterfaceDemand::transportedResult(Id function,std::size_t leaf) {
    const auto& fn=functions_.at(function);
    auto value=rewrite(fn.results.at(leaf),function);
    return fn.slots.contains(leaf) ? narrowTransport(std::move(value),fn.resultTypes.at(leaf),fn.resultStorageTypes.at(leaf)) : value;
}

InterfaceDemand::Bound InterfaceDemand::transportRange(const Ref& value,Owner owner) const {
    if(!closed_) throw std::logic_error("unclosed transport range");
    const auto& a=value->arguments;const auto& op=value->name;
    Ref storage;Bound bound;
    if((op=="FrameArg" || op=="CaptureArg") && owner) {
        const auto& fn=functions_.at(*owner);const auto index=a.at(0)->number.convert_to<std::size_t>();
        storage=(op=="FrameArg" ? fn.argumentStorageTypes : fn.captureStorageTypes).at(index);
        bound=(op=="FrameArg" ? fn.argumentRanges : fn.captureRanges).at(index);
    } else if(op=="CResult") {
        const auto site=a.at(0)->number.convert_to<Id>();
        const auto call=calls_.find(site);
        if(call==calls_.end() || call->second.owner!=owner) return {};
        const auto& fn=functions_.at(call->second.callee);
        const auto physical=a.at(1)->number.convert_to<std::size_t>();
        for(const auto& [logical,slot]:fn.slots) if(slot==physical) {
            storage=fn.resultStorageTypes.at(logical);
            bound=callRanges_.at(site).at(logical);break;
        }
    } else if(op=="LoopSlotValue") {
        const auto id=a.at(0)->number.convert_to<Id>(),root=a.at(1)->number.convert_to<Id>();
        const auto& loop=loops_.at(id);
        if(loop.owner!=owner) return {};
        const auto physical=a.at(2)->number.convert_to<std::size_t>();
        for(const auto& slot:loop.slots) if(slot.root==root)
            for(const auto& [logical,index]:slot.leaves) if(index==physical) {
                storage=carriedStorage(id,root,logical);bound=loopRanges_.at({id,root,logical});break;
            }
    } else if(op=="LoopReady" || op=="LoopFlag") return Range{0,1};
    if(!storage || !bound) return {};
    const auto f=scalarFormat(storage);
    if(!f || !f->bits || f->bits>32 || (f->kind!=ScalarFormat::IntegerKind && f->kind!=ScalarFormat::Bool)) return {};
    return convertedIntegerRange(bound,f->bits,f->sign);
}

Ref InterfaceDemand::carriedStorage(Id id,Id root,std::size_t leaf) const {
    const auto& loop=loops_.at(id);std::size_t at=0;
    for(const auto& slot:loop.slots) {
        if(slot.root==root && leaf<slot.count) return loop.storageTypes.at(at+leaf);
        at+=slot.count;
    }
    throw std::logic_error("missing carried storage leaf");
}

// Alternate C projections retain logical call/loop numbering and their own
// public ABI, but consume the same private carried-state layout. Do not expose
// a compact ordinary-C result index to a projector with another result ABI.
Ref InterfaceDemand::facts(const Ref& source,Owner owner) {
    if(!closed_) throw std::logic_error("unclosed value facts");
    auto& cache=storageFacts_[owner];
    if(auto found=cache.find(source);found!=cache.end()) return found->second;
    auto value=normalize(source,owner);auto result=value;
    if(value->name=="Invoke" && discardedCalls_.contains(value->arguments.at(0)->number.convert_to<Id>())) {
        result=terms_.make("Done");cache.emplace(source,result);return result;
    }
    if(!value->arguments.empty() && value->name!="Record" && value->name!="Array" && value->name!="Fields" && value->name!="Captures") {
        auto args=value->arguments;
        auto first=value->name=="RecordValue" || value->name=="After" ? 1u : 0u;
        for(std::size_t i=first;i<args.size();++i) args[i]=facts(args[i],owner);
        if(value->name=="Loop") {
            const auto& loop=loops_.at(args[0]->number.convert_to<Id>());
            for(unsigned position:{1u,4u,6u}) {
                std::size_t at=0;
                auto convert=[&](auto&& self,const Ref& v)->Ref {
                    if(v->name=="Values" || v->name=="RecordValue") {
                        auto children=v->arguments;
                        for(std::size_t i=v->name=="RecordValue" ? 1u : 0u;i<children.size();++i) children[i]=self(self,children[i]);
                        return terms_.make(v->name,children);
                    }
                    auto index=at++;
                    return narrowTransport(v,loop.trace->arguments[10]->arguments.at(index),loop.storageTypes.at(index));
                };
                args[position]=convert(convert,args[position]);
            }
            args[10]=terms_.make("Types",loop.storageTypes);
        }
        else if(value->name=="LoopSlotValue")
            args[3]=carriedStorage(args[0]->number.convert_to<Id>(),args[1]->number.convert_to<Id>(),args[2]->number.convert_to<std::size_t>());
        if(args!=value->arguments) result=terms_.make(value->name,args);
    }
    cache.emplace(source,result);return result;
}
}
