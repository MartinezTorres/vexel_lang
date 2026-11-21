// Ordinary C expressions share the typed DAG scheduler with value/region
// bindings. Scratch lives for one full C expression or one ordered total result
// group. Separate arguments need disjoint lanes: C does not sequence their
// evaluation relative to each other.
struct CCalculations {
    Terms& terms;
    std::map<std::string,Ref> inputs;
    std::map<std::string,IntegerBound> inputRanges;
    std::function<IntegerBound(const Ref&)> transportRange;
    // Rewrites can return temporary roots. Retain each key's graph so a later
    // allocation cannot reuse its address for a different calculation.
    std::map<const Term*,std::pair<Ref,CalculationProjection>> projections;
    std::map<std::vector<const Term*>,std::pair<std::vector<Ref>,CalculationProjection>> resultPlans;
    std::map<std::string,std::size_t> next,maximum;
    std::set<std::string> helpers;
    std::size_t intervals=0,retained=0,scratchReuses=0,narrowedValues=0,narrowedOperations=0;
    bool reuseScratch=true;
    bool shareResults=true;
    bool shareDivisions=false;
    bool unrollDivisions=true;
    bool rotationHelpers=false;
    std::size_t sharedDivisions=0;
    std::size_t resultGroups=0;
    unsigned linearTerms=16;
    std::size_t linearCombinations=0;
    bool sealed=false;
    explicit CCalculations(Terms& arena):terms(arena) {}
    void statement() {next.clear();}
    std::string raw(const Ref& value);
    std::optional<CalculationInput> input(const Ref& value) {
        const auto& op=value->name;const auto& a=value->arguments;
        Ref type;IntegerBound range=transportRange ? transportRange(value) : IntegerBound{};
        if(op=="Input" || op=="FrameArg" || op=="CaptureArg") {
            auto found=inputs.find(op+":"+show(a.at(0)));
            if(found==inputs.end()) return {};
            type=found->second;
            if(auto bounds=inputRanges.find(op+":"+show(a.at(0)));bounds!=inputRanges.end()) range=bounds->second;
        } else if(op=="LoopSlotValue") type=a.at(3);
        else if(op=="LoopValue" || op=="CResult") type=a.at(2);
        else if(op=="LoopReady" || op=="LoopFlag") type=terms.make("UInt",{terms.number(8)});
        else if(op=="LoopIndex" || op=="LoopReturned" || op=="CallValue") type=terms.make("UInt",{terms.number(32)});
        else return {};
        return CalculationInput{type,raw(value),op!="Input" && op!="FrameArg" && op!="CaptureArg",range};
    }
    std::vector<std::string> render(const CalculationProjection& plan) {
        std::map<std::string,std::string> names;
        for(const auto& slot:plan.scratch) {
            auto index=next[slot.type]++;
            if(sealed && index>=maximum[slot.type])
                throw std::logic_error("calculation scratch exceeds its closed frame plan");
            maximum[slot.type]=std::max(maximum[slot.type],index+1);
            names.emplace(slot.name,"vx_calc_"+slot.type+"_"+std::to_string(index));
        }
        // Replace identifiers, not substrings (vxcse_1 also prefixes _10).
        std::vector<std::string> results;
        for(const auto& expression:plan.expressions) {
            std::string result;
            for(std::size_t i=0;i<expression.size();) {
                auto start=i;auto c=expression[i];
                if((c>='a' && c<='z') || (c>='A' && c<='Z') || c=='_') {
                    do {++i;if(i==expression.size()) break;c=expression[i];}
                    while((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_');
                    auto token=expression.substr(start,i-start);
                    auto found=names.find(token);result+=found==names.end() ? token : found->second;
                } else {result+=c;++i;}
            }
            results.push_back(std::move(result));
        }
        if(sealed) for(const auto& helper:plan.helpers) if(!helpers.contains(helper))
            throw std::logic_error("calculation needs an unplanned runtime helper");
        helpers.insert(plan.helpers.begin(),plan.helpers.end());
        ++intervals;retained+=plan.sharedValues;scratchReuses+=plan.scratchReuses;narrowedValues+=plan.narrowedValues;
        narrowedOperations+=plan.narrowedOperations;
        linearCombinations+=plan.linearCombinations;sharedDivisions+=plan.sharedDivisions;return results;
    }
    std::string expression(const Ref& value) {
        auto [it,inserted]=projections.try_emplace(value.get(),value,CalculationProjection{});
        if(inserted) it->second.second=projectCalculations({value},[&](const Ref& leaf){return input(leaf);},reuseScratch,linearTerms,shareDivisions,unrollDivisions,rotationHelpers);
        const auto& plan=it->second.second;
        return plan.expressions.empty() ? raw(value) : render(plan).at(0);
    }
    std::vector<std::string> results(const std::vector<Ref>& values) {
        if(shareResults && values.size()>1 && values.size()<=64) {
            std::vector<const Term*> key;for(const auto& value:values) key.push_back(value.get());
            auto [it,inserted]=resultPlans.try_emplace(std::move(key),values,CalculationProjection{});
            if(inserted) it->second.second=projectCalculations(values,[&](const Ref& leaf){return input(leaf);},reuseScratch,linearTerms,shareDivisions,unrollDivisions,rotationHelpers);
            if(!it->second.second.expressions.empty()) {
                statement();++resultGroups;return render(it->second.second);
            }
        }
        std::vector<std::string> result;
        for(const auto& value:values) {statement();result.push_back(expression(value));}
        return result;
    }
    std::size_t bytes() const {
        std::size_t result=0;
        for(const auto& [type,count]:maximum) result+=count*(type.find("32")!=std::string::npos ? 4u : type.find("16")!=std::string::npos ? 2u : 1u);
        return result;
    }
    void declarations(std::ostream& out,bool fixed) const {
        for(const auto& [type,count]:maximum) for(std::size_t i=0;i<count;++i)
            out<<"    "<<(fixed ? "static " : "")<<"VX_STABLE "<<type<<" vx_calc_"<<type<<'_'<<i<<";\n";
    }
};
