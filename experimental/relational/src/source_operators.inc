// Scalar operator relationships are shared by expressions and logical places.
// A shift's count is independently typed; it never widens the left operand.
void Session::compareValue(Ref type,Ref left,Ref right,const std::string& op,Ref result) {
    later({type},[this,left,right,op,result](const auto& types){
        if(aggregate(types[0])) {
            auto a=structure(types[0],[this](const Ref&){return fresh("comparison left leaf");});
            auto b=structure(types[0],[this](const Ref&){return fresh("comparison right leaf");});
            equation(left,a);equation(right,b);
            std::vector<Ref> fields,leftValues,rightValues;
            for(const auto& leaf:leaves(types[0],a)) {fields.push_back(leaf.type);leftValues.push_back(leaf.value);}
            for(const auto& leaf:leaves(types[0],b)) rightValues.push_back(leaf.value);
            compareSequence(fields,leftValues,rightValues,op,result);return;
        }
        if(auto format=scalarFormat(types[0]);format && format->bits>32) {
            later({left,right},[this,type=types[0],left,right,op,result](const auto& values){
                if(runtimeValue(values[0]) || runtimeValue(values[1])) {pending("wide runtime comparison requires a target representation");return;}
                if(type->name=="Float") request.goals.push_back(build("numeric_binary",{type,terms().make(op),left,right,result}));
                else request.goals.push_back(call("compare"+op,{left,right,result}));
            });return;
        }
        if(types[0]->name=="Float") {request.goals.push_back(build("numeric_binary",{types[0],terms().make(op),left,right,result}));return;}
        if(scalarFormat(types[0]) && scalarSigned(types[0])) {request.goals.push_back(call("signed_compare"+op,{types[0]->arguments[0],left,right,result}));return;}
        if(types[0]->name!="String") {request.goals.push_back(call("compare"+op,{left,right,result}));return;}
        later({left,right},[this,op,result](const auto& values){
            if(values[0]->name!="Bytes" || values[1]->name!="Bytes") {conflict();return;}
            const auto& a=values[0]->arguments;const auto& b=values[1]->arguments;
            int order=0;
            for(std::size_t i=0;i<std::min(a.size(),b.size());++i) if(a[i]->number!=b[i]->number) {order=a[i]->number<b[i]->number ? -1 : 1;break;}
            if(!order) order=a.size()<b.size() ? -1 : a.size()>b.size() ? 1 : 0;
            bool answer=op=="==" ? order==0 : op=="!=" ? order!=0 : op=="<" ? order<0 : op=="<=" ? order<=0 : op==">" ? order>0 : order>=0;
            equation(result,number(answer ? 1 : 0));
        });
    });
}

// Prefix order is a relation over the converted element interfaces. Empty
// arrays still constrain their element families; different outer lengths do
// not force equal storage shapes. NaN equality is false, so an unordered
// element stops lexical ordering while inequality remains true.
void Session::compareSequence(const std::vector<Ref>& types,const std::vector<Ref>& left,const std::vector<Ref>& right,const std::string& op,Ref result) {
    auto count=std::min(left.size(),right.size());
    if(op=="==" || op=="!=") {
        Ref same=number(left.size()==right.size() ? 1 : 0);
        for(std::size_t i=0;i<count;++i) {
            auto equalItem=fresh("equal sequence item");compareValue(types[i],left[i],right[i],"==",equalItem);
            same=booleanAnd(same,equalItem);
        }
        equation(result,op=="==" ? same : booleanNot(same));return;
    }
    bool less=op=="<" || op=="<=";
    Ref answer=number(op=="<" ? left.size()<right.size() : op=="<=" ? left.size()<=right.size() : op==">" ? left.size()>right.size() : left.size()>=right.size());
    for(std::size_t i=count;i>0;--i) {
        auto equalItem=fresh("equal lexical item"),ordered=fresh("ordered lexical item");
        compareValue(types[i-1],left[i-1],right[i-1],"==",equalItem);
        compareValue(types[i-1],left[i-1],right[i-1],less ? "<" : ">",ordered);
        auto tail=booleanAnd(equalItem,answer);
        answer=booleanNot(booleanAnd(booleanNot(ordered),booleanNot(tail)));
    }
    equation(result,answer);
}
void Session::compareOperands(Ref leftType,Ref left,Ref rightType,Ref right,const std::string& op,Ref result) {
    later({leftType,rightType},[this,left,right,op,result](const auto& types){
        auto common=fresh("comparison interface"),a=fresh("converted comparison left"),b=fresh("converted comparison right");
        if(types[0]->name=="Array" && types[1]->name=="Array") {
            commonType(types[0]->arguments[0],types[1]->arguments[0],common);
            conversion(known(types[0],left),terms().make("Array",{common,types[0]->arguments[1]}),left,a);
            conversion(known(types[1],right),terms().make("Array",{common,types[1]->arguments[1]}),right,b);
            std::vector<Ref> av,bv,fields;
            for(std::size_t i=0;i<arrayCount(types[0]);++i) {av.push_back(fresh("left sequence item"));fields.push_back(common);}
            for(std::size_t i=0;i<arrayCount(types[1]);++i) bv.push_back(fresh("right sequence item"));
            equation(a,terms().make("Values",av));equation(b,terms().make("Values",bv));
            compareSequence(fields,av,bv,op,result);return;
        }
        commonType(types[0],types[1],common);
        conversion(known(types[0],left),common,left,a);conversion(known(types[1],right),common,right,b);
        compareValue(common,a,b,op,result);
    });
}

