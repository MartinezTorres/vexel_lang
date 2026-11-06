// Portable value representations are language facts. Native C storage layout
// is deliberately absent: canonical bytes have no alignment padding.
bool byteArray(const Ref& type) {
    return type->name=="Array" && type->arguments[0]->name=="UInt" && type->arguments[0]->arguments[0]->number==8;
}
bool bitArray(const Ref& type) {return type->name=="Array" && type->arguments[0]->name=="Bool";}
std::optional<std::size_t> canonicalBytes(const Ref& type) {
    if(type->name=="NoValue") return {};
    std::size_t count=0;
    if(type->name=="Array") {
        auto element=canonicalBytes(type->arguments[0]);if(!element) return {};
        count=*element*arrayCount(type);
    } else if(type->name=="Record") {
        for(const auto& field:type->arguments[1]->arguments) {
            auto size=canonicalBytes(field->arguments[0]);if(!size) return {};
            count+=*size;
        }
    } else {
        auto format=scalarFormat(type);if(!format || !format->bits) return {};
        count=(format->bits+7)/8;
    }
    if(count>10000) throw ConstructionLimit("canonical byte representation exceeds construction budget");
    return count;
}
bool emptyCastCompatible(const Ref& from,const Ref& to) {
    // Exact is the empty literal's unconcretized element carrier. There is
    // no supplied element whose value or shape could reject concretization.
    if(from->name=="Exact") return true;
    if(identical(from,to) || (scalarFormat(from) && scalarFormat(to))) return true;
    if((bitArray(from) && to->name=="UInt") || (from->name=="UInt" && bitArray(to)))
        return arrayCount(bitArray(from) ? from : to)==scalarBits(from->name=="UInt" ? from : to);
    if(byteArray(from)!=byteArray(to)) {
        auto a=canonicalBytes(from),b=canonicalBytes(to);
        if(a && b) return *a==*b;
    }
    if(from->name=="Array" && to->name=="Array") {
        return identical(from->arguments[1],to->arguments[1]) && emptyCastCompatible(from->arguments[0],to->arguments[0]);
    }
    if(from->name!="Record" || to->name!="Record" || !identical(from->arguments[0],to->arguments[0]) || !identical(from->arguments[2],to->arguments[2])) return false;
    const auto& a=from->arguments[1]->arguments;const auto& b=to->arguments[1]->arguments;
    if(a.size()!=b.size()) return false;
    for(std::size_t i=0;i<a.size();++i) if(a[i]->name!=b[i]->name || !emptyCastCompatible(a[i]->arguments[0],b[i]->arguments[0])) return false;
    return true;
}
void Session::castValue(Ref source,Ref target,Ref input,Ref result) {
    later({source,target},[this,input,result](const auto& types){
        const auto& from=types[0];const auto& to=types[1];
        if(identical(from,to)) {equation(result,input);return;}
        bool packed=(bitArray(from) && to->name=="UInt") || (from->name=="UInt" && bitArray(to));
        bool byteCandidate=!packed && byteArray(from)!=byteArray(to);
        auto fromBytes=byteCandidate ? canonicalBytes(from) : std::optional<std::size_t>{};
        auto toBytes=byteCandidate ? canonicalBytes(to) : std::optional<std::size_t>{};
        bool bytes=byteArray(from)!=byteArray(to) && fromBytes && toBytes;
        if(packed || bytes) {
            if(packed) {
                auto count=arrayCount(bitArray(from) ? from : to);
                auto bits=scalarBits(from->name=="UInt" ? from : to);
                if(count!=bits) {conflict();return;}
            } else if(*fromBytes!=*toBytes) {conflict();return;}
            later({input},[this,from,to,packed,result](const auto& values){
                const auto& value=values[0];std::vector<Ref> encoded;
                unsigned unit=packed ? 1u : 8u;
                for(const auto& leaf:leaves(from,value)) {
                    auto f=scalarFormat(leaf.type).value();
                    auto count=packed && bitArray(from) ? 1u : (f.bits+unit-1)/unit;
                    if(leaf.value->kind!=Term::Number && f.bits>32) {pending("wide runtime representation requires a target realization");return;}
                    for(unsigned i=0;i<count;++i) {
                        unsigned used=std::min(unit,f.bits-i*unit);Integer mask=(Integer(1)<<used)-1;
                        if(leaf.value->kind==Term::Number) encoded.push_back(number((leaf.value->number>>(i*unit))&mask));
                        else if(i==0 && leaf.value->name=="Runtime" && leaf.value->arguments[1]->number>=0 && leaf.value->arguments[2]->number<=mask) encoded.push_back(leaf.value);
                        else encoded.push_back(terms().make("Runtime",{terms().make("RepresentationPart",{leaf.value,number(i*unit),number(mask)}),number(0),number(mask)}));
                    }
                }
                std::size_t cursor=0;
                auto decoded=structure(to,[&](const Ref& type) {
                    auto f=scalarFormat(type).value();auto count=packed && bitArray(to) ? 1u : (f.bits+unit-1)/unit;
                    std::vector<Ref> parts(encoded.begin()+static_cast<std::ptrdiff_t>(cursor),encoded.begin()+static_cast<std::ptrdiff_t>(cursor+count));cursor+=count;
                    bool constant=std::all_of(parts.begin(),parts.end(),[](const Ref& part){return part->kind==Term::Number;});
                    if(constant) {
                        Integer raw=0;for(std::size_t i=0;i<parts.size();++i) raw|=parts[i]->number<<(i*unit);
                        if(f.kind==ScalarFormat::Bool && raw>1) {conflict();return fresh("invalid Boolean representation");}
                        raw&=(Integer(1)<<f.bits)-1;
                        if(f.sign && raw>=(Integer(1)<<(f.bits-1))) raw-=Integer(1)<<f.bits;
                        return number(raw);
                    }
                    if(f.bits>32) {pending("wide runtime decoding requires a target realization");return fresh("wide representation");}
                    // A one-part unsigned decode is already its source value
                    // when the proven interval fits. This also proves that a
                    // packed single bit cannot fail Boolean validation.
                    if(parts.size()==1 && !f.sign && parts[0]->name=="Runtime" && parts[0]->arguments[1]->number>=0 && parts[0]->arguments[2]->number<(Integer(1)<<f.bits)) return parts[0];
                    // Reassembling all consecutive slices of one bounded
                    // unsigned value preserves that value. This is a term
                    // identity backed by the source interval, not a C peephole.
                    Ref original;bool consecutive=!f.sign;
                    for(std::size_t i=0;i<parts.size() && consecutive;++i) {
                        const auto& part=parts[i];
                        if(part->name!="Runtime" || part->arguments[0]->name!="RepresentationPart") {consecutive=false;break;}
                        const auto& slice=part->arguments[0]->arguments;
                        if(!original) original=slice[0];
                        consecutive=identical(original,slice[0]) && slice[1]->number==i*unit && slice[2]->number==((Integer(1)<<std::min(unit,f.bits-static_cast<unsigned>(i)*unit))-1);
                    }
                    if(consecutive && original && original->name=="Runtime" && original->arguments[1]->number>=0 && original->arguments[2]->number<(Integer(1)<<f.bits)) return original;
                    auto expression=terms().make("RepresentationJoin",{type,number(unit),terms().make("Parts",parts)});
                    return terms().make("Runtime",{expression,number(f.sign ? -(Integer(1)<<(f.bits-1)) : Integer(0)),number((Integer(1)<<(f.bits-(f.sign ? 1 : 0)))-1)});
                });
                if(cursor!=encoded.size()) throw std::logic_error("canonical representation size mismatch");
                equation(result,decoded);
            });return;
        }
        if(from->name=="Array" && to->name=="Array") {
            if(!arrayCount(from) && !emptyCastCompatible(from->arguments[0],to->arguments[0])) {conflict();return;}
            equation(from->arguments[1],to->arguments[1]);std::vector<Ref> inputs,convertedValues;
            for(std::size_t i=0;i<arrayCount(from);++i) {
                auto a=fresh("cast source element"),b=fresh("cast target element");inputs.push_back(a);convertedValues.push_back(b);
                castValue(from->arguments[0],to->arguments[0],a,b);
            }
            equation(input,terms().make("Values",inputs));equation(result,terms().make("Values",convertedValues));return;
        }
        if(from->name=="Record" && to->name=="Record") {
            equation(from->arguments[0],to->arguments[0]);equation(from->arguments[2],to->arguments[2]);
            const auto& a=from->arguments[1]->arguments;const auto& b=to->arguments[1]->arguments;
            if(a.size()!=b.size()) {conflict();return;}
            std::vector<Ref> inputs,convertedValues;
            for(std::size_t i=0;i<a.size();++i) {
                if(a[i]->name!=b[i]->name) {conflict();return;}
                auto av=fresh("cast source field"),bv=fresh("cast target field");inputs.push_back(av);convertedValues.push_back(bv);
                castValue(a[i]->arguments[0],b[i]->arguments[0],av,bv);
            }
            equation(input,terms().make("RecordValue",{from->arguments[0],terms().make("Values",inputs)}));
            equation(result,terms().make("RecordValue",{to->arguments[0],terms().make("Values",convertedValues)}));return;
        }
        request.goals.push_back(call("scalar_cast",{from,to,input,result}));
    });
}
Ref Session::castCompletion(Id id) {
    auto result=fresh("cast completion");
    later({demand(id,Facet::Type),demand(id,Facet::Value)},[this,result](const auto& args){
        Ref code=validateSchema(args[0],args[1]);
        std::function<bool(const Ref&)> traps=[&](const Ref& value) {
            if(value->name=="RepresentationJoin" && value->arguments[0]->name=="Bool") return true;
            if(value->name=="NumericCast" && value->arguments[0]->name=="Float" && value->arguments[1]->name!="Float" && value->arguments[1]->name!="Bool") return true;
            return std::any_of(value->arguments.begin(),value->arguments.end(),traps);
        };
        for(const auto& leaf:leaves(args[0],args[1])) if(runtimeValue(leaf.value) && traps(leaf.value)) code=sequence(code,terms().make("Evaluate",{leaf.value}));
        equation(result,code);
    });return result;
}
