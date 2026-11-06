// Array dimensions are independent interface facts. Their leaf deliberately
// carries no scalar/record schema. Array constructors reuse ordinary unification
// so consumers can select an outer dimension without requesting record fields.
void Session::annotationShape(Id site,Id scope,Ref result) {
    const auto& spec=graph.nodes[site];
    if(spec.text=="(tuple)") {equation(result,terms().make("ShapeScalar"));return;}
    Ref shape=spec.body!=none ? demand(expression(spec.body,scope),Facet::Shape) : terms().make("ShapeScalar");
    for(Id dimension:spec.children) {
        auto length=demand(expression(dimension,scope),Facet::Value);
        request.goals.push_back(lessEqual(number(0),length));
        shape=terms().make("Array",{shape,length});
    }
    equation(result,shape);
}
void Session::broadcastDimensions(const std::vector<Id>& operands,Ref result) {
    std::vector<Ref> shapes;for(Id operand:operands) shapes.push_back(demand(operand,Facet::Shape));
    later(shapes,[this,result](const auto& facts){
        for(const auto& fact:facts) {
            Ref leaf=fact;while(leaf->name=="Array") leaf=leaf->arguments[0];
            if(leaf->name!="ShapeScalar") {pending("broadcast rank needs closed shape evidence");return;}
        }
        auto shape=broadcastShape(facts);
        if(!shape) {conflict();return;}
        equation(result,shapedType(terms(),terms().make("ShapeScalar"),*shape));
    });
}
void Session::shapeProperty(Id id,Ref result) {
    const auto& c=cells[id];
    if(c.kind==Cell::Missing) {pending(c.problem,c.site);return;}
    if(c.kind==Cell::Math) {broadcastDimensions(c.items,result);return;}
    if(Id selected=operatorCall(id);selected!=none) {operatorProperty(id,selected,Facet::Shape,result);return;}
    if(!broadcastName(id).empty()) {broadcastDimensions(c.items,result);return;}
    if(c.kind==Cell::Projection) {projectedProperty(c.target,c.names,result,Facet::Shape);return;}
    if(c.kind==Cell::Record) {equation(result,terms().make("ShapeScalar"));return;}
    if(c.kind==Cell::Alias || c.kind==Cell::Receiver || c.kind==Cell::SchemaSymbol ||
       ((c.kind==Cell::Bound || c.kind==Cell::Thunk) && c.annotation==none && c.target!=none)) {
        equation(result,demand(c.target,Facet::Shape));return;
    }
    if(c.kind==Cell::Store) {equation(result,demand(c.location,Facet::Shape));return;}
    if(c.kind==Cell::Dispatch) {dispatch(id,Facet::Shape,result);return;}
    if(c.annotation!=none) {
        const auto& spec=graph.nodes[c.annotation];
        if(c.kind==Cell::Bound && spec.body!=none && spec.children.empty() && (global(id) || c.field)) aliasProperty(id,Facet::Shape,result);
        else annotationShape(c.annotation,c.scope,result);
        return;
    }
    if(c.kind==Cell::Invocation) {
        const auto& fn=graph.nodes[c.definition.site];
        if(fn.annotation!=none) {
            Id scope=environment(c.definition.scope);
            for(std::size_t i=0;i<c.items.size();++i) {
                const auto& param=graph.nodes[fn.children[i]];
                environments[scope].values[param.text]=bound(fn.children[i],scope,param.annotation,c.items[i]);
            }
            annotationShape(fn.annotation,scope,result);return;
        }
    }
    if(c.kind==Cell::Element || (c.kind==Cell::SyntaxCell && graph.nodes[c.site].syntax==Syntax::Index)) {
        Id owner=c.kind==Cell::Element ? c.target : c.items[0];
        later({demand(owner,Facet::Category)},[this,owner,result](const auto& categories){
            if(categories[0]->name=="String") equation(result,terms().make("ShapeScalar"));
            else equation(demand(owner,Facet::Shape),terms().make("Array",{result,fresh("indexed outer dimension")}));
        });return;
    }
    if(c.kind==Cell::SyntaxCell) {
        const auto& n=graph.nodes[c.site];
        if(n.syntax==Syntax::Tuple || n.syntax==Syntax::String) {equation(result,terms().make("ShapeScalar"));return;}
        if(n.syntax==Syntax::Array) {
            Ref element=c.items.empty() ? terms().make("ShapeScalar") : demand(c.items[0],Facet::Shape);
            for(std::size_t i=1;i<c.items.size();++i) equation(element,demand(c.items[i],Facet::Shape));
            equation(result,terms().make("Array",{element,number(c.items.size())}));return;
        }
        if(n.syntax==Syntax::Binary && n.text=="..") {
            equation(result,terms().make("Array",{terms().make("ShapeScalar"),demand(id,Facet::Length)}));return;
        }
        if(n.syntax==Syntax::Cast) {annotationShape(n.children[0],c.scope,result);return;}
    }
    typeProperty(demand(id,Facet::Type),Facet::Shape,result);
}