void Session::binaryProperty(Id id,const std::string& op,Facet facet,Ref result) {
    const auto& cell=cells[id];bool logical=op=="&&" || op=="||",shift=op=="<<" || op==">>";
    bool comparison=op=="==" || op=="!=" || op=="<" || op=="<=" || op==">" || op==">=";
    if(facet!=Facet::Type && facet!=Facet::Value) {pending("scalar operator has no array shape",cell.site);return;}
    // Like builtin comparisons, this signature has a fixed result property.
    // Value/completion demand separately validates the operands actually used.
    // Asking #[0 && missing()] must not demand the unreachable operand.
    if((logical || comparison) && facet==Facet::Type) {equation(result,terms().make("Bool"));return;}
    if(comparison) {
        compareOperands(demand(cell.items[0],Facet::Type),demand(cell.items[0],Facet::Value),demand(cell.items[1],Facet::Type),demand(cell.items[1],Facet::Value),op,result);return;
    }
    auto a=demand(cell.items[0],Facet::Type);
    if(facet==Facet::Type) {
        auto b=demand(cell.items[1],Facet::Type);
        if(shift) {
            request.goals.push_back(call("bitwise_operand",{a}));request.goals.push_back(call("integer_operand",{b}));equation(result,a);
        } else {
            if(op=="&" || op=="|" || op=="^") {request.goals.push_back(call("bitwise_operand",{a}));request.goals.push_back(call("bitwise_operand",{b}));}
            request.goals.push_back(call("arithmetic_type",{a,b,result}));
        }
        return;
    }
    if(logical) {
        auto left=fresh("logical left operand");
        request.goals.push_back(call("implicit",{a,terms().make("Bool"),demand(cell.items[0],Facet::Value),left}));
        later({left},[this,id,op,result](const auto& args){
            const auto& value=args[0];bool isAnd=op=="&&";
            if(value->kind==Term::Number && (value->number!=0)!=isAnd) {equation(result,value);return;}
            Id rhs=cells[id].items[1];auto right=fresh("logical right operand");
            request.goals.push_back(call("implicit",{demand(rhs,Facet::Type),terms().make("Bool"),demand(rhs,Facet::Value),right}));
            equation(result,isAnd ? booleanAnd(value,right) : booleanNot(booleanAnd(booleanNot(value),booleanNot(right))));
        });return;
    }
    auto type=demand(id,Facet::Type),left=fresh("arithmetic left"),right=fresh("arithmetic right");
    request.goals.push_back(call("implicit",{a,type,demand(cell.items[0],Facet::Value),left}));
    if(shift) equation(right,demand(cell.items[1],Facet::Value));
    else request.goals.push_back(call("implicit",{demand(cell.items[1],Facet::Type),type,demand(cell.items[1],Facet::Value),right}));
    request.goals.push_back(call("arithmetic"+op,{type,left,right,result}));
}

Ref Session::arithmeticCompletion(Id id) {
    const auto& cell=cells[id];std::string op;
    if(cell.kind==Cell::Math) {
        if(cell.inputName!="isqrt" && cell.inputName!="fmod") return terms().make("Done");
        auto result=fresh("math completion");
        later({demand(id,Facet::Type),demand(id,Facet::Value)},[this,result](const auto& values){
            Ref code=terms().make("Done");
            for(const auto& leaf:leaves(values[0],values[1])) if(runtimeValue(leaf.value)) code=sequence(code,terms().make("Evaluate",{leaf.value}));
            equation(result,code);
        });return result;
    }
    if(cell.kind==Cell::Arithmetic) op=cell.inputName;
    else if(cell.site!=none && graph.nodes[cell.site].syntax==Syntax::Binary) op=graph.nodes[cell.site].text;
    if(cell.site!=none && graph.nodes[cell.site].syntax==Syntax::Cast) return castCompletion(id);
    if(op!="/" && op!="%" && op!="<<" && op!=">>") return terms().make("Done");
    auto result=fresh("arithmetic completion");
    later({demand(id,Facet::Value)},[this,result](const auto& values){
        equation(result,runtimeValue(values[0]) ? terms().make("Evaluate",{values[0]}) : terms().make("Done"));
    });return result;
}

