void Session::projectBoundary(const Model& model,std::ostream& header,std::ostream& code) {
    std::vector<Ref> storage;
    auto validateScalar=[&](const Ref& type,const std::string& value) {
        unsigned bits=scalarBits(type);
        if(bits==32) return;
        if(scalarSigned(type))
            code<<"    if (vx_signed("<<value<<", 32) < "<<-(Integer(1)<<(bits-1))<<"L || vx_signed("<<value<<", 32) > "<<((Integer(1)<<(bits-1))-1)<<"L) vx_trap(16);\n";
        else code<<"    if ("<<value<<" > "<<((Integer(1)<<bits)-1)<<"UL) vx_trap(16);\n";
    };
    auto parameters=[](const Ref& type) {
        std::string text;std::size_t i=0;
        for(const auto& leaf:leaves(type)) {
            if(i) text+=", ";
            text+="uint8_t m"+std::to_string(i)+", "+cScalarType(leaf.type)+" v"+std::to_string(i);++i;
        }
        return text.empty() ? std::string("void") : text;
    };
    for(const auto& [key,artifact]:model.witnesses) {
        if(key.starts_with("external_")) {
            std::string arguments="uint32_t *result",actual="result";std::size_t i=0;
            for(const auto& type:artifact->arguments[1]->arguments)
                for(const auto& leaf:leaves(type)) {auto name="p"+std::to_string(i++);arguments+=", "+cScalarType(leaf.type)+" "+name;actual+=", "+name;}
            header<<"/* Sink ABI: caller-owned result leaves, then value argument leaves. */\nvoid vx_external_"<<artifact->arguments[0]->name<<'('<<arguments<<") VX_ABI;\n";
            header<<"void vx_invoke_external_"<<artifact->arguments[0]->name<<'('<<arguments<<") VX_ABI VX_RESIDENT;\n";
            code<<"void vx_invoke_external_"<<artifact->arguments[0]->name<<'('<<arguments<<") VX_ABI VX_RESIDENT {\n    vx_external_"<<artifact->arguments[0]->name<<'('<<actual<<");\n";
            i=0;for(const auto& leaf:leaves(artifact->arguments[2])) {
                validateScalar(leaf.type,"result["+std::to_string(i)+"]");
                ++i;
            }
            code<<"}\n";
        } else if(key.starts_with("storage_")) storage.push_back(artifact);
    }
    // Declare all helpers before emitting any initializer: dependency cycles
    // are represented by calls and guarded at runtime, not recursive C text.
    for(const auto& item:storage) {
        auto id=show(item->arguments[0]);const auto& type=item->arguments[2];
        header<<"void vx_load_"<<id<<"(uint32_t *out) VX_ABI VX_RESIDENT;\n"
              <<"void vx_store_"<<id<<'('<<parameters(type)<<") VX_ABI VX_RESIDENT;\n";
    }
    for(const auto& item:storage) {
        auto id=show(item->arguments[0]);const auto& type=item->arguments[2];
        auto flat=leaves(type);bool external=item->arguments[3]->name=="External",lazy=item->arguments[3]->name=="Lazy";
        const auto& declaration=graph.nodes[cells[item->arguments[0]->number.convert_to<Id>()].site];
        const auto mapped=mapperOffsets.find(item->arguments[0]->number.convert_to<Id>());
        unsigned mapperCursor=mapped==mapperOffsets.end() ? 0 : mapped->second;
        std::vector<unsigned> mapperLeaves;
        auto addressFact=model.witnesses.find("binding_"+id);
        auto binding=msxBinding(declaration,type,addressFact==model.witnesses.end() ? Ref{} : addressFact->second);
        const auto& space=binding.space;unsigned address=binding.address;
        std::vector<std::string> names;
        for(std::size_t i=0;i<flat.size();++i) {
            auto scalar=cScalarType(flat[i].type);
            auto name=external ? "vx_external_"+declaration.text+"_"+std::to_string(i) : "vxs_"+id+"_"+std::to_string(i);
            names.push_back(name);
            mapperLeaves.push_back(mapperCursor);mapperCursor+=nativeBytes(flat[i].type);
            if(mapped!=mapperOffsets.end()) continue;
            if(!external) code<<"static volatile "<<scalar<<' '<<name<<";\n";
            else if(space.empty()) header<<"extern volatile "<<scalar<<' '<<name<<";\n";
            else {
                auto bytes=nativeBytes(flat[i].type);
                if(address+bytes>65536u) throw Error(declaration.span,"MSX binding crosses the address space");
                header<<"#ifdef __SDCC\n";
                if(space=="port") header<<"__sfr __at("<<address<<") "<<name<<";\n";
                else header<<"#define "<<name<<" (*(volatile "<<scalar<<"*)"<<address<<"U)\n";
                header<<"#else\nextern volatile "<<scalar<<' '<<name<<";\n#endif\n";
                code<<"#ifndef __SDCC\nvolatile "<<scalar<<' '<<name<<";\n#endif\n";
                address+=bytes;
            }
        }
        auto store=[&](std::size_t i,const std::string& value,const std::string& pad) {
            if(mapped==mapperOffsets.end()) code<<pad<<names[i]<<" = ("<<cScalarType(flat[i].type)<<")("<<value<<");\n";
            else for(unsigned byte=0;byte<nativeBytes(flat[i].type);++byte)
                code<<pad<<"vx_mapper_write("<<mapperLeaves[i]+byte<<"UL, (uint8_t)((uint32_t)("<<value<<") >> "<<8*byte<<"));\n";
        };
        if(!external) {
            code<<"static uint8_t vxs_ready_"<<id<<"; /* 0 absent, 1 ready, 2 initializing */\n";
            if(lazy) {
                code<<"static void vx_ensure_"<<id<<"(void) VX_ABI VX_RESIDENT {\n";
                cTemporaries(code,item->arguments[4]);
                code<<"    if (vxs_ready_"<<id<<" == 1) return;\n    if (vxs_ready_"<<id<<" == 2) vx_trap(18);\n    vxs_ready_"<<id<<" = 2;\n";
                cExecution(code,item->arguments[4]);auto initial=leaves(type,item->arguments[5]);
                for(std::size_t i=0;i<flat.size();++i) store(i,cExpression(initial[i].value),"    ");
                code<<"    vxs_ready_"<<id<<" = 1;\n}\n";
            }
        }
        code<<"void vx_load_"<<id<<"(uint32_t *out) VX_ABI VX_RESIDENT {\n";
        if(lazy) code<<"    vx_ensure_"<<id<<"();\n";
        else if(!external) code<<"    if (!vxs_ready_"<<id<<") vx_trap(17);\n";
        for(std::size_t i=0;i<flat.size();++i) {
            code<<"    out["<<i<<"] = ";
            if(mapped==mapperOffsets.end()) code<<names[i];
            else {
                code<<"("<<cScalarType(flat[i].type)<<")(";
                for(unsigned byte=0;byte<nativeBytes(flat[i].type);++byte) {
                    if(byte) code<<" | ";
                    code<<"((uint32_t)vx_mapper_read("<<mapperLeaves[i]+byte<<"UL) << "<<8*byte<<")";
                }
                code<<")";
            }
            code<<";\n";
            if(external) validateScalar(flat[i].type,"out["+std::to_string(i)+"]");
        }
        code<<"}\nvoid vx_store_"<<id<<'('<<parameters(type)<<") VX_ABI VX_RESIDENT {\n";
        if(lazy) code<<"    vx_ensure_"<<id<<"();\n";
        for(std::size_t i=0;i<flat.size();++i) {
            code<<"    if (m"<<i<<") {\n";store(i,"v"+std::to_string(i),"        ");code<<"    }\n";
        }
        if(!external) code<<"    vxs_ready_"<<id<<" = 1;\n";
        code<<"}\n";
    }
}
