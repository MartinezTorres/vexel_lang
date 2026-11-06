#include "vexel.hpp"
#include <algorithm>

namespace vx {
Type Solver::resolvedType(const Type& type) const {
    std::set<Id> visiting;
    std::function<Type(const Type&)> resolve=[&](const Type& input)->Type {
        if(input.kind==Type::Unknown && input.obligation!=none && visiting.insert(input.obligation).second) {
            const auto& node=g.nodes[input.obligation];
            if(node.fact || node.inferredType) {
                Type result=resolve(node.fact ? node.fact->type : *node.inferredType);
                visiting.erase(input.obligation);
                if(result.kind!=Type::Unknown || result.obligation!=none) return result;
            } else visiting.erase(input.obligation);
            return input;
        }
        Type result=input;
        if(input.kind==Type::Array) result.element=std::make_shared<Type>(resolve(*input.element));
        if(input.kind==Type::Record) {
            for(Type& field:result.fields) field=resolve(field);
            if(input.schemaObligation!=none && g.nodes[input.schemaObligation].fact) {
                const Type* schema=&g.nodes[input.schemaObligation].fact->type;
                while(schema->kind==Type::Array) schema=schema->element.get();
                if(schema->kind!=Type::Record || schema->declaration!=input.declaration)
                    throw std::logic_error("schema obligation resolved a different declaration");
                result.schema=schema->schema;result.schemaObligation=schema->schemaObligation;result.origin=schema->origin;
            }
        }
        return result;
    };
    return resolve(type);
}
void Solver::requireTypeProperties(const Type& type) const {
    TypePropertiesRequired pending;
    std::function<void(const Type&)> visit=[&](const Type& input) {
        if(input.kind==Type::Unknown && input.obligation!=none) pending.types.push_back(input.obligation);
        if(input.kind==Type::Array) visit(*input.element);
        if(input.kind==Type::Record) {
            if(input.schemaObligation!=none) pending.schemas.push_back(input.schemaObligation);
            for(const Type& field:input.fields) visit(field);
        }
    };
    visit(type);
    if(!pending.types.empty() || !pending.schemas.empty()) throw pending;
}
Type Solver::conversionType(const Type& source,const Type& target,bool cast,Id at) const {
    Type from=resolvedType(source),to=resolvedType(target);
    if(!(from==source) || !(to==target)) return conversionType(from,to,cast,at);
    // Identity conversion preserves pending properties without needing them.
    // Actual value conversion still closes its representation and invariants.
    if(source==target) return source;
    if (target.kind == Type::Unknown && target.obligation==none) return source;
    requireTypeProperties(source);requireTypeProperties(target);
    if (source.kind == Type::Unknown) return target;
    if (cast && representationCast(source,target))
        return representation({source,Value::runtime(),Effect::Pure},target,at).type;
    if (source.kind == Type::Record && target.kind == Type::Record && source.declaration == target.declaration && source.fieldNames == target.fieldNames) {
        if(source.schema!=target.schema) fail(at,"record types have different captured schema values");
        Type result = target;
        for (std::size_t i = 0; i < result.fields.size(); ++i) result.fields[i] = conversionType(source.fields[i],target.fields[i],cast,at);
        return result;
    }
    if (source.kind == Type::Array && target.kind == Type::Array && source.size == target.size)
        return Type::array(conversionType(*source.element,*target.element,cast,at),target.size);
    if (source.kind == Type::Exact && (target.kind == Type::Exact || target.kind == Type::Integer || target.kind == Type::Fixed || target.kind == Type::Float || target.kind == Type::Boolean)) return target;
    return convert({source,Value::runtime(),Effect::Pure},target,cast,at).type;
}
void Solver::enqueueType(Id id) {
    auto& n = g.nodes[id]; n.typeDemanded = true;
    if (!n.fact && (!n.inferredType || !n.inferredType->settled()) && !n.typeQueued) { n.typeQueued = true; queue.push_back(id * 2 + 1); }
}
const Type* Solver::needType(Id owner, Id dependency) {
    auto& n = g.nodes[owner]; auto& d = g.nodes[dependency];
    if (std::find(n.typeDependencies.begin(), n.typeDependencies.end(), dependency) == n.typeDependencies.end())
        n.typeDependencies.push_back(dependency);
    Id subscription = owner * 2 + (processingType ? 1 : 0);
    if (std::find(d.typeUsers.begin(), d.typeUsers.end(), subscription) == d.typeUsers.end()) d.typeUsers.push_back(subscription);
    if (d.fact) return &d.fact->type;
    if (d.inferredType) {
        Type current=resolvedType(*d.inferredType);
        if(!(current==*d.inferredType)) publishType(dependency,std::move(current));
        return &*d.inferredType;
    }
    if (d.typeError) throw *d.typeError;
    if (!d.typeDemanded) enqueueType(dependency);
    return nullptr;
}
void Solver::publishType(Id id, Type type) {
    auto& n = g.nodes[id];
    if (n.inferredType) {
        if (*n.inferredType == type) return;
        if (n.inferredType->settled()) throw std::logic_error("inconsistent type/value properties at " + n.identity);
        std::function<Type(const Type&, const Type&)> refine = [&](const Type& before, const Type& after) -> Type {
            if (before.kind == Type::Unknown) return after;
            if (after.kind == Type::Unknown || before == after) return before;
            if (before.kind == Type::Array && after.kind == Type::Array && before.size == after.size)
                return Type::array(refine(*before.element, *after.element), before.size);
            if (before.kind == Type::Record && after.kind == Type::Record &&
                before.declaration == after.declaration && before.fieldNames == after.fieldNames) {
                Type result = before;
                if(before.schemaObligation!=none && after.schemaObligation==none) {
                    result.schema=after.schema;result.schemaObligation=none;result.origin=after.origin;
                }
                for (std::size_t i = 0; i < before.fields.size(); ++i) result.fields[i] = refine(before.fields[i], after.fields[i]);
                return result;
            }
            throw std::logic_error("incompatible refinement of type property at " + n.identity);
        };
        type = refine(*n.inferredType, type);
        if (*n.inferredType == type) return;
    }
    n.inferredType = std::move(type);
    for (Id subscriber : n.typeUsers)
        if (subscriber % 2) enqueueType(subscriber / 2); else enqueue(subscriber / 2);
}
void Solver::typeRule(Id id) {
    auto& n = g.nodes[id];
    auto get = [&](Id dependency) { return needType(id, dependency); };
    auto flow = [&](Id dependency) -> const Completion& {
        return g.nodes[dependency].fact ? g.nodes[dependency].completion : g.nodes[dependency].typeCompletion;
    };
    auto copy = [&](Id dependency) {
        if (const Type* t = get(dependency)) {
            if (n.rule != Rule::Slot && n.rule != Rule::Load && n.rule != Rule::Input && n.rule != Rule::Type)
                n.typeCompletion = flow(dependency);
            publishType(id, *t);
        }
    };
    auto empty = [&]() { publishType(id, Type::simple(Type::Empty)); };
    auto integral = [](const Type& t) { return t.kind == Type::Exact || t.kind == Type::Integer; };
    auto numeric = [&](const Type& t) { return integral(t) || t.kind == Type::Fixed || t.kind == Type::Float; };
    auto all = [&]() {
        bool ready = true;
        for (Id input : n.inputs) if (!get(input)) ready = false;
        return ready;
    };
    const Node* syntax = n.site == none ? nullptr : &g.nodes[n.site];
    if (n.rule == Rule::Unary || n.rule == Rule::Binary ||
        (n.rule == Rule::Array && (n.site==none || hasCompletion(n.site,n.scope))) ||
        n.rule == Rule::Index || n.rule == Rule::Update || n.rule == Rule::Cast || n.rule == Rule::Adapt) {
        n.typeCompletion = {};
        for (Id input : n.inputs) {
            if (!get(input)) return;
            n.typeCompletion.include(flow(input));
            if (!flow(input).normal) { n.typeCompletion.normal = false; empty(); return; }
        }
    }
    switch (n.rule) {
    case Rule::Math: mathRule(id,true);return;
    case Rule::CustomIteration: case Rule::IterationBody:
    case Rule::Sequence: case Rule::FunctionResult: case Rule::ExitJoin: case Rule::Exit: case Rule::Resume: case Rule::Evaluate:
        completionRule(id,true); return;
    case Rule::Alias: fail(id,"receiver place is not a standalone type");
    case Rule::CallProbe: publishType(id,Type::simple(Type::Boolean)); return;
    case Rule::Deferred: {
        if (n.expansion == none) n.expansion = build(n.site,n.scope,n.previous).value;
        copy(n.expansion); return;
    }
    case Rule::ShapeCheck: empty(); return;
    case Rule::Probe: publishType(id, Type::simple(Type::Boolean)); return;
    case Rule::TypeJoin: case Rule::Construct: case Rule::Tuple: case Rule::FieldIndex: case Rule::Unpack:
        recordRule(id, true); return;
    case Rule::Constant: throw std::logic_error("constant without facts");
    case Rule::Type:
        rule(id); return;
    case Rule::Loop: case Rule::Iterate: case Rule::Repeat:
        if (!hasCompletion(n.site,n.scope)) {empty();return;}
        if (n.signatureOrigin!=none) {loopTypeRule(id);return;}
        if(n.signature==none) {
            Id projection=g.semantic(n.rule,n.site,n.inputs,n.scope);
            auto& proof=g.nodes[projection];proof.previous=n.previous;proof.text=n.text;proof.signatureOrigin=id;
            n.signature=projection;
        }
        if(get(n.signature)) {n.typeCompletion=flow(n.signature);empty();}
        return;
    case Rule::Entry: case Rule::Require: case Rule::LoopHeader: case Rule::LoopExit: case Rule::CallState:
        empty(); return;
    case Rule::Step: case Rule::Store: case Rule::Finish: {
        // A function without a possible return transfer was built as Finish.
        // Its interface needs the result type, not the interfaces of every
        // earlier statement. Value/effect demand still traverses its state.
        if(n.rule==Rule::Finish && n.region==id) {
            const auto& owner=g.nodes[g.scopes[n.scope].node];
            // A projected value also has a purity obligation. Its statement
            // completion evidence must remain available to invariant checks.
            if(owner.region!=none && g.nodes[owner.region].rule!=Rule::PropertyCall) {
                copy(n.inputs[0]);return;
            }
        }
        Id state = n.inputs[n.rule == Rule::Finish ? 1 : 0], value = n.inputs[n.rule == Rule::Finish ? 0 : 1];
        if (!get(state)) return;
        n.typeCompletion = flow(state);
        if (!n.typeCompletion.normal) { empty(); return; }
        const Type* t = get(value); if (!t) return;
        n.typeCompletion.include(flow(value)); n.typeCompletion.normal = flow(value).normal;
        if (n.rule == Rule::Step || !n.typeCompletion.normal) empty(); else publishType(id,*t);
        return;
    }
    case Rule::StateOf: {
        Id operation = n.inputs[0];
        if (!get(operation)) return;
        n.typeCompletion = flow(operation);
        Id previous = g.nodes[operation].previous;
        if (previous != none) {
            if (!get(previous)) return;
            n.typeCompletion.include(flow(previous)); n.typeCompletion.normal = n.typeCompletion.normal && flow(previous).normal;
        }
        empty(); return;
    }
    case Rule::Slot:
    case Rule::Input: {
        if (n.annotation == none) { copy(n.inputs.at(0)); return; }
        const Type* declared = get(n.annotation); if (!declared) return;
        if (declared->settled() || n.inputs.empty()) { publishType(id,*declared); return; }
        const Type* supplied = get(n.inputs[0]); if (!supplied) return;
        std::function<Type(const Type&,const Type&)> fill = [&](const Type& target,const Type& input) -> Type {
            Type required=resolvedType(target),offered=resolvedType(input);
            if(!(required==target) || !(offered==input)) return fill(required,offered);
            // A promise is a requirement to solve, not an unconstrained hole.
            if (target.kind == Type::Unknown) return target.obligation==none ? input : target;
            if(!target.settled() && input.kind==Type::Unknown) requireTypeProperties(input);
            if (target.kind == Type::Record && input.kind == Type::Record && target.declaration == input.declaration) {
                Type result = target;
                for (std::size_t i = 0; i < result.fields.size(); ++i) result.fields[i] = fill(result.fields[i],input.fields[i]);
                return result;
            }
            if (target.kind == Type::Array && input.kind == Type::Array && target.size == input.size)
                return Type::array(fill(*target.element,*input.element),target.size);
            if (!target.settled()) fail(id,"generic annotation does not match initializer type");
            return target;
        };
        publishType(id,fill(*declared,*supplied)); return;
    }
    case Rule::PropertyLoad: case Rule::Load: case Rule::GlobalRead:
        copy(n.slot); return;
    case Rule::LoopValue:
    case Rule::Sort:
    case Rule::PropertyValue:
        copy(n.inputs[0]); return;
    case Rule::PropertyCall:
        copy(n.signatureOrigin); return;
    case Rule::LoopItem: {
        if (const Type* array = get(g.nodes[n.region].inputs[0])) {
            if (array->kind != Type::Array) fail(id, "iteration requires an array", Diagnostic::NotIterable);
            publishType(id, *array->element);
        }
        return;
    }
    case Rule::Global: {
        if (g.options.strictness>=1 && syntax->annotation==none && syntax->key!="process-binding")
            fail(id,"strictness 1 requires an explicit type for a new global: "+syntax->text);
        globalTypeAliases(id);
        if (syntax->annotation != none) {
            const Type* declared = get(type(syntax->annotation,n.scope,n.previous)); if (!declared) return;
            // A query can expose independently known fields. Bare generic
            // holes still need their initializer context before publication:
            // unlike pending properties, they have no producer to subscribe to.
            std::function<bool(const Type&)> bound=[&](const Type& value) {
                if(value.kind==Type::Unknown) return value.obligation!=none;
                if(value.kind==Type::Array) return bound(*value.element);
                if(value.kind==Type::Record) for(const Type& field:value.fields) if(!bound(field)) return false;
                return true;
            };
            if(bound(*declared)) {
                publishType(id,*declared);
                // A direct alias join already includes every initializer's
                // property. Other equations may still need their witnesses.
                if(n.signature!=none && g.nodes[n.signature].key=="alias-properties") return;
            }
            if(declared->settled()) return;
        }
        prepareGlobal(id);
        copy(n.expansion); return;
    }
    case Rule::Bind: {
        if (n.inputs.empty()) {
            if (Id local=constant(n.text,n.scope);local!=none) n.inputs.push_back(local);
            else if (auto target = g.targetFacts.find(n.text); target != g.targetFacts.end()) n.inputs.push_back(target->second);
            else {
                auto found = g.symbols.find(n.text);
                if (found == g.symbols.end()) fail(id, "undefined identifier: " + n.text, Diagnostic::UndefinedName);
                if (found->second.size() != 1 || g.nodes[found->second[0]].syntax != Syntax::Global)
                    fail(id, "function names are not values: " + n.text);
                n.inputs.push_back(global(found->second[0]));
            }
        }
        copy(n.inputs[0]); return;
    }
    case Rule::Cast: case Rule::Adapt: {
        if (!all()) return;
        const Type& from = *get(n.inputs[0]); const Type& to = *get(n.inputs[1]);
        if(n.rule==Rule::Cast && !to.settled() && from.kind==Type::Array &&
            *from.element==Type::integer(8,false)) return;
        // Concreteness/representability of an exact value is a value obligation;
        // a type query does not evaluate its arithmetic to obtain that value.
        Type result = conversionType(from,to,n.rule == Rule::Cast,id);
        publishType(id, result); return;
    }
    case Rule::Choice:
        copy(n.inputs[1]); return;
    case Rule::Solve:
        // The solve region's result may need its constraints to determine shape.
        rule(id); return;
    case Rule::Resource:
        resourceRule(id,true);return;
    case Rule::Unary: {
        const Type* t = get(n.inputs[0]); if (!t) return;
        if (n.text == "!") {
            if (!integral(*t) && t->kind != Type::Boolean) fail(id, "logical operand must be boolean");
            publishType(id, Type::simple(Type::Boolean)); return;
        }
        if (n.text == "|" && (t->kind == Type::Array || t->kind == Type::String)) {
            publishType(id, Type::simple(Type::Exact)); return;
        }
        if (!numeric(*t)) fail(id, "unary operator requires a number");
        if (t->kind == Type::Float && n.text != "+" && n.text != "-" && n.text != "|")
            fail(id,"floating-point unary operator must be +, - or absolute value");
        publishType(id, *t); return;
    }
    case Rule::Binary: {
        if (n.text.starts_with('.') && n.text != "..") { broadcastRule(id, true); return; }
        if (!all()) return;
        if (n.text == "..") { rule(id); return; }
        const Type& a = *get(n.inputs[0]); const Type& b = *get(n.inputs[1]);
        if (n.text == "&&" || n.text == "||") {
            if ((a.kind != Type::Boolean && a.kind != Type::Exact) || (b.kind != Type::Boolean && b.kind != Type::Exact))
                fail(id, "logical operands must be boolean");
            publishType(id, Type::simple(Type::Boolean)); return;
        }
        bool cmp = n.text == "==" || n.text == "!=" || n.text == "<" || n.text == "<=" || n.text == ">" || n.text == ">=";
        bool shift = n.text == "<<" || n.text == ">>";
        Type t;
        if (cmp && a.kind == Type::Array && b.kind == Type::Array) {
            (void)common(*a.element, *b.element, id); t = Type::simple(Type::Boolean);
        } else {
            t = shift ? a : common(a, b, id);
            if (!cmp && (!numeric(t) || !numeric(b))) fail(id, "arithmetic operands must be numbers");
            if (t.kind == Type::Float && !cmp && n.text != "+" && n.text != "-" && n.text != "*" && n.text != "/" && n.text != "%")
                fail(id,"unsupported floating-point operator: "+n.text);
            if (shift && !integral(b)) fail(id,"shift count must be an integer");
            if (cmp) t = Type::simple(Type::Boolean);
        }
        publishType(id, t); return;
    }
    case Rule::Array: {
        Type element;
        if(!n.inputs.empty()) {
            if(n.signature==none) {
                n.signature=g.semantic(Rule::TypeJoin,n.site,n.inputs,n.scope);
                g.nodes[n.signature].key="array-element";
            }
            element.obligation=n.signature;
        } else if(n.annotation!=none) {
            element.obligation=n.annotation;
        }
        publishType(id, Type::array(element, n.inputs.size())); return;
    }
    case Rule::Index: {
        if (!all()) return;
        const Type& a = *get(n.inputs[0]); const Type& index = *get(n.inputs[1]);
        if (!integral(index)) fail(id, "array index must be an integer");
        if (a.kind == Type::Record) {
            const Fact* position = need(id, n.inputs[1]); if (!position) return;
            if (position->value.kind != Value::Number || position->value.number < 0 || position->value.number >= a.fields.size())
                fail(id, "record field index must be a valid compile-time position");
            const Type& field = a.fields[position->value.number.convert_to<std::size_t>()];
            if (field.kind == Type::Unknown && field.obligation != none) copy(field.obligation);
            else publishType(id, field);
        }
        else if (a.kind == Type::Array) {
            if(a.element->kind==Type::Unknown && a.element->obligation!=none) copy(a.element->obligation);
            else publishType(id,*a.element);
        }
        else if (a.kind == Type::String) publishType(id, Type::integer(8, false));
        else fail(id, "indexing requires an array or string");
        return;
    }
    case Rule::Update:
        copy(n.inputs[0]); return;
    case Rule::Merge: {
        if (n.text == "state") { empty(); return; }
        const Type* a = get(n.inputs[1]); const Type* b = get(n.inputs[2]);
        if (a && b) publishType(id, common(*a, *b, id));
        return;
    }
    case Rule::ShortCircuit: {
        if(logicalRule(id,true)) return;
        const Type* left = get(n.inputs[0]); if (!left) return;
        if (left->kind != Type::Boolean && left->kind != Type::Exact) fail(id, "logical operand must be boolean");
        if (n.branches.empty()) {
            n.typeExpanded = true;
            const auto& known = g.nodes[n.inputs[0]].fact;
            if (known && known->value.kind == Value::Number && (known->value.number != 0) == (n.text == "||")) {
                publishType(id, Type::simple(Type::Boolean)); return;
            }
            // A type-only RHS has its own structural graph and no state demand.
            Built rhs = build(syntax->children[1], n.scope, n.previous);
            n.branches.push_back(rhs.value); n.ends.push_back(rhs.state);
        }
        const Type* right = get(n.branches[0]); if (!right) return;
        if (right->kind != Type::Boolean && right->kind != Type::Exact) fail(id, "logical operand must be boolean");
        publishType(id, Type::simple(Type::Boolean)); return;
    }
    case Rule::Select: {
        const Type* condition = get(n.inputs[0]); if (!condition) return;
        if (condition->kind != Type::Boolean && condition->kind != Type::Exact) fail(id, "condition must be boolean");
        if (signatureSpecialization(n.scope)) {
            const Fact* guard = need(id,n.inputs[0]); if (!guard) return;
            if (guard->value.symbolic()) requestChoice(guard->value,id);
            if (guard->value.kind != Value::Number || guard->effect >= Effect::Control)
                fail(id,"recursive return shape requires a pure compile-time condition");
        }
        // Share expansion with the value rule when already present.
        if (n.branches.empty()) {
            n.typeExpanded = true;
            const auto& known = g.nodes[n.inputs[0]].fact;
            bool determined = known && known->value.kind == Value::Number;
            std::vector<std::size_t> arms = determined ? std::vector<std::size_t>{known->value.number != 0 ? 1U : 2U} :
                std::vector<std::size_t>{1,2};
            for (auto arm : arms) {
                Id scope = g.scope(n.scope, n.site, n.identity + "/type-arm" + std::to_string(arm));
                Built b = arm < syntax->children.size() ? build(syntax->children[arm], scope, n.previous) : Built{unit(n.site), n.previous};
                n.branches.push_back(b.value); n.ends.push_back(b.state);
            }
        }
        bool ready = true;
        std::vector<std::optional<Type>> types;
        std::vector<std::optional<Error>> errors;
        for (Id branch : n.branches) {
            try {
                const Type* t = get(branch); if (!t) ready = false;
                types.push_back(t ? std::optional<Type>{*t} : std::optional<Type>{});
                errors.push_back({});
            } catch (const Error& error) { types.push_back({}); errors.push_back(error); }
        }
        // Matching aggregate structure can expose each field or element type
        // separately. A condition needed by one property need not block another.
        if(ready && types.size()==2 && types[0] && types[1] && !errors[0] && !errors[1] &&
           flow(n.branches[0]).normal && flow(n.branches[1]).normal &&
           flow(n.branches[0]).exits.empty() && flow(n.branches[1]).exits.empty() &&
           ((types[0]->kind==Type::Record && types[1]->kind==Type::Record && types[0]->declaration==types[1]->declaration && types[0]->fieldNames==types[1]->fieldNames) ||
            (types[0]->kind==Type::Array && types[1]->kind==Type::Array && types[0]->size==types[1]->size))) {
            if(n.signature==none) {
                n.signature=g.semantic(Rule::TypeJoin,n.site,{n.inputs[0],n.branches[0],n.branches[1]},n.scope);
                g.nodes[n.signature].key="select-type";
            }
            copy(n.signature);return;
        }
        bool needGuard = std::any_of(errors.begin(), errors.end(), [](const auto& error) { return error.has_value(); });
        if (types.size() == 2 && types[0] && types[1] && flow(n.branches[0]).normal && flow(n.branches[1]).normal && !(*types[0] == *types[1])) needGuard = true;
        if (needGuard) {
            const Fact* guard = need(id, n.inputs[0]); if (!guard) return;
            if (guard->value.symbolic()) requestChoice(guard->value,id);
            if (guard->value.kind == Value::Number) {
                std::size_t selected = n.branches.size() == 1 ? 0 : guard->value.number != 0 ? 0 : 1;
                if (errors[selected]) throw *errors[selected];
                if (!types[selected]) return;
                n.typeCompletion = flow(n.branches[selected]);
                publishType(id, syntax->children.size() == 2 ? Type::simple(Type::Empty) : *types[selected]); return;
            }
            for (const auto& error : errors) if (error) throw *error;
        }
        if (!ready) return;
        n.typeCompletion.normal = false; n.typeCompletion.exits.clear();
        for (Id branch : n.branches) {
            n.typeCompletion.include(flow(branch)); n.typeCompletion.normal = n.typeCompletion.normal || flow(branch).normal;
        }
        if (!n.typeCompletion.normal) { empty(); return; }
        if (syntax->children.size() == 2) { empty(); return; }
        Type t;
        for (Id branch : n.branches) if (flow(branch).normal) t = common(t, *get(branch), id);
        publishType(id, t); return;
    }
    case Rule::Invoke: {
        if (!resolveCall(id, true)) return;
        if (n.rule != Rule::Invoke) { typeRule(id); return; }
        const auto& fn = g.nodes[n.body];
        if (fn.external) { if (fn.annotation != none) copy(type(fn.annotation,n.lexicalScope,n.lexicalScope==none ? none : n.previous)); else empty(); return; }
        std::set<Id> visiting;
        std::function<bool(Id)> generic=[&](Id annotation) {
            const auto& source=g.nodes[annotation];
            if(source.body!=none) return false;
            if(source.text=="(tuple)") return std::any_of(source.children.begin(),source.children.end(),generic);
            Id declaration=namedType(source.text,n.lexicalScope);
            if(declaration==none || !visiting.insert(declaration).second) return false;
            for(Id field:g.nodes[declaration].children) {
                Id fieldType=g.nodes[field].annotation;
                if(fieldType==none || dependentField(g,declaration,fieldType) || generic(fieldType)) return true;
            }
            return false;
        };
        if (fn.annotation != none && !generic(fn.annotation) && g.nodes[fn.annotation].body == none && g.nodes[fn.annotation].children.empty()) {
            const Type* declared = get(type(fn.annotation,n.lexicalScope,n.lexicalScope == none ? none : n.previous));
            if (!declared) return;
            if(fn.noinline) requireTypeProperties(*declared);
            if (declared->settled()) { publishType(id,*declared); return; }
        }
        if (n.signatureOrigin == none && n.expansion == none) {
            if (n.signature == none) {
                n.signature = g.semantic(Rule::Invoke,n.site,n.inputs,n.scope);
                auto& projection = g.nodes[n.signature];
                projection.text = n.text; projection.body = n.body; projection.previous = n.previous;
                projection.receivers = n.receivers; projection.receiverCount = n.receiverCount;
                projection.lexicalScope = n.lexicalScope; projection.candidates = n.candidates;
                projection.signatureOrigin = id;
            }
            copy(n.signature); return;
        }
        if (n.expansion == none) instantiate(id, n.body);
        if (n.expansion == none) return;
        if (n.runtimeBackedge != none) {
            auto interface = recursiveInterfaces.find(g.nodes[n.runtimeBackedge].key);
            if (interface != recursiveInterfaces.end()) { publishType(id,interface->second); return; }
            // A declared result is an independent interface equation, including
            // queries and dimensions. Use the owning activation's annotation:
            // it contains the formal bindings and the correct reaching state.
            // Demanding the entire Finish here would wait for this backedge.
            if(fn.annotation!=none) {
                const auto& result=g.nodes[n.expansion];
                Id annotation=result.annotation;
                if(annotation==none && result.rule==Rule::Finish && !result.inputs.empty()) {
                    const auto& converted=g.nodes[result.inputs[0]];
                    if(converted.rule==Rule::Adapt) annotation=converted.inputs[1];
                }
                if(annotation!=none) {
                    const Type* declared=get(annotation);if(!declared) return;
                    requireTypeProperties(*declared);
                    if(declared->settled()) {publishType(id,*declared);return;}
                }
            }
            // A backedge is a type equation, not an unconditional subscription
            // to a partially constructed result (which could grow forever).
            return;
        }
        copy(n.expansion); return;
    }
    case Rule::None: throw std::logic_error("structural node demanded as a type");
    }
    throw std::logic_error("missing type rule");
}
} // namespace vx