void Session::flowLogical(Id id,Id before,const Flow& out) {
    auto left=flow(cells[id].items[0],before);
    later({left.state},[this,id,left,out](const auto& statesAt){
        continueFlow(statesAt[0]->number.template convert_to<Id>(),left.code,out,[this,id,left](Id at,const Flow& result){
            auto boolean=fresh("logical condition");
            request.goals.push_back(call("implicit",{demand(left.value,Facet::Type),terms().make("Bool"),demand(left.value,Facet::Value),boolean}));
            later({boolean},[this,id,at,result](const auto& values){
                const auto& condition=values[0];bool isAnd=graph.nodes[cells[id].site].text=="&&";
                auto skipped=Flow{known(terms().make("Bool"),number(isAnd ? 0 : 1)),number(at),terms().make("Done")};
                auto finish=[this,result](const Flow& value){
                    equation(cells[result.value].knownType,demand(value.value,Facet::Type));equation(cells[result.value].knownValue,demand(value.value,Facet::Value));
                    equation(result.state,value.state);equation(result.code,value.code);
                };
                if(condition->kind==Term::Number && (condition->number!=0)!=isAnd) {finish(skipped);return;}
                Flow evaluated{known(fresh("logical result type"),fresh("logical result value")),fresh("logical result state"),fresh("logical result code")};
                auto rightEntry=states[at];rightEntry.conditions.push_back(isAnd ? condition : booleanNot(condition));
                auto right=flow(cells[id].items[1],state(std::move(rightEntry)));
                later({right.state},[this,right,evaluated](const auto& ended){
                    continueFlow(ended[0]->number.template convert_to<Id>(),right.code,evaluated,[this,right](Id end,const Flow& converted){
                        equation(cells[converted.value].knownType,terms().make("Bool"));
                        request.goals.push_back(call("implicit",{demand(right.value,Facet::Type),terms().make("Bool"),demand(right.value,Facet::Value),cells[converted.value].knownValue}));
                        equation(converted.state,number(end));equation(converted.code,terms().make("Done"));
                    });
                });
                if(condition->kind==Term::Number) finish(evaluated);
                else flowMerge(condition,isAnd ? evaluated : skipped,isAnd ? skipped : evaluated,at,result);
            });
        });
    });
}

// A compound operation evaluates its location and snapshots the old operand
// once. Logical compounds also gate the RHS and the write itself.
void Session::compoundFlow(const std::string& op,Id previous,const std::vector<Ref>& path,Id before,Ref prefix,const Flow& out,const FlowAction& action) {
    continueFlow(before,prefix,out,[this,op,previous,path,action](Id at,const Flow& result){
        if(op!="&&=" && op!="||=") {action(at,result);return;}
        auto type=demand(previous,Facet::Type),value=demand(previous,Facet::Value);
        for(const auto& step:path) {
            auto part=fresh("logical compound operand"),partType=fresh("logical compound type");
            if(step->name=="Field") memberValue(type,value,step->arguments[0]->name,partType,part);
            else {equation(type,terms().make("Array",{partType,fresh("logical compound extent")}));indexValue(type,value,step->arguments[0],part);}
            type=partType;value=part;
        }
        auto boolean=fresh("logical compound condition");request.goals.push_back(call("implicit",{type,terms().make("Bool"),value,boolean}));
        later({boolean},[this,op,at,result,action](const auto& values){
            const auto& condition=values[0];bool isAnd=op=="&&=";
            if(condition->kind==Term::Number) {
                if((condition->number!=0)==isAnd) {action(at,result);return;}
                equation(cells[result.value].knownType,terms().make("Bool"));equation(cells[result.value].knownValue,condition);
                equation(result.state,number(at));equation(result.code,terms().make("Done"));return;
            }
            Flow changed{known(fresh("compound result type"),fresh("compound result value")),fresh("compound state"),fresh("compound code")};
            auto changedEntry=states[at];changedEntry.conditions.push_back(isAnd ? condition : booleanNot(condition));
            action(state(std::move(changedEntry)),changed);
            Flow skipped{known(terms().make("Bool"),number(isAnd ? 0 : 1)),number(at),terms().make("Done")};
            flowMerge(condition,isAnd ? changed : skipped,isAnd ? skipped : changed,at,result);
        });
    });
}
