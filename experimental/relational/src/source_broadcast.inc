// Broadcasting constructs scalar relationships over immutable operand snapshots.
// Shape construction never executes a sample element. Execution separately threads
// the ordinary flow relation through scalar calls in row-major array order.
std::vector<std::size_t> arrayShape(Ref type) {
    std::vector<std::size_t> shape;
    while(type->name=="Array") {shape.push_back(arrayCount(type));type=type->arguments[0];}
    return shape;
}
std::optional<std::vector<std::size_t>> broadcastShape(const std::vector<Ref>& types) {
    std::vector<std::size_t> shape;
    for(const auto& type:types) {
        auto operand=arrayShape(type);
        if(operand.size()>shape.size()) shape.insert(shape.begin(),operand.size()-shape.size(),1);
        for(std::size_t i=0;i<operand.size();++i) {
            auto& size=shape[shape.size()-operand.size()+i];auto other=operand[i];
            if(size==other || other==1) continue;
            if(size==1) size=other;
            else return {};
        }
    }
    return shape;
}
std::size_t shapeCount(const std::vector<std::size_t>& shape) {
    if(std::find(shape.begin(),shape.end(),0)!=shape.end()) return 0;
    std::size_t count=1;
    for(auto size:shape) {
        if(size>10000/count) throw ConstructionLimit("broadcast shape exceeds construction budget");
        count*=size;
    }
    return count;
}
Ref shapedType(Terms& terms,Ref element,const std::vector<std::size_t>& shape) {
    for(auto i=shape.rbegin();i!=shape.rend();++i) element=terms.make("Array",{element,terms.number(*i)});
    return element;
}
std::string Session::broadcastName(Id id) const {
    const auto& cell=cells[id];std::string op;
    if(cell.kind==Cell::Arithmetic) op=cell.inputName;
    else if(cell.kind==Cell::SyntaxCell && cell.site!=none && graph.nodes[cell.site].syntax==Syntax::Binary) op=graph.nodes[cell.site].text;
    return op.size()>1 && op[0]=='.' && op!=".." ? op : std::string{};
}
Id Session::element(Id owner,std::size_t index) {
    Cell cell;cell.kind=Cell::Element;cell.expanded=true;cell.flowPure=true;
    cell.site=cells[owner].site;cell.scope=cells[owner].scope;cell.target=owner;cell.knownValue=number(index);
    return addCell(std::move(cell));
}
std::vector<Id> Session::broadcastElements(Id id,const std::string& op,const std::vector<Id>& operands,const std::vector<Ref>& types,const std::vector<std::size_t>& shape,bool prototype,bool math) {
    std::vector<Id> result;auto count=prototype ? 1 : shapeCount(shape);
    for(std::size_t index=0;index<count;++index) {
        std::vector<std::size_t> coordinate(shape.size());auto remainder=index;
        if(!prototype) for(std::size_t dim=shape.size();dim-->0;) {coordinate[dim]=remainder%shape[dim];remainder/=shape[dim];}
        std::vector<Id> arguments;
        for(std::size_t arg=0;arg<operands.size();++arg) {
            Id selected=operands[arg];auto type=types[arg];auto operandShape=arrayShape(type);
            for(std::size_t dim=0;dim<operandShape.size();++dim) {
                if(!prototype) selected=element(selected,operandShape[dim]==1 ? 0 : coordinate[shape.size()-operandShape.size()+dim]);
                type=type->arguments[0];
            }
            // This is an interface variable, not an invented zero value.
            // A value-dependent result interface remains honestly unresolved.
            if(prototype) selected=known(type,fresh("empty broadcast element value"),cells[id].site);
            arguments.push_back(selected);
        }
        Cell scalar;scalar.kind=math ? Cell::Math : Cell::Arithmetic;scalar.expanded=true;scalar.site=cells[id].site;scalar.scope=cells[id].scope;
        scalar.inputName=op.substr(1);scalar.items=std::move(arguments);result.push_back(addCell(std::move(scalar)));
    }
    return result;
}
Ref Session::broadcastType(const std::vector<Id>& elements,const std::vector<std::size_t>& shape) {
    if(elements.empty()) throw std::logic_error("empty broadcast requires an interface prototype");
    Ref type=demand(elements[0],Facet::Type);
    for(std::size_t i=1;i<elements.size();++i) {auto joined=fresh("broadcast element interface");commonType(type,demand(elements[i],Facet::Type),joined);type=joined;}
    return shapedType(terms(),type,shape);
}
void Session::broadcastProperty(Id id,const std::string& op,Facet facet,Ref result) {
    if(facet==Facet::Value) {equation(result,demand(id,Facet::Execution));return;}
    if(facet==Facet::Length) {
        equation(demand(id,Facet::Shape),terms().make("Array",{fresh("broadcast inner dimensions"),result}));return;
    }
    if(facet!=Facet::Type && facet!=Facet::Length && facet!=Facet::Element) {conflict();return;}
    auto complete=[this,id,op,facet,result]{
        std::vector<Ref> types;for(Id operand:cells[id].items) types.push_back(demand(operand,Facet::Type));
        later(types,[this,id,op,facet,result](const auto& inputs){
            auto shape=broadcastShape(inputs);if(!shape) {conflict();return;}
            if(facet==Facet::Length) {
                if(shape->empty()) conflict();else equation(result,number((*shape)[0]));return;
            }
            auto elements=broadcastElements(id,op,cells[id].items,inputs,*shape,shapeCount(*shape)==0);
            typeProperty(broadcastType(elements,*shape),facet,result);
        });
    };
    complete();
}
void Session::flowBroadcast(Id id,const std::string& op,Id before,const Flow& out) {
    flowItems(cells[id].items,before,[this,id,op,out](auto operands,Id end,Ref prefix){
        continueFlow(end,prefix,out,[this,id,op,operands](Id at,const Flow& result){
            std::vector<Ref> types;for(Id operand:operands) types.push_back(demand(operand,Facet::Type));
            later(types,[this,id,op,operands,at,result](const auto& inputs){
                auto shape=broadcastShape(inputs);if(!shape) {conflict();return;}
                auto elements=broadcastElements(id,op,operands,inputs,*shape);
                if(elements.empty()) {
                    auto prototype=broadcastElements(id,op,operands,inputs,*shape,true);
                    equation(cells[result.value].knownType,broadcastType(prototype,*shape));
                    std::function<Ref(std::size_t)> empty=[&](std::size_t dim) {
                        std::vector<Ref> values;
                        for(std::size_t i=0;i<(*shape)[dim];++i) values.push_back(empty(dim+1));
                        return terms().make("Values",values);
                    };
                    equation(cells[result.value].knownValue,empty(0));equation(result.state,number(at));equation(result.code,terms().make("Done"));return;
                }
                flowItems(elements,at,[this,result,shape=*shape](auto values,Id ended,Ref code){
                    continueFlow(ended,code,result,[this,values,shape](Id finish,const Flow& completed){
                        Ref type=broadcastType(values,shape),elementType=type;
                        for(std::size_t i=0;i<shape.size();++i) elementType=elementType->arguments[0];
                        std::vector<Ref> converted;
                        for(Id value:values) {auto item=fresh("broadcast result element");conversion(value,elementType,demand(value,Facet::Value),item);converted.push_back(item);}
                        std::size_t offset=0;
                        std::function<Ref(std::size_t)> nest=[&](std::size_t dim) {
                            if(dim==shape.size()) return converted[offset++];
                            std::vector<Ref> items;for(std::size_t i=0;i<shape[dim];++i) items.push_back(nest(dim+1));
                            return terms().make("Values",items);
                        };
                        equation(cells[completed.value].knownType,type);equation(cells[completed.value].knownValue,nest(0));
                        equation(completed.state,number(finish));equation(completed.code,terms().make("Done"));
                    });
                });
            });
        });
    });
}
void Session::flowBroadcastStore(Id id,Id slot,const std::vector<Ref>& path,Id previous,Id rhs,Id before,const Flow& out) {
    Ref type=demand(previous,Facet::Type),value=demand(previous,Facet::Value);
    for(const auto& step:path) {
        auto partType=fresh("broadcast place type"),part=fresh("broadcast previous operand");
        if(step->name=="Field") memberValue(type,value,step->arguments[0]->name,partType,part);
        else {equation(type,terms().make("Array",{partType,fresh("broadcast place extent")}));indexValue(type,value,step->arguments[0],part);}
        type=partType;value=part;
    }
    Cell operation;operation.kind=Cell::Arithmetic;operation.expanded=true;operation.builtin=true;
    operation.site=cells[id].site;operation.scope=cells[id].scope;operation.items={known(type,value),rhs};
    auto spelling=graph.nodes[operation.site].text;operation.inputName=spelling.substr(0,spelling.size()-1);
    auto calculated=flow(addCell(std::move(operation)),before);
    later({calculated.state},[this,calculated,slot,path,out](const auto& ends){
        continueFlow(ends[0]->number.template convert_to<Id>(),calculated.code,out,[this,calculated,slot,path](Id end,const Flow& stored){storePlace(slot,path,calculated.value,end,stored,false);});
    });
}
