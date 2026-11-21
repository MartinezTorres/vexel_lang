// Physical cuts in the already closed execution graph. Each cut retains the
// ordinary C signature. Resident wrappers own the dynamic mapper state, so a
// call (including recursion or a numeric helper) may replace window B and then
// restore the suspended caller's segment before returning to it.
struct BankProjection {
    struct Part {
        std::size_t begin,end;
        std::string name,module,signature,arguments,body,framePolicy;
        std::string extraHeader={},owner={},role="whole";
        unsigned operations=0;
        std::string support={};
    };
    std::vector<Part> parts;
    std::string calculationSupport,residentCalculationSupport;
    void cut(const std::ostringstream& code,std::size_t begin,const std::string& name,
             const std::string& module,const std::string& arguments,const std::string& framePolicy="automatic") {
        auto text=code.str();auto signatureEnd=text.find(" {\n",begin);
        if(signatureEnd==std::string::npos) throw std::logic_error("missing generated function boundary");
        parts.push_back({begin,text.size(),name,module,text.substr(begin,signatureEnd-begin),arguments,text.substr(signatureEnd),framePolicy});
    }
    void emit(SourceResult& answer,std::ostream& report,unsigned callDepth,unsigned chunkOperations,
              const std::map<std::string,unsigned>& scopedOperations) const {
        const auto& source=answer.implementation;std::ostringstream resident;std::size_t copied=0;
        auto emitted=parts;
        for(const auto& [name,limit]:scopedOperations) {
            (void)limit;
            if(std::none_of(parts.begin(),parts.end(),[&](const auto& part){return part.name==name;}))
                throw std::invalid_argument("bank partition scope has no emitted function: "+name);
        }
        for(std::size_t i=0;i<parts.size();++i) {
            const auto& original=parts[i];
            auto scope=scopedOperations.find(original.name);
            auto operations=scope==scopedOperations.end() ? chunkOperations : std::min(chunkOperations,scope->second);
            emitted[i].owner=original.name;emitted[i].operations=operations;
            auto split=splitBankFunction(original.name,original.signature,original.body,operations,original.framePolicy=="static-exclusive");
            if(split.parts.empty()) continue;
            auto header="vxf_"+original.name+".h";
            answer.files.emplace(header,split.header);emitted[i].extraHeader=header;emitted[i].body=split.body;
            emitted[i].role="dispatcher";
            resident<<"#include \""<<header<<"\"\n";
            for(const auto& piece:split.parts)
                emitted.push_back({source.size(),source.size(),piece.name,piece.module,piece.signature,"frame",piece.body,"automatic",header,original.name,"continuation",operations,original.support});
        }
        if(callDepth) resident<<"#include \"vexel.h\"\nstatic uint16_t vx_call_depth;\n";
        report<<",\"bank_modules\":[";
        for(std::size_t i=0;i<emitted.size();++i) {
            const auto& part=emitted[i];resident<<source.substr(copied,part.begin-copied);copied=part.end;
            auto implementation=part.signature;auto nameAt=implementation.find(part.name+"(");
            if(nameAt==std::string::npos) throw std::logic_error("missing generated function name");
            implementation.replace(nameAt,part.name.size(),"vxi_"+part.name);
            auto qualifier=implementation.find(" VX_RESIDENT");
            if(qualifier==std::string::npos) throw std::logic_error("missing generated residency qualifier");
            implementation.erase(qualifier,std::string(" VX_RESIDENT").size());
            answer.files.emplace(part.module+".c","/* Editable NEO-8 window B module. Keep the generated ABI. */\n#include \"vexel.h\"\n"+
                part.support+(part.extraHeader.empty() ? "" : "#include \""+part.extraHeader+"\"\n")+implementation+part.body);
            bool returns=!part.signature.starts_with("void ");
            resident<<"extern "<<implementation<<";\n#ifdef __SDCC\nextern const uint16_t __ML_SEGMENT_B_"<<part.module<<";\n#endif\n"
                    <<part.signature<<" {\n    uint16_t previous;\n";
            if(returns) resident<<"    uint32_t result;\n";
            if(callDepth) resident<<"    if (vx_call_depth >= "<<callDepth<<"u) vx_trap(23);\n    ++vx_call_depth;\n";
            resident<<"#ifdef __SDCC\n    previous = vx_bank_b((uint16_t)&__ML_SEGMENT_B_"<<part.module<<");\n"
                    <<"#else\n    previous = vx_bank_b(0);\n#endif\n    "<<(returns ? "result = " : "")<<"vxi_"<<part.name<<'('<<part.arguments<<");\n"
                    <<"    (void)vx_bank_b(previous);\n";
            if(callDepth) resident<<"    --vx_call_depth;\n";
            if(returns) resident<<"    return result;\n";
            resident<<"}\n";
            if(i) report<<',';
            report<<"{\"name\":"<<vx::quote(part.name)<<",\"module\":"<<vx::quote(part.module)
                  <<",\"window\":\"B\",\"frame_policy\":"<<vx::quote(part.framePolicy)
                  <<",\"partition_owner\":"<<vx::quote(part.owner)<<",\"partition_role\":"<<vx::quote(part.role)
                  <<",\"chunk_operations\":"<<part.operations<<"}";
        }
        resident<<source.substr(copied);
        auto text=resident.str();
        if(!calculationSupport.empty()) {
            auto at=text.find(calculationSupport);
            if(at==std::string::npos) throw std::logic_error("missing calculation support boundary");
            text.replace(at,calculationSupport.size(),residentCalculationSupport);
        }
        answer.files.emplace("vx_banked.c",std::move(text));report<<']';
    }
};
