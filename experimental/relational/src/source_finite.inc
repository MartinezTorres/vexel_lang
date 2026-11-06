void Session::rangeProperty(Id id,Facet facet,Ref result) {
    const auto& c=cells[id];
    request.goals.push_back(call("integer_operand",{demand(c.items[0],Facet::Type)}));
    request.goals.push_back(call("integer_operand",{demand(c.items[1],Facet::Type)}));
    auto left=demand(c.items[0],Facet::Value),right=demand(c.items[1],Facet::Value);
    auto element=fresh("range element");request.goals.push_back(call("promote",{demand(c.items[0],Facet::Type),demand(c.items[1],Facet::Type),element}));
    later({left,right},[this,facet,result,element](const auto& bounds){
        if(bounds[0]->kind!=Term::Number || bounds[1]->kind!=Term::Number) {pending("range bounds must be compile-time integers");return;}
        Integer distance=bounds[1]->number-bounds[0]->number,step=distance>0 ? 1 : -1;
        if(distance==0) {conflict();return;}
        if(distance<0) distance=-distance;
        if(distance>10000) throw ConstructionLimit("range construction budget exhausted");
        if(facet==Facet::Type) equation(result,terms().make("Array",{element,number(distance)}));
        else if(facet==Facet::Length) equation(result,number(distance));
        else if(facet==Facet::Element) equation(result,element);
        else if(facet==Facet::Value) {
            std::vector<Ref> values;
            for(Integer i=0;i<distance;++i) values.push_back(number(bounds[0]->number+i*step));
            equation(result,terms().make("Values",values));
        } else pending("range property has no schema");
    });
}
void Session::finiteChoice(Id id,Ref result) {
    const auto& c=cells[id];
    later({demand(id,Facet::Type),demand(c.items[0],Facet::Value),demand(c.items[0],Facet::Element)},[this,id,result](const auto& args){
        if(args[1]->name!="Values") {conflict();return;}
        if(runtimeValue(args[1])) {pending("finite domains must be compile-time values",cells[id].site);return;}
        auto value=structure(args[0],[&](const Ref& type){
            auto chosen=fresh("finite leaf"),converted=fresh("finite representation");
            request.goals.push_back(finite(chosen,args[1]->arguments));request.goals.push_back(call("implicit",{args[2],type,chosen,converted}));return converted;
        });
        equation(result,value);
    });
}
