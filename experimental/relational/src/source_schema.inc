// Schema evaluation receives immutable views of fields and frozen captures.
// Helpers may still mutate their own parameters and scratch locals.
Id Session::schemaOrigin(const std::string& name,Id lexical) {
    Id first=none;
    for(Id env=lexical;env!=none;env=environments[env].parent) {
        auto found=environments[env].values.find(name);if(found==environments[env].values.end()) continue;
        Id id=found->second;if(first==none) first=id;
        if(cells[id].readonly && cells[id].previous!=none) return cells[id].previous;
        if(cells[id].kind==Cell::Bound || cells[id].kind==Cell::Store) return location(id);
    }
    return first;
}
Id Session::schemaAlias(Id target,const std::string& name,Id lexical) {
    if(global(target)) return target;
    Cell proxy;proxy.kind=Cell::Alias;proxy.expanded=true;proxy.flowPure=true;proxy.readonly=true;
    proxy.target=target;proxy.previous=schemaOrigin(name,lexical);proxy.site=cells[target].site;proxy.scope=lexical;
    return addCell(std::move(proxy));
}
bool Session::schemaBranches(Id site,Id scope) {
    std::set<std::pair<Id,Id>> calls;
    std::function<bool(Id,Id)> visit=[&](Id at,Id lexical) {
        if(at==none) return false;
        const auto& node=graph.nodes[at];
        if(node.syntax==Syntax::Select || node.syntax==Syntax::Probe ||
           node.syntax==Syntax::Iterate || node.syntax==Syntax::Loop || node.syntax==Syntax::Repeat ||
           (node.syntax==Syntax::Binary && (node.text=="&&" || node.text=="||")) ||
           (node.syntax==Syntax::Name && node.text.starts_with("$"))) return true;
        if(node.syntax==Syntax::Function || node.syntax==Syntax::RecordDecl) return false;
        if(node.syntax==Syntax::Call) {
            auto definitions=functions(lexical,node.text);
            if(definitions.size()!=1) return true;
            auto definition=definitions[0];
            if(!calls.emplace(definition.site,definition.scope).second) return true;
            bool branching=visit(graph.nodes[definition.site].body,definition.scope);
            calls.erase({definition.site,definition.scope});if(branching) return true;
        }
        if(node.syntax==Syntax::Construct) {
            auto definition=record(lexical,node.text);
            if(definition.site==none || !calls.emplace(definition.site,definition.scope).second) return true;
            for(Id field:graph.nodes[definition.site].children) if(visit(graph.nodes[field].annotation,definition.scope)) return true;
            calls.erase({definition.site,definition.scope});
        }
        for(Id child:node.children) if(visit(child,lexical)) return true;
        return visit(node.body,lexical);
    };
    return visit(site,scope);
}
void Session::captureSchemaType(Id id,std::set<Id> visited) {
    if(!visited.insert(id).second) return;
    expand(id);const auto& cell=cells[id];
    if(cell.readonly) {(void)demand(id,Facet::Type);return;}
    if(cell.kind==Cell::Alias && cell.target!=none) {
        if(changesProjectionState(id)) {
            auto entry=states[0];entry.schema=true;auto evaluated=flow(id,state(std::move(entry)));
            equation(evaluated.effect,number(0));(void)demand(evaluated.value,Facet::Type);return;
        }
        captureSchemaType(cell.target,std::move(visited));return;
    }
    if(cell.kind==Cell::Dispatch) {
        later({demand(id,Facet::Target)},[this,id,visited](const auto& args){
            Definition definition{args[0]->arguments[0]->number.template convert_to<Id>(),args[0]->arguments[1]->number.template convert_to<Id>()};
            Id body=instantiate(definition,cells[id].items,cells[id].site,cells[id].scope);
            captureSchemaType(body,visited);
        });return;
    }
    if(cell.kind==Cell::Bound && cell.annotation==none && cell.target!=none) {captureSchemaType(cell.target,std::move(visited));return;}
    if(cell.kind==Cell::SyntaxCell && graph.nodes[cell.site].syntax==Syntax::Select && cell.items.size()==3) {
        auto entry=states[0];entry.schema=true;auto condition=flow(cell.items[0],state(std::move(entry)));equation(condition.effect,number(0));
        auto boolean=fresh("schema type alternative condition");
        request.goals.push_back(call("implicit",{demand(condition.value,Facet::Type),terms().make("Bool"),demand(condition.value,Facet::Value),boolean}));
        later({boolean},[this,id,visited](const auto& args){
            const auto& select=cells[id];
            if(args[0]->kind==Term::Number) captureSchemaType(select.items[args[0]->number==0 ? 2u : 1u],visited);
            else if(args[0]->name=="Runtime") {
                // This is a dependency question, not a claim that the two
                // alternatives have equal types. Actual uses still validate
                // the original requirement against the fixed field interface.
                captureSchemaType(select.items[1],visited);captureSchemaType(select.items[2],visited);
            } else conflict();
        });return;
    }
    (void)demand(id,Facet::Type);
}
Ref Session::recordRequirement(Ref type,Id site,Id scope,Id dimensionScope) {
    const auto& spec=graph.nodes[site];auto checks=terms().make("Done");
    if(spec.text=="(tuple)") {
        std::vector<Ref> fields;
        for(Id child:spec.children) {
            auto field=fresh("tuple schema field");fields.push_back(field);
            checks=sequence(checks,recordRequirement(field,child,scope,dimensionScope));
        }
        equation(type,tupleType(terms(),fields));return checks;
    }
    for(auto dimension=spec.children.rbegin();dimension!=spec.children.rend();++dimension) {
        auto element=fresh("schema element"),length=fresh("schema fixed extent");
        equation(type,terms().make("Array",{element,length}));type=element;
        auto entry=states[0];entry.schema=true;Id start=state(std::move(entry));
        // A straight-line expression has no value-dependent capture choices.
        // It needs no abstract runtime representation for an exact field.
        Id lookupScope=dimensionScope!=none && schemaBranches(*dimension,scope) ? dimensionScope : scope;
        auto expressionValue=flow(expression(*dimension,lookupScope),start);equation(expressionValue.effect,number(0));
        auto condition=fresh("schema extent equality"),checked=fresh("schema extent check");
        request.goals.push_back(call("compare==",{demand(expressionValue.value,Facet::Value),length,condition}));
        later({condition},[this,checked](const auto& args){
            if(args[0]->kind==Term::Number) {equation(args[0],number(1));equation(checked,terms().make("Done"));}
            else if(args[0]->name=="Runtime") equation(checked,terms().make("CheckSchema",{args[0]}));
            else conflict();
        });
        checks=sequence(checks,sequence(expressionValue.code,checked));
    }
    if(spec.body!=none || record(scope,spec.text).site==none) equation(type,annotationBase(site,scope));
    else {
        Cell expected;expected.definition=record(scope,spec.text);
        equation(type,terms().make("Record",{nominal(expected),fresh("schema nested fields"),fresh("schema nested captures")}));
    }
    return checks;
}
Ref Session::validateSchema(Ref type,Ref value) {
    auto result=fresh("value schema checks");
    later({type,value},[this,result](const auto& args){
        const auto& t=args[0];const auto& v=args[1];auto checks=terms().make("Done");
        if(t->name=="Array") {
            if(v->name!="Values" || v->arguments.size()!=arrayCount(t)) {conflict();return;}
            for(const auto& item:v->arguments) checks=sequence(checks,validateSchema(t->arguments[0],item));
        } else if(t->name=="Record") {
            if(tupleType(t)) {
                const auto& fields=t->arguments[1]->arguments;
                if(v->name!="RecordValue" || !identical(t->arguments[0],v->arguments[0]) || v->arguments[1]->arguments.size()!=fields.size()) {conflict();return;}
                for(std::size_t i=0;i<fields.size();++i) checks=sequence(checks,validateSchema(fields[i]->arguments[0],v->arguments[1]->arguments[i]));
                equation(result,checks);return;
            }
            auto found=schemaDefinitions.find(show(t->arguments[0]));
            if(found==schemaDefinitions.end()) {pending("record schema definition remains unavailable");return;}
            const auto definition=found->second;const auto& schema=graph.nodes[definition.site];
            const auto& fields=t->arguments[1]->arguments;
            if(v->name!="RecordValue" || v->arguments[1]->arguments.size()!=fields.size() || fields.size()!=schema.children.size()) {conflict();return;}
            Id scope=environment(definition.scope);
            std::map<std::string,std::map<std::string,Ref>> captures;
            for(const auto& field:t->arguments[2]->arguments) {
                auto split=field->name.find(':');
                captures[split==std::string::npos ? field->name : field->name.substr(split+1)][split==std::string::npos ? "value" : field->name.substr(0,split)]=field->arguments[0];
            }
            for(const auto& [name,facets]:captures) {
                Ref capturedType,capturedValue=facets.contains("value") ? facets.at("value") : fresh("unread schema capture");
                if(facets.contains("type")) capturedType=facets.at("type");
                else if(facets.contains("category") && facets.at("category")->name=="String") capturedType=terms().make("String");
                else if(facets.contains("length") || facets.contains("element")) capturedType=terms().make("Array",{
                    facets.contains("element") ? facets.at("element") : fresh("unread capture element"),facets.contains("length") ? facets.at("length") : fresh("unread capture extent")});
                else if(facets.contains("identity")) capturedType=terms().make("Record",{facets.at("identity"),fresh("unread capture fields"),fresh("unread capture schema")});
                else if(facets.contains("category") || facets.contains("shape")) capturedType=fresh("unread capture type");
                else {
                    Id original=lookup(definition.scope,name);
                    if(original==none) {pending("schema capture lacks its logical type");return;}
                    capturedType=demand(original,Facet::Type);
                }
                if(runtimeValue(capturedValue)) {conflict();return;}
                Id captured=known(capturedType,capturedValue);cells[captured].readonly=true;cells[captured].previous=schemaOrigin(name,definition.scope);
                for(auto [label,facet]:{std::pair{"category",Facet::Category}, {"length",Facet::Length}, {"element",Facet::Element}, {"identity",Facet::Identity}, {"shape",Facet::Shape}})
                    if(facets.contains(label)) cells[captured].knownProperties[facet]=facets.at(label);
                environments[scope].values[name]=captured;
            }
            for(std::size_t i=0;i<fields.size();++i) {
                Id field=known(fields[i]->arguments[0],v->arguments[1]->arguments[i]);cells[field].readonly=true;
                environments[scope].values[fields[i]->name]=field;
            }
            for(std::size_t i=0;i<fields.size();++i) {
                auto annotationSite=graph.nodes[schema.children[i]].annotation;
                if(annotationSite!=none) checks=sequence(checks,recordRequirement(fields[i]->arguments[0],annotationSite,scope));
                checks=sequence(checks,validateSchema(fields[i]->arguments[0],v->arguments[1]->arguments[i]));
            }
        }
        equation(result,checks);
    });return result;
}
