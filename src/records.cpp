#include "vexel.hpp"
#include <algorithm>

namespace vx {
namespace {
bool containsDependent(const Graph& g,const Type& type) {
    if(type.kind==Type::Array) return containsDependent(g,*type.element);
    if(type.kind!=Type::Record) return false;
    if(type.declaration!=none)
        for(Id field:g.nodes[type.declaration].children)
            if(dependentField(g,type.declaration,g.nodes[field].annotation)) return true;
    return std::any_of(type.fields.begin(),type.fields.end(),[&](const Type& field){return containsDependent(g,field);});
}
void checkInvariantPurity(const Graph& g,Id expression,Span at) {
    Id region=g.nodes[expression].scope;
    while(region!=none) {
        Id parent=g.scopes[region].parent;
        if(parent==none) parent=g.scopes[region].caller; // A frozen binding view retains its owning region.
        if(parent==none || !g.scopes[parent].typeValues) break;
        region=parent;
    }
    if(region==none) return;
    auto inside=[&](Id scope) {
        std::set<Id> seen;
        while(scope!=none && seen.insert(scope).second) {
            if(scope==region) return true;
            const auto& frame=g.scopes[scope];
            scope=g.nodes[frame.node].slot!=none ? frame.caller : frame.parent!=none ? frame.parent : frame.caller;
        }
        return false;
    };
    std::set<Id> visited;std::vector<Id> pending{expression};
    while(!pending.empty()) {
        Id id=pending.back();pending.pop_back();if(id==none || !visited.insert(id).second) continue;
        const auto& node=g.nodes[id];
        if(node.rule==Rule::Store && (node.fact || node.inferredType) &&
           (node.fact ? node.completion : node.typeCompletion).normal && inside(node.scope) && !inside(g.nodes[node.slot].scope))
            throw Error(at,"record invariants cannot modify enclosing storage");
        pending.insert(pending.end(),node.inputs.begin(),node.inputs.end());
        pending.insert(pending.end(),node.dependencies.begin(),node.dependencies.end());
        pending.push_back(node.expansion);pending.push_back(node.endState);pending.push_back(node.previous);
    }
}
}
bool dependentField(const Graph& g,Id declaration,Id annotation) {
    if(annotation==none) return false;
    std::set<std::string> names;
    for(Id field:g.nodes[declaration].children) names.insert(g.nodes[field].text);
    std::set<Id> visited;std::vector<Id> pending{annotation};
    while(!pending.empty()) {
        Id id=pending.back();pending.pop_back();if(!visited.insert(id).second) continue;
        const auto& node=g.nodes[id];
        if(node.syntax==Syntax::Name && names.contains(node.text)) return true;
        pending.insert(pending.end(),node.children.begin(),node.children.end());
        if(node.annotation!=none) pending.push_back(node.annotation);
        if(node.body!=none) pending.push_back(node.body);
    }
    return false;
}
bool Solver::recordSchema(Id id,Type& result) {
    auto& n=g.nodes[id];
    if(!n.typeExpanded) {
        bool ready=true;
        std::optional<Type> constructed;
        bool dependent=std::any_of(g.nodes[result.declaration].children.begin(),g.nodes[result.declaration].children.end(),[&](Id field) {
            return dependentField(g,result.declaration,g.nodes[field].annotation);
        });
        if((n.key=="constructor-type" || n.key=="argument-type" || n.key=="equation-witness") && dependent) {
            const Type* instance=needType(id,n.slot);if(!instance) return false;
            constructed=resolvedType(*instance);
            if(n.key=="argument-type" || n.key=="equation-witness") {
                for(std::size_t i=0;i<n.inputs.size() && constructed->kind==Type::Array;++i) {
                    Type element=*constructed->element;constructed=std::move(element);
                }
                if(constructed->kind!=Type::Record || constructed->declaration!=result.declaration ||
                   constructed->fields.size()!=result.fields.size()) constructed.reset();
            }
        }
        std::set<std::string> fields;
        std::map<std::string,Type> fieldTypes;
        for(std::size_t i=0;i<result.fields.size();++i) {
            const auto& name=result.fieldNames[i];fields.insert(name);
            Type field=result.fields[i];
            if(constructed && !field.settled()) {
                field=constructed->fields[i];
            }
            fieldTypes[name]=std::move(field);
        }
        std::map<std::string,std::pair<Id,Id>> captures;
        std::set<std::string> visitedFunctions;
        std::function<void(Id,Id,const std::set<std::string>&,const std::map<std::string,Type>&,const std::string&)> visit;
        visit=[&](Id site,Id scope,const std::set<std::string>& bound,const std::map<std::string,Type>& boundTypes,const std::string& prefix) {
            const auto& source=g.nodes[site];
            // A type query needs types, not a captured runtime value. Its own
            // dependent signature/dimension obligations retain their rules.
            if(source.syntax==Syntax::Type) {
                for(Id dimension:source.children) visit(dimension,scope,bound,boundTypes,prefix);
                return;
            }
            // Operator syntax is a call site too. Discover the same receiver
            // methods as expression construction, including scalar methods
            // reached by a dotted builtin's broadcast fallback.
            if(source.syntax==Syntax::Unary || source.syntax==Syntax::Binary || source.syntax==Syntax::Iterate ||
               (source.syntax==Syntax::Assign && source.text!="=")) {
                const std::string op=source.syntax==Syntax::Assign ? source.text.substr(0,source.text.size()-1) : source.text;
                const bool dotted=op.starts_with('.') && op!="..";
                if(!functions("."+op,scope).empty() || (dotted && !functions(op,scope).empty())) {
                    Node call=source;call.syntax=Syntax::Call;call.text="."+op;call.receiverCount=1;
                    call.key=dotted ? "schema-dotted-operator" : "schema-operator";call.identity+="/schema-operator";
                    visit(g.add(std::move(call)),scope,bound,boundTypes,prefix);return;
                }
                if(source.syntax==Syntax::Iterate && source.text=="@@" && !functions(".<",scope).empty()) {
                    Node call=source;call.syntax=Syntax::Call;call.text=".<";call.receiverCount=1;
                    call.children={source.children[0],source.children[0]};call.key="schema-broadcast";
                    call.identity+="/schema-comparison";
                    visit(g.add(std::move(call)),scope,bound,boundTypes,prefix);
                }
            }
            // Freeze a closed expression rather than all of its inputs. For
            // example, |array| is available from shape even if its elements
            // are uninitialized. Names bound by fields/parameters stay open.
            auto closed=[&]() {
                std::vector<Id> pending{site};bool lexical=false;
                while(!pending.empty()) {
                    const auto& part=g.nodes[pending.back()];pending.pop_back();
                    if(part.syntax==Syntax::Name) {
                        if(bound.contains(part.text)) return false;
                        lexical=lexical || binding(part.text,scope)!=none;
                    }
                    if(part.syntax==Syntax::Call && !functions(part.text,scope).empty()) lexical=true;
                    pending.insert(pending.end(),part.children.begin(),part.children.end());
                    if(part.body!=none) pending.push_back(part.body);
                }
                return lexical;
            };
            bool expression=source.syntax==Syntax::Name || source.syntax==Syntax::Unary || source.syntax==Syntax::Binary ||
                source.syntax==Syntax::Call || source.syntax==Syntax::Index || source.syntax==Syntax::Group ||
                source.syntax==Syntax::Member || source.syntax==Syntax::Cast || source.syntax==Syntax::Array ||
                source.syntax==Syntax::Tuple || source.syntax==Syntax::Select;
            if(expression && closed()) {
                captures.emplace(prefix+g.identity(site),std::make_pair(site,scope));return;
            }
            if(source.syntax==Syntax::Name && !bound.contains(source.text) && binding(source.text,scope)!=none)
                captures.emplace(prefix+source.text,std::make_pair(site,scope));
            auto candidates=source.syntax==Syntax::Call ? functions(source.text,scope) : std::vector<Scope::Callable>{};
            std::erase_if(candidates,[&](const auto& candidate) {
                const auto& fn=g.nodes[candidate.declaration];
                return fn.children.size()!=source.children.size() || fn.receiverCount!=source.receiverCount;
            });
            Id selected=none;
            bool dispatchMatched=false;
            // Resolve the applicable body with the ordinary overload machinery
            // when the open fields/parameters supply concrete argument types.
            // These private inputs have no values: a schema question must not
            // execute its constructor arguments to discover captures.
            bool canSelect=!candidates.empty();
            std::vector<Id> pending;
            for(std::size_t i=0;i<source.children.size();++i) {
                bool deferred=!candidates.empty() && std::all_of(candidates.begin(),candidates.end(),[&](const auto& candidate) {
                    const auto& parameter=g.nodes[g.nodes[candidate.declaration].children[i]];
                    return parameter.expression && parameter.annotation==none;
                });
                if(!deferred) pending.push_back(source.children[i]);
            }
            while(canSelect && !pending.empty()) {
                const auto& part=g.nodes[pending.back()];pending.pop_back();
                if(part.syntax==Syntax::Name && bound.contains(part.text)) {
                    auto found=boundTypes.find(part.text);
                    Type known=found==boundTypes.end() ? Type{} : resolvedType(found->second);
                    requireTypeProperties(known);
                    canSelect=known.settled() && known.kind!=Type::Exact;
                }
                // Locally declared bindings need their own analysis frame.
                if(part.syntax==Syntax::Block || part.syntax==Syntax::Declare || part.syntax==Syntax::Assign) canSelect=false;
                pending.insert(pending.end(),part.children.begin(),part.children.end());
            }
            if(canSelect) {
                std::string key=prefix+g.identity(site)+"/"+std::to_string(scope);
                for(const auto& [name,known]:boundTypes) key+="/"+name+":"+known.str();
                for(Id probe:n.candidates) if(g.nodes[probe].key==key) {selected=probe;break;}
                if(selected==none) {
                    Id frame=g.scope(scope,site,n.identity+"/schema-overload");g.scopes[frame].typeValues=true;
                    for(const auto& [name,known]:boundTypes) {
                        Id input=g.semantic(Rule::Input,site,{},frame);g.nodes[input].annotation=literal(site,resolvedType(known),{});
                        g.nodes[input].readonly=true;g.nodes[input].text=name;g.scopes[frame].slots[name]=input;
                    }
                    Id entry=g.semantic(Rule::Entry,site,{},frame);g.nodes[entry].previous=n.previous;
                    Node query=source;query.syntax=Syntax::MethodProbe;query.identity+="/schema-overload";
                    selected=callExpression(g.add(std::move(query)),frame,entry).value;
                    g.nodes[selected].key=key;n.candidates.push_back(selected);
                }
                bool argumentsReady=true,exactUnknown=false;
                for(Id input:g.nodes[selected].inputs) {
                    if(g.nodes[input].rule==Rule::Deferred) continue;
                    const Type* argument=needType(id,input);
                    if(argument) requireTypeProperties(*argument);
                    if(!argument || !argument->settled()) argumentsReady=false;
                    else if(argument->kind==Type::Exact && (!g.nodes[input].fact || !g.nodes[input].fact->value.known())) exactUnknown=true;
                }
                if(!argumentsReady) {ready=false;return;}
                if(source.key=="schema-broadcast" && !g.nodes[selected].phase) {
                    // Probe element types without reading an array element or
                    // assuming that a possibly empty array contains a value.
                    for(Id& input:g.nodes[selected].inputs) {
                        Type supplied=*needType(id,input),scalar=supplied;
                        while(scalar.kind==Type::Array) scalar=Type(*scalar.element);
                        if(scalar==supplied) continue;
                        Id sample=g.semantic(Rule::Input,site,{},g.nodes[selected].scope);
                        g.nodes[sample].annotation=literal(site,scalar,{});input=sample;
                    }
                    g.nodes[selected].phase=1;ready=false;enqueue(id);return;
                }
                // Exact-value representability can select an overload. Until
                // that value exists, retain its possible capture obligations.
                if(exactUnknown && candidates.size()>1) selected=none;
                else {
                    const Fact* matched=need(id,selected);if(!matched) {ready=false;return;}
                    if(matched->value.number==0) {
                        if(!source.key.starts_with("schema-")) fail(id,"no matching overload in record invariant: "+source.text,Diagnostic::NoOverload);
                        candidates.clear();selected=none;
                    } else {
                        dispatchMatched=true;
                        Id declaration=g.nodes[selected].body;
                        std::erase_if(candidates,[&](const auto& candidate){return candidate.declaration!=declaration;});
                    }
                }
            }
            for(const auto& candidate:candidates) {
                const auto& function=g.nodes[candidate.declaration];
                if(candidate.scope==none || function.body==none) continue;
                std::set<std::string> locals;
                std::map<std::string,Type> localTypes;
                std::string context=g.identity(candidate.declaration);
                for(std::size_t i=0;i<function.children.size();++i) {
                    const auto& parameter=g.nodes[function.children[i]];locals.insert(parameter.text);
                    Type known;
                    if(parameter.annotation!=none) {
                        Id requirement=selected!=none ? parameterType(selected,candidate.declaration,i,candidate.scope) : type(parameter.annotation,candidate.scope,n.previous);
                        const Fact* declared=need(id,requirement);
                        if(!declared) {ready=false;return;} known=declared->type;
                    } else if(selected!=none && g.nodes[g.nodes[selected].inputs[i]].rule!=Rule::Deferred) {
                        const Type* supplied=needType(id,g.nodes[selected].inputs[i]);
                        if(!supplied) {ready=false;return;} known=*supplied;
                    }
                    localTypes[parameter.text]=known;context+="/"+known.str();
                }
                if(!visitedFunctions.insert(context+"/scope/"+std::to_string(candidate.scope)).second) continue;
                std::vector<Id> bodySites{function.body};
                while(!bodySites.empty()) {
                    const auto& local=g.nodes[bodySites.back()];bodySites.pop_back();
                    if(local.syntax==Syntax::Declare) locals.insert(local.text);
                    if(local.syntax==Syntax::Assign && g.nodes[local.children[0]].syntax==Syntax::Name) {
                        const auto& name=g.nodes[local.children[0]].text;
                        if(slot(name,candidate.scope)==none && constant(name,candidate.scope)==none && !g.symbols.contains(name))
                            locals.insert(name);
                    }
                    if(local.syntax!=Syntax::Function) bodySites.insert(bodySites.end(),local.children.begin(),local.children.end());
                }
                visit(function.body,candidate.scope,locals,localTypes,context+"/");
            }
            if(source.key=="schema-dotted-operator" && !dispatchMatched && !functions(source.text.substr(1),scope).empty()) {
                Node scalar=source;scalar.text=source.text.substr(1);scalar.key="schema-broadcast";
                scalar.identity+="/scalar";visit(g.add(std::move(scalar)),scope,bound,boundTypes,prefix);
            }
            if(source.key=="schema-operator" && source.text==".@@" && !dispatchMatched && !functions(".<",scope).empty()) {
                Node comparison=source;comparison.text=".<";comparison.key="schema-broadcast";
                comparison.children={source.children[0],source.children[0]};comparison.identity+="/comparison";
                visit(g.add(std::move(comparison)),scope,bound,boundTypes,prefix);
            }
            if(source.syntax==Syntax::Function) return;
            for(Id child:source.children) visit(child,scope,bound,boundTypes,prefix);
            if(source.body!=none) visit(source.body,scope,bound,boundTypes,prefix);
            if(source.annotation!=none) visit(source.annotation,scope,bound,boundTypes,prefix);
        };
        for(Id field:g.nodes[result.declaration].children) {
            Id annotation=g.nodes[field].annotation;
            if(dependentField(g,result.declaration,annotation)) visit(annotation,n.scope,fields,fieldTypes,"");
        }
        if(!ready) return false;
        n.typeExpanded=true;
        for(const auto& [name,capture]:captures) {
            Id frame=g.scope(capture.second,n.site,n.identity+"/schema-capture");g.scopes[frame].typeValues=true;
            Id entry=g.semantic(Rule::Entry,n.site,{},frame);g.nodes[entry].previous=n.previous;
            Id value=build(capture.first,frame,entry).value;
            Id frozen=g.semantic(Rule::PropertyValue,capture.first,{value},frame);g.nodes[frozen].text=name;
            n.environment.push_back(frozen);
        }
    }
    for(Id capture:n.environment) {
        const Fact* value=need(id,capture);if(!value) return false;
        if(value->value.symbolic()) requestChoice(value->value,id);
        if(!value->value.known() || value->effect>=Effect::Control)
            fail(id,"record invariant lexical captures require compile-time values; put runtime dependencies in explicit fields");
        checkInvariantPurity(g,capture,n.span);
        std::string name=g.nodes[capture].text,text=value->type.str()+":"+value->value.str();
        result.schema+=std::to_string(name.size())+":"+name+"="+std::to_string(text.size())+":"+text+";";
    }
    return true;
}
bool Solver::recordConversion(Id id,const Fact& value) {
    auto& n=g.nodes[id];
    if(n.key=="unchecked-record" || !containsDependent(g,value.type)) return false;
    // Reuse the ordinary conversion node as the representation producer. Its
    // enclosing obligation adds record invariants; no runtime value is invented.
    Id raw=g.semantic(n.rule,n.site,n.inputs,n.scope);g.nodes[raw].key="unchecked-record";
    g.nodes[raw].previous=n.previous;g.nodes[raw].annotation=n.annotation;
    std::function<Id(Id,const Type&)> validate=[&](Id input,const Type& type)->Id {
        if(!containsDependent(g,type)) return input;
        std::vector<Id> fields;
        for(std::size_t i=0;i<type.size;++i) {
            Id field=g.semantic(Rule::Index,n.site,{input,literal(n.site,Type::simple(Type::Exact),Value::integer(i))},n.scope);
            fields.push_back(validate(field,type.kind==Type::Array ? *type.element : type.fields[i]));
        }
        Id output=g.semantic(type.kind==Type::Array ? Rule::Array : type.declaration==none ? Rule::Tuple : Rule::Construct,n.site,std::move(fields),n.scope);
        if(type.kind==Type::Record && type.declaration!=none) g.nodes[output].annotation=literal(n.site,type,{});
        if(type.kind==Type::Array && type.size==0) g.nodes[output].annotation=literal(n.site,*type.element,{});
        return output;
    };
    Id checked=validate(raw,value.type);
    Id entry=g.semantic(Rule::Entry,n.site,{},n.scope);
    Id evaluated=g.semantic(Rule::Step,n.site,{entry,raw},n.scope);g.nodes[evaluated].previous=entry;
    n.expansion=g.semantic(Rule::Finish,n.site,{checked,evaluated},n.scope);
    (void)need(id,n.expansion);return true;
}
void Solver::recordRule(Id id, bool typesOnly) {
    auto& n = g.nodes[id];
    if(n.rule==Rule::TypeJoin) {
        if(n.key=="select-type" || n.key=="select-schema") {selectTypeRule(id,typesOnly);return;}
        if(n.key=="array-element" || n.key=="alias-properties" || n.key=="common-schema") {
            std::vector<Type> types;bool ready=true;
            for(Id input:n.inputs) {
                const Type* type=needType(id,input);
                if(!type) {ready=false;continue;}
                Type current=resolvedType(*type);
                if(current.kind==Type::Unknown) requireTypeProperties(current);
                if(current.kind==Type::Empty) fail(id,n.key=="alias-properties" ? "cyclic alias initializer has no value interface" : "array element has no value");
                types.push_back(std::move(current));
            }
            if(!ready) return;
            if(n.key=="common-schema") {
                for(const Type& type:types) if(type.schemaObligation!=none && !need(id,type.schemaObligation)) ready=false;
                if(!ready) return;
                for(Type& type:types) type=resolvedType(type);
                for(const Type& type:types) if(type.schema!=types.front().schema)
                    fail(id,"record types have different captured schema values");
                publish(id,{types.front(),{},Effect::Pure});return;
            }
            Type result=types.empty() ? Type{} : types.front();
            bool record=result.kind==Type::Record;
            bool array=result.kind==Type::Array;
            for(const Type& type:types) {
                record=record && type.kind==Type::Record && type.declaration==result.declaration && type.fieldNames==result.fieldNames;
                array=array && type.kind==Type::Array && type.size==result.size;
            }
            if(types.size()>1 && (record || array)) {
                // Common outer layout does not require common leaf properties.
                // Give each field/element and captured schema its own join.
                const std::size_t count=record ? result.fields.size() : 1;
                if(n.branches.empty()) for(std::size_t i=0;i<count;++i) {
                    Id offset=literal(n.site,Type::simple(Type::Exact),Value::integer(i));
                    std::vector<Id> projections;
                    for(Id input:n.inputs) projections.push_back(g.semantic(Rule::Index,n.site,{input,offset},n.scope));
                    Id joined=g.semantic(Rule::TypeJoin,n.site,std::move(projections),n.scope);
                    g.nodes[joined].key="array-element";n.branches.push_back(joined);
                }
                for(std::size_t i=0;i<count;++i) {
                    Type field;field.obligation=n.branches[i];
                    if(record) result.fields[i]=field;
                    else result.element=std::make_shared<Type>(field);
                }
                if(record) {
                    if(n.signature==none) {
                        n.signature=g.semantic(Rule::TypeJoin,n.site,n.inputs,n.scope);
                        g.nodes[n.signature].key="common-schema";
                    }
                    result.schema.clear();result.schemaObligation=n.signature;
                }
            } else for(const Type& type:types) result=common(result,type,id);
            result=resolvedType(result);
            if(!typesOnly) requireTypeProperties(result);
            if(result.settled()) publish(id,{result,{},Effect::Pure});
            else publishType(id,result);
            return;
        }
        Type result;bool ready=true;
        for(Id input:n.inputs) {
            const Type* type=needType(id,input);
            if(!type) {ready=false;continue;}
            requireTypeProperties(*type);
            if(!type->settled()) {ready=false;continue;}
            result=common(result,*type,id);
        }
        if(ready) publish(id,{result,{},Effect::Pure});
        return;
    }
    if(n.rule==Rule::ShapeCheck) {
        const Type* dimensionType=needType(id,n.inputs[0]);if(!dimensionType) return;
        if(dimensionType->kind!=Type::Exact && dimensionType->kind!=Type::Integer)
            fail(id,"record field dimension requires an integer");
        if(n.expansion==none) {
            Id condition=g.semantic(Rule::Binary,n.site,n.inputs,n.scope);g.nodes[condition].text="==";
            n.expansion=condition;
            if(n.scope!=none && g.scopes[n.scope].puzzle!=none)
                n.expansion=g.semantic(Rule::Require,n.site,{condition},n.scope);
        }
        const Fact* check=need(id,n.expansion);if(!check) return;
        if(check->effect==Effect::External) fail(id,"record field dimensions cannot perform external operations");
        checkInvariantPurity(g,n.inputs[0],n.span);
        if(g.nodes[n.expansion].rule==Rule::Require) {publish(id,*check);return;}
        if(check->value.known() && check->value.number==0) fail(id,"record field dimension does not match its array shape");
        publish(id,{Type::simple(Type::Empty),{},check->value.known() ? check->effect : std::max(check->effect,Effect::Control)});return;
    }
    if (n.rule == Rule::Unpack) {
        const Type* tuple = needType(id,n.inputs[0]); if (!tuple) return;
        if (tuple->kind != Type::Record || tuple->declaration != none)
            fail(id,"multi-assignment requires a tuple");
        if (tuple->fields.size() != n.phase) fail(id,"multi-assignment tuple arity mismatch");
        if (typesOnly) publishType(id,*tuple);
        else if (const Fact* value = need(id,n.inputs[0])) publish(id,*value);
        return;
    }
    if (n.rule == Rule::FieldIndex) {
        const Type* record = needType(id, n.inputs[0]); if (!record) return;
        if (record->kind != Type::Record) fail(id, "member access requires a record or tuple");
        auto found = std::find(record->fieldNames.begin(), record->fieldNames.end(), n.text);
        if (found == record->fieldNames.end()) fail(id, "missing field/member: " + n.text, Diagnostic::MissingMember);
        publish(id, {Type::simple(Type::Exact), Value::integer(found - record->fieldNames.begin()), Effect::Pure}); return;
    }
    Type target;
    if (n.rule == Rule::Construct) {
        if(typesOnly) {
            const Type* annotation=needType(id,n.annotation);if(!annotation) return;
            target=*annotation;
        } else {
            const Fact* annotation = need(id, n.annotation); if (!annotation) return;
            target = annotation->type;
        }
        if (target.kind != Type::Record) fail(id, "constructor requires a named record type");
        if (target.fields.size() != n.inputs.size()) fail(id, "constructor argument count does not match its fields");
    } else {
        std::vector<std::string> names;
        for (std::size_t i = 0; i < n.inputs.size(); ++i) names.push_back("__" + std::to_string(i));
        target = Type::record(none, "", std::move(names), std::vector<Type>(n.inputs.size()));
    }
    bool projected=target.declaration!=none;
    if(projected && n.environment.empty()) {
        Id parent=target.origin==none ? n.scope : g.nodes[target.origin].scope;
        Id frame=g.scope(parent,n.site,n.identity+"/fields");
        Id state=target.origin==none ? none : g.nodes[target.origin].previous;
        if(state==none) state=g.semantic(Rule::Entry,n.site,{},frame);
        // Install every field before instantiating any annotation. These are
        // immutable argument projections, not sequenced automatic stores.
        for(std::size_t i=0;i<n.inputs.size();++i) {
            Id field=g.nodes[target.declaration].children[i];
            Id proxy=g.semantic(Rule::Input,field,{n.inputs[i]},frame);
            g.nodes[proxy].key="record-field";g.nodes[proxy].readonly=true;
            g.scopes[frame].slots[g.nodes[field].text]=proxy;n.environment.push_back(proxy);
        }
        std::vector<std::size_t> parents(n.inputs.size());
        std::vector<Id> aliases(n.inputs.size(),none),joins(n.inputs.size(),none);
        for(std::size_t i=0;i<parents.size();++i) parents[i]=i;
        auto leader=[&](std::size_t i) {while(parents[i]!=i) i=parents[i];return i;};
        for(std::size_t i=0;i<n.inputs.size();++i) {
            Id annotation=g.nodes[g.nodes[target.declaration].children[i]].annotation;
            if(annotation==none || !g.nodes[annotation].children.empty() || g.nodes[annotation].body==none) continue;
            Id query=g.nodes[annotation].body;
            while(g.nodes[query].syntax==Syntax::Group) query=g.nodes[query].children[0];
            if(g.nodes[query].syntax!=Syntax::Name) continue;
            auto found=std::find(target.fieldNames.begin(),target.fieldNames.end(),g.nodes[query].text);
            if(found==target.fieldNames.end()) continue;
            std::size_t other=static_cast<std::size_t>(found-target.fieldNames.begin());
            aliases[i]=other;parents[leader(i)]=leader(other);
        }
        std::map<std::size_t,std::vector<std::size_t>> components;
        for(std::size_t i=0;i<parents.size();++i) components[leader(i)].push_back(i);
        for(const auto& [key,members]:components) {
            (void)key;
            if(std::any_of(members.begin(),members.end(),[&](std::size_t i){return aliases[i]==none;})) continue;
            std::vector<Id> arguments;for(std::size_t i:members) arguments.push_back(n.inputs[i]);
            Id joined=g.semantic(Rule::TypeJoin,n.site,std::move(arguments),frame);
            g.nodes[joined].key="alias-properties";
            for(std::size_t i:members) joins[i]=joined;
        }
        for(std::size_t i=0;i<n.inputs.size();++i) {
            Id field=g.nodes[target.declaration].children[i],annotation=g.nodes[field].annotation;
            Id requirement=none;
            if(joins[i]!=none) requirement=joins[i];
            else if(dependentField(g,target.declaration,annotation)) {
                requirement=type(annotation,frame,state);g.nodes[requirement].key="record-field";g.nodes[requirement].slot=n.inputs[i];
            } else if(target.fields[i].kind!=Type::Unknown) requirement=literal(field,target.fields[i],{});
            else if(target.fields[i].obligation!=none) requirement=target.fields[i].obligation;
            g.nodes[n.environment[i]].annotation=requirement;n.branches.push_back(requirement);
        }
    }
    std::vector<Value> values;
    Effect effect = Effect::Pure;
    bool ready = true;
    for (std::size_t i = 0; i < n.inputs.size(); ++i) {
        Id input = projected ? n.environment[i] : n.inputs[i];
        if (typesOnly) {
            if(projected) {
                if(target.fields[i].kind==Type::Unknown) target.fields[i].obligation=input;
                continue;
            }
            // Tuple fields, like nominal fields, are independent promises.
            // Eagerly requesting all inputs makes unused fields appear relevant
            // to a later equation proof, even when their errors are suppressed.
            if(target.fields[i].kind==Type::Unknown) target.fields[i].obligation=input;
        } else {
            const Fact* value = need(id, input);
            if (!value) { ready = false; continue; }
            Type field = target.fields[i].kind == Type::Unknown ? value->type : target.fields[i];
            Fact converted = convert(*value, field, false, id);
            target.fields[i] = converted.type; values.push_back(converted.value);
            effect = std::max(effect, converted.effect);
            // Field projections deliberately shed the producer's effects;
            // constructor operands still execute exactly once in source order.
            if(projected) {
                const Fact* original=need(id,n.inputs[i]);if(!original) {ready=false;continue;}
                effect=std::max(effect,original->effect);
                Id requirement=n.branches[i];
                if(requirement!=none) for(Id check:g.nodes[requirement].ends) {
                    const Fact* checked=need(id,check);if(!checked) ready=false;
                    else effect=std::max(effect,checked->effect);
                }
            }
        }
    }
    if (typesOnly) publishType(id, target);
    else if (ready) publish(id, {target, Value::array(std::move(values)), effect});
}
} // namespace vx
