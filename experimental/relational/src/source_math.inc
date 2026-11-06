// Only parser-marked bundled declarations reach this adapter. An override is
// an ordinary module; spelling a function "sqrt" grants no special behavior.
void Session::mathProperty(Id id,Facet facet,Ref result) {
    if(facet==Facet::Length) {
        equation(demand(id,Facet::Shape),terms().make("Array",{fresh("math inner dimensions"),result}));return;
    }
    if(facet!=Facet::Type && facet!=Facet::Value && facet!=Facet::Length && facet!=Facet::Element) {conflict();return;}
    std::vector<Ref> types;for(Id argument:cells[id].items) types.push_back(demand(argument,Facet::Type));
    later(types,[this,id,facet,result](const auto& supplied){
        const auto name=cells[id].inputName;auto op=mathOperation(name);
        bool binary=op==5 || op==6 || op==7 || op==15 || op==16;
        if(supplied.size()!=(binary ? 2u : 1u)) {conflict();return;}
        auto shape=broadcastShape(supplied);if(!shape) {conflict();return;}
        if(!shape->empty()) {
            if(facet==Facet::Length) {equation(result,number((*shape)[0]));return;}
            auto elements=broadcastElements(id,"."+name,cells[id].items,supplied,*shape,facet!=Facet::Value && shapeCount(*shape)==0,true);
            if(facet!=Facet::Value) {typeProperty(broadcastType(elements,*shape),facet,result);return;}
            // Even an empty result has a checked scalar interface.
            (void)demand(id,Facet::Type);
            std::size_t position=0;
            std::function<Ref(std::size_t)> values=[&](std::size_t dimension)->Ref {
                if(dimension==shape->size()) return demand(elements[position++],Facet::Value);
                std::vector<Ref> nested;for(std::size_t i=0;i<(*shape)[dimension];++i) nested.push_back(values(dimension+1));
                return terms().make("Values",nested);
            };
            equation(result,values(0));return;
        }
        if(facet!=Facet::Type && facet!=Facet::Value) {conflict();return;}
        for(const auto& type:supplied) {
            auto format=scalarFormat(type);if(!format || format->kind==ScalarFormat::Bool) {conflict();return;}
        }
        auto operand=supplied[0];
        if(binary && op!=16) {operand=fresh("common math operand");commonType(supplied[0],supplied[1],operand);}
        if(op==16) {
            auto exponent=scalarFormat(supplied[1]).value();
            if(exponent.kind!=ScalarFormat::Exact && !(exponent.kind==ScalarFormat::IntegerKind && !exponent.sign && exponent.bits<=32)) {conflict();return;}
        }
        later({operand},[this,id,facet,result,supplied,op,operand,binary](const auto& resolved){
            const auto format=scalarFormat(resolved[0]).value();
            if((op==0 || op==7 || (op>=8 && op<=11)) && format.kind!=ScalarFormat::Float) {conflict();return;}
            if(op==12 && format.kind!=ScalarFormat::Exact && format.kind!=ScalarFormat::IntegerKind) {conflict();return;}
            auto type=op>=8 && op<=11 ? terms().make("Bool") : operand;
            if(facet==Facet::Type) {equation(result,type);return;}
            auto a=fresh("converted math left"),b=number(0);
            request.goals.push_back(call("implicit",{supplied[0],operand,demand(cells[id].items[0],Facet::Value),a}));
            if(binary) {
                b=fresh("converted math right");
                request.goals.push_back(call("implicit",{supplied[1],op==16 ? terms().make("UInt",{number(32)}) : operand,demand(cells[id].items[1],Facet::Value),b}));
            }
            request.goals.push_back(build("numeric_math",{operand,number(op),a,b,result}));
        });
    });
}
