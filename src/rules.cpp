#include "vexel.hpp"
#include <algorithm>
#include <sstream>

namespace vx {
namespace {
Effect effects(const Fact& a, const Fact& b) { return std::max(a.effect, b.effect); }
bool integer(const Type& t) { return t.kind == Type::Integer || t.kind == Type::Exact; }
std::optional<unsigned> width(const std::string& name, unsigned maximum) {
    if (name.size() < 2 || (name[0] != 'i' && name[0] != 'u')) return {};
    unsigned n = 0;
    for (std::size_t i = 1; i < name.size(); ++i) {
        if (name[i] < '0' || name[i] > '9') return {};
        unsigned d = static_cast<unsigned>(name[i] - '0');
        if (n > maximum/10 || (n==maximum/10 && d>maximum%10)) return {};
        n = n * 10 + d;
    }
    if (!n || n > maximum) return {};
    return n;
}
}
void Solver::instantiate(Id id, Id function) {
    auto& n = g.nodes[id]; const auto& fn = g.nodes[function];
    const bool signatureOnly = n.signatureOrigin != none;
    for(Id param:fn.children) {
        const auto& name=g.nodes[param].text;
        bool shadows=g.symbols.contains(name) || constant(name,n.lexicalScope)!=none || slot(name,n.lexicalScope)!=none || !functions(name,n.lexicalScope).empty();
        for(Id scope=n.lexicalScope;scope!=none;scope=g.scopes[scope].parent)
            shadows=shadows || g.scopes[scope].expressions.contains(name);
        if(shadows) fail(param,"parameter shadows an enclosing symbol: "+name);
    }
    // Conversion of an rvalue receiver creates a fresh typed location. A bound
    // source location must retain its type; overload matching checks that case.
    for(std::size_t i=0;i<fn.receiverCount;++i) {
        const auto& receiver=g.nodes[n.receivers[i]];
        Id annotation=g.nodes[fn.children[i]].annotation;
        if(receiver.key!="temporary" || annotation==none) continue;
        Id target=parameterType(id,function,i,n.lexicalScope);
        const Type* source=needType(id,n.inputs[i]);const Fact* declared=need(id,target);
        if(!source || !declared) return;
        Type converted=conversionType(*source,declared->type,false,id);
        if(converted==*source) continue;
        Id value=adapt(n.inputs[i],target,n.site);
        Id location=g.semantic(Rule::Slot,n.site,{value},n.scope);
        Id state=g.semantic(Rule::Store,n.site,{n.previous,value},n.scope);
        g.nodes[state].slot=location;g.nodes[state].previous=n.previous;
        Id alias=g.semantic(Rule::Alias,n.site,{},n.scope);g.nodes[alias].slot=location;g.nodes[alias].key="converted-temporary";
        n.inputs[i]=value;n.receivers[i]=alias;n.previous=state;
        if(signatureOnly) {if(!needType(id,value)) return;}
        else if(!need(id,value)) return;
    }
    std::vector<Fact> args;
    std::string key = g.identity(function) + "(";
    bool runtime = false;
    const bool finiteContext = n.scope != none && g.scopes[n.scope].puzzle != none;
    bool sharedState = n.lexicalScope != none || fn.receiverCount != 0;
    std::set<Id> captures;
    auto capture = [&](Id variable) {
        if (g.nodes[variable].rule == Rule::Alias) variable = g.nodes[variable].slot;
        if (g.nodes[variable].rule==Rule::Global) return;
        captures.insert(variable);
    };
    for (Id scope = n.lexicalScope; scope != none; scope = g.scopes[scope].parent)
        for (const auto& [name,variable] : g.scopes[scope].slots) { (void)name; capture(variable); }
    for (Id receiver : n.receivers) capture(receiver);
    std::set<Id> writtenCaptures;
    // Runtime recursion and independent property reads need the same write
    // proof. Implicit operator calls can mutate captures just like named calls;
    // preserving a specialization constant across such a backedge is unsound.
    if(sharedState) for(Id variable:captures)
        if(mayWrite(fn.body,n.lexicalScope,variable)) writtenCaptures.insert(variable);
    for (Id receiver : n.receivers)
        if (g.nodes[g.nodes[receiver].slot].rule!=Rule::Global) writtenCaptures.insert(g.nodes[receiver].slot);
    for (std::size_t i = 0; i < n.inputs.size(); ++i) {
        if (g.nodes[n.inputs[i]].rule == Rule::Deferred) {
            args.push_back({}); sharedState = true; key += "expression@" + g.nodes[n.inputs[i]].identity; continue;
        }
        Fact arg = g.nodes[n.inputs[i]].fact ? *g.nodes[n.inputs[i]].fact :
            Fact{*g.nodes[n.inputs[i]].inferredType, Value::runtime(), Effect::Pure};
        Id annotation = g.nodes[fn.children[i]].annotation;
        if (annotation != none) {
            Id requirement=parameterType(id,function,i,n.lexicalScope);
            const Fact* target=need(id,requirement);if(!target) return;
            Type supplied=arg.type;
            if (signatureOnly && !arg.value.known()) arg.type = conversionType(arg.type,target->type,false,id);
            else arg = convert(arg,target->type,false,id);
            if(!signatureOnly && (arg.type.kind==Type::Record || arg.type.kind==Type::Array) && !(supplied==arg.type)) {
                // A converted aggregate is a new value with its own invariants.
                // Keep the conversion at the call site, before materialization.
                n.inputs[i]=adapt(n.inputs[i],requirement,n.site);
                if(!need(id,n.inputs[i])) return;
            }
        }
        arg.effect = Effect::Pure;
        runtime = runtime || !arg.value.known();
        key += arg.type.str() + ":" + arg.value.str();
        if (!arg.value.known()) key += "@" + g.nodes[n.inputs[i]].identity;
        key += ";"; args.push_back(std::move(arg));
    }
    key += ")";
    if (sharedState) key += "/state" + std::to_string(n.previous);
    if (signatureOnly) key += "/signature";
    if (n.scope != none && g.scopes[n.scope].puzzle != none) {
        key += "/solve" + std::to_string(g.scopes[n.scope].puzzle);
        key += "/guard" + g.scopes[n.scope].guard.str();
    }
    n.key = key;
    n.valueDependentSignature = signatureOnly && (specializedSignatures.contains(key) || signatureSpecialization(n.scope));
    if (auto it = pureCalls.find(key); !signatureOnly && !finiteContext && !sharedState && it != pureCalls.end()) {
        n.expansion = it->second; ++cacheHits; return;
    }
    // Only ancestor invocations can introduce a recursive backedge. Independent
    // calls never share unfinished effectful bodies.
    auto ancestor = [&](Id scopeId) {
        const auto& scope = g.scopes[scopeId];
        return g.nodes[scope.node].slot != none ? scope.caller : scope.parent != none ? scope.parent : scope.caller;
    };
    std::vector<Id> cycle;
    for (Id scopeId = n.scope; scopeId != none;
         scopeId = ancestor(scopeId)) {
        const auto& owner = g.nodes[g.scopes[scopeId].node];
        if (owner.slot != none && (g.nodes[owner.region].signatureOrigin != none) != signatureOnly) continue;
        if (owner.slot != none) cycle.push_back(owner.slot);
        if (owner.slot != function) continue;
        if (!fn.noinline && !fn.exported) fail(id, "recursive calls require &~ on '" + fn.text + "'");
        for (Id member : cycle) if (!g.nodes[member].noinline && !g.nodes[member].exported)
            fail(id,"recursive calls require &~ on '"+g.nodes[member].text+"'");
        if (signatureOnly) {
            if (n.valueDependentSignature) continue;
            const auto& target = g.nodes[owner.region];
            bool compatible = true;
            for (std::size_t i = 0; i < args.size(); ++i) {
                const Type* before = needType(id,target.carried.at(i)); if (!before) return;
                if (!(*before == args[i].type)) compatible = false;
            }
            if (!compatible) continue;
            n.runtimeBackedge = owner.region; n.expansion = owner.body; n.carried = target.carried;
            return;
        }
        const bool residualControl = n.scope != none && g.scopes[n.scope].runtimeControl;
        if (runtime || residualControl) {
            for (const auto& arg : args) if (arg.value.symbolic()) requestChoice(arg.value,id);
            auto& target = g.nodes[owner.region];
            bool targetRuntime = false, compatible = true;
            for (std::size_t i = 0; i < args.size(); ++i) {
                const Fact* supplied = need(id,target.carried.at(i)); if (!supplied) return;
                const auto& before = *supplied;
                targetRuntime = targetRuntime || !before.value.known();
                if (!(before.type == args[i].type)) compatible = false;
            }
            if ((!targetRuntime && !residualControl) || !compatible) continue;
            for (std::size_t i = fn.receiverCount; i < args.size(); ++i) {
                const auto& before = *g.nodes[target.carried[i]].fact;
                if (before.value.known() && !(before.value == args[i].value)) {
                    if (before.type.concrete()) throw RecursionWidening{function,i,false,owner.region};
                    compatible = false;
                }
            }
            if (!compatible) continue;
            for (Id variable : writtenCaptures) {
                if (std::find(target.environment.begin(),target.environment.end(),variable) != target.environment.end()) continue;
                const Type* type = needType(id,variable); if (!type) return;
                if (!type->concrete()) fail(id,"recursive captured storage requires a concrete type");
                throw RecursionWidening{function,g.nodes[variable].site,true,owner.region};
            }
            n.runtimeBackedge = owner.region; n.expansion = owner.body; n.carried = target.carried;
            n.environment = target.environment; n.seeds = target.seeds;
            if (!n.environment.empty()) {
                n.endState = g.semantic(Rule::CallState,n.site,{},n.scope);
                g.nodes[n.endState].region = id; g.nodes[n.endState].previous = n.previous;
            }
            return;
        }
        if (owner.key == key) {
            n.expansion = owner.body;
            if (n.expansion == none) throw std::logic_error("recursive invocation has no return obligation");
            return;
        }
    }
    Id frame = g.scope(n.lexicalScope, function, finiteContext ? key + "/callsite/" + n.identity : key);
    g.scopes[frame].caller = n.scope;
    g.scopes[frame].loop = none;
    if (n.scope != none) {
        g.scopes[frame].puzzle = g.scopes[n.scope].puzzle;
        g.scopes[frame].guard = g.scopes[n.scope].guard;
        g.scopes[frame].runtimeControl = g.scopes[n.scope].runtimeControl;
    }
    auto& owner = g.nodes[g.scopes[frame].node]; owner.key = key; owner.slot = function; owner.region = id;
    Id state = g.semantic(Rule::Entry, function, {}, frame);
    if (sharedState) {
        g.nodes[state].previous = n.previous; g.nodes[state].region = id;
        if (!signatureOnly && (runtime || recursiveRuntimeInstances.contains(key) || (n.scope != none && g.scopes[n.scope].runtimeControl))) for (Id variable : captures) if (runtimeCaptures.contains({function,g.nodes[variable].site})) {
            n.environment.push_back(variable); n.seeds.push_back(load(variable,n.previous,n.site));
        }
    }
    n.carried = n.inputs;
    for (std::size_t i = 0; i < fn.children.size(); ++i) {
        Id param = fn.children[i];
        if (i < fn.receiverCount) {
            g.scopes[frame].slots[g.nodes[param].text] = n.receivers.at(i); continue;
        }
        if (g.nodes[param].expression) {
            const auto& argument = g.nodes[n.inputs[i]];
            g.scopes[frame].expressions[g.nodes[param].text] = {argument.site,argument.scope,g.nodes[param].annotation,
                fn.text == ".@" || fn.text == ".@@"};
            continue;
        }
        Id supplied = n.inputs[i];
        if (!signatureOnly && (runtime || recursiveRuntimeInstances.contains(key)) && args[i].type.concrete() && runtimeParameters.contains({function,i}))
            supplied = literal(param,args[i].type,Value::runtime());
        // The closure's formal inputs include specialization constants. Backend
        // parameters are exactly those formal inputs whose values remain open.
        Id input = g.semantic(Rule::Input, param, {supplied}, frame);
        if (g.nodes[param].annotation != none) g.nodes[input].annotation = parameterType(id,function,i,n.lexicalScope);
        n.carried[i] = input;
        Id variable = g.semantic(Rule::Slot, param, {input}, frame);
        g.nodes[variable].text = g.nodes[param].text;
        g.scopes[frame].slots[g.nodes[param].text] = variable;
        Id store = g.semantic(Rule::Store, param, {state, input}, frame);
        g.nodes[store].slot = variable; g.nodes[store].previous = state;
        state = store;
    }
    // Build the return obligation first, so later recursive rule firings can
    // point to it without evaluating bodies on the C++ call stack.
    bool completion = hasCompletion(fn.body,frame);
    Id output = g.semantic(completion ? Rule::FunctionResult : Rule::Finish, function, {}, frame);
    g.scopes[frame].function = output;
    owner.body = output; n.expansion = output; ++invocations;
    Built body = block(fn.body, frame, state, true);
    Id value = body.value;
    if (fn.annotation != none) {
        Id supplied=value,returnState=body.state;
        if(completion) {
            supplied=g.semantic(Rule::FunctionResult,function,{value,body.state},frame);
            g.nodes[supplied].region=output;
            returnState=resume(body.state,output,"function",function,frame);
        }
        Id target=argumentType(fn.annotation,frame,returnState,supplied);
        if(completion) g.nodes[output].annotation=target;
        else value=adapt(value,target,function);
    }
    g.nodes[output].region = output;
    g.nodes[output].inputs = {value, body.state};
    if (sharedState) n.endState = completion ? resume(body.state,output,"function",function,frame) : body.state;
}
bool Solver::resolveCall(Id id, bool typesOnly) {
    auto& n = g.nodes[id];
    const auto visible=functions(n.text,n.scope);
    auto lexical=[&](Id declaration) {
        for (const auto& candidate:visible) if (candidate.declaration==declaration) return candidate.scope;
        return n.lexicalScope;
    };
    auto fallback = [&]() {
        if (n.site == none) return false;
        const auto& source = g.nodes[n.site];
        if (!source.key.starts_with("fallback-")) return false;
        n.rule = source.key == "fallback-unary" ? Rule::Unary : source.key == "fallback-binary" ? Rule::Binary : Rule::Iterate;
        n.text = source.text.substr(1); n.endState = n.previous;
        if (n.rule == Rule::Iterate) n.inputs.resize(1);
        return true;
    };
    for (Id input : n.inputs) {
        if (g.nodes[input].rule == Rule::Deferred) continue;
        if (typesOnly) { if (!needType(id, input)) return false; }
        else if (!need(id, input)) return false;
    }
        if (n.candidates.empty()) {
            auto it = g.symbols.find(n.text);
            if (it == g.symbols.end()) { if (fallback()) return true; fail(id, "undefined function: " + n.text, Diagnostic::UndefinedFunction); }
            for (Id fn : it->second)
                if (g.nodes[fn].syntax == Syntax::Function && g.nodes[fn].children.size() == n.inputs.size() &&
                    g.nodes[fn].receiverCount == n.receiverCount) n.candidates.push_back(fn);
            if (n.candidates.empty()) { if (fallback()) return true; fail(id, "no matching overload (name or arity): " + n.text, Diagnostic::NoOverload); }
        }
        if (n.body == none) {
            bool ready = true;
            // Nominal structure is independent of a schema's lexical values.
            // Reject a different family before demanding its capture closure.
            std::function<bool(Id,const Type&,Id)> structure=[&](Id annotation,const Type& supplied,Id scope) {
                const auto& syntax=g.nodes[annotation];
                if(syntax.body!=none) return true;
                Type current=resolvedType(supplied);
                auto known=[&]() {if(current.kind==Type::Unknown) requireTypeProperties(current);};
                known();
                if(syntax.text=="(tuple)") {
                    if(current.kind!=Type::Record || current.declaration!=none || current.fields.size()!=syntax.children.size()) return false;
                    for(std::size_t i=0;i<syntax.children.size();++i)
                        if(!structure(syntax.children[i],current.fields[i],scope)) return false;
                    return true;
                }
                for(Id dimension:syntax.children) {
                    (void)dimension;known();if(current.kind!=Type::Array) return false;
                    Type element=*current.element;current=resolvedType(element);
                }
                if(Id declaration=namedType(syntax.text,scope);declaration!=none) {
                    known();
                    if(current.kind!=Type::Record || current.declaration!=declaration) return false;
                    for(std::size_t i=0;i<current.fields.size();++i) {
                        Id field=g.nodes[g.nodes[declaration].children[i]].annotation;
                        if(field!=none && !dependentField(g,declaration,field) && !structure(field,current.fields[i],scope)) return false;
                    }
                }
                return true;
            };
            std::vector<Id> applicable;
            for (Id fn : n.candidates) {
                if (g.nodes[fn].children.size() != n.inputs.size()) fail(id, "wrong argument count for " + n.text);
                bool matches=true;
                for(std::size_t i=0;i<n.inputs.size();++i) {
                    Id annotation=g.nodes[g.nodes[fn].children[i]].annotation;if(annotation==none) continue;
                    const Type* supplied=needType(id,n.inputs[i]);
                    if(!supplied) {ready=false;matches=false;continue;}
                    if(!structure(annotation,*supplied,lexical(fn))) {matches=false;break;}
                }
                if(!matches) continue;
                applicable.push_back(fn);
                for (std::size_t i=0;i<g.nodes[fn].children.size();++i)
                    if (g.nodes[g.nodes[fn].children[i]].annotation != none && !need(id, parameterType(id,fn,i,lexical(fn)))) ready = false;
            }
            if (!ready) return false;
            int best = -1; std::vector<Id> matches;
            for (Id fn : applicable) {
                int score = 0; bool match = true;
                for (std::size_t i = 0; i < n.inputs.size(); ++i) {
                    Id ann = g.nodes[g.nodes[fn].children[i]].annotation;
                    if (g.nodes[n.inputs[i]].rule == Rule::Deferred && ann == none) continue;
                    const Type* argumentType = needType(id,n.inputs[i]); if (!argumentType) return false;
                    Fact arg = g.nodes[n.inputs[i]].fact ? *g.nodes[n.inputs[i]].fact :
                        Fact{*argumentType, Value::runtime(), Effect::Pure};
                    if (ann == none) {
                        if ((g.nodes[fn].external || g.nodes[fn].exported) && g.nodes[fn].key!="bundled-math") fail(fn, "ABI function parameters require types");
                        std::function<bool(const Type&)> exact = [&](const Type& t) {
                            Type current=resolvedType(t);
                            if(!(current==t)) return exact(current);
                            // An open property is not evidence that this
                            // interface contains no unconstrained literals.
                            if(t.kind==Type::Unknown) requireTypeProperties(t);
                            if (t.kind == Type::Exact) return true;
                            if (t.kind == Type::Array) return exact(*t.element);
                            return std::any_of(t.fields.begin(),t.fields.end(),exact);
                        };
                        if (g.options.strictness >= 2 && exact(arg.type)) fail(id,"strictness 2 requires concrete arguments at unconstrained call boundaries");
                        continue;
                    }
                    const auto& target = g.nodes[parameterType(id,fn,i,lexical(fn))].fact->type;
                    if(i<g.nodes[fn].receiverCount && g.nodes[n.receivers.at(i)].key!="temporary") {
                        try { if(!(conversionType(arg.type,target,false,id)==arg.type)) {match=false;break;} }
                        catch(const Error&) {match=false;break;}
                    }
                    if(n.key=="logical-dispatch") {
                        try {(void)conversionType(arg.type,target,false,id);}
                        catch(const Error&) {match=false;break;}
                    }
                    if (arg.type.kind == Type::Exact && !arg.value.known() && (!typesOnly || applicable.size() > 1 || n.key=="logical-dispatch") && g.nodes[n.inputs[i]].rule != Rule::Deferred && (n.rule != Rule::CallProbe || n.key=="logical-dispatch")) {
                        const Fact* value = need(id, n.inputs[i]); if (!value) return false;
                        arg = *value;
                    }
                    try {
                        if (typesOnly && !arg.value.known()) (void)conversionType(arg.type,target,false,id);
                        else if ((g.nodes[n.inputs[i]].rule != Rule::Deferred && n.rule != Rule::CallProbe) || arg.type.kind != Type::Exact || arg.value.known())
                            (void)convert(arg, target, false, id);
                    }
                    catch (const Error&) { match = false; break; }
                    score += arg.type == target ? 2 : 1;
                }
                if (!match) continue;
                if (score > best) { best = score; matches.clear(); }
                if (score == best) matches.push_back(fn);
            }
            if (matches.empty()) { if (fallback()) return true; fail(id, "no matching overload (argument types or representability): " + n.text, Diagnostic::NoOverload); }
            if (matches.size() > 1) fail(id, "ambiguous call to '" + n.text + "'; annotate or cast the arguments");
            n.body = matches[0];
            n.lexicalScope=lexical(n.body);
        }
    if(n.rule==Rule::Invoke && g.nodes[n.body].key=="bundled-math") {
        n.rule=Rule::Math;n.text=g.nodes[n.body].text;n.endState=n.previous;
    }
    return true;
}
void Solver::rule(Id id) {
    auto& n = g.nodes[id];
    const Node* syntax = n.site == none ? nullptr : &g.nodes[n.site];
    auto get = [&](Id dependency) { return need(id, dependency); };
    auto done = [&](Fact f) { publish(id, std::move(f)); };
    auto empty = [&](Effect effect = Effect::Pure) { done({Type::simple(Type::Empty), {}, effect}); };
    auto all = [&](const std::vector<Id>& inputs) {
        bool ready = true;
        for (Id input : inputs) if (!get(input)) ready = false;
        return ready;
    };
    if (n.signatureOrigin != none && n.rule!=Rule::PropertyCall) {
        // A dependent shape may genuinely need a value while resolving a
        // signature. Execute the original call, never the recursive type edge.
        if (const Fact* value = get(n.signatureOrigin)) {
            n.completion = g.nodes[n.signatureOrigin].completion; done(*value);
        }
        return;
    }
    if (n.previous != none && n.rule != Rule::Entry && n.rule != Rule::Type &&
        n.rule != Rule::Step && n.rule != Rule::Store && n.rule != Rule::Load &&
        n.rule != Rule::Finish && n.rule != Rule::Sequence && n.rule != Rule::Exit &&
        n.rule != Rule::Deferred && n.rule != Rule::CallProbe && n.rule != Rule::Probe && n.rule!=Rule::PropertyCall) {
        const Fact* prior = get(n.previous); if (!prior) return;
        if (!g.nodes[n.previous].completion.normal) {
            n.completion = g.nodes[n.previous].completion; n.endState = n.previous;
            empty(prior->effect); return;
        }
    }
    switch (n.rule) {
    case Rule::Math: mathRule(id);return;
    case Rule::CustomIteration: case Rule::IterationBody:
    case Rule::Sequence: case Rule::Exit: case Rule::FunctionResult: case Rule::Resume: case Rule::ExitJoin: case Rule::Evaluate:
        completionRule(id); return;
    case Rule::Alias: fail(id,"receiver place is not a standalone value");
    case Rule::CallProbe: {
        try {
            if (!resolveCall(id,true)) return;
            done({Type::simple(Type::Boolean),Value::integer(1),Effect::Pure});
        } catch (const Error& error) {
            if (error.category!=Diagnostic::UndefinedFunction && error.category!=Diagnostic::NoOverload) throw;
            done({Type::simple(Type::Boolean),Value::integer(0),Effect::Pure});
        }
        return;
    }
    case Rule::Deferred: fail(id,"unevaluated expression cannot be demanded as a value");
    case Rule::Probe: {
        const Type* receiver = needType(id, n.inputs[0]); if (!receiver) return;
        if (receiver->kind == Type::Unknown) return;
        bool present = receiver->kind == Type::Record &&
            std::find(receiver->fieldNames.begin(), receiver->fieldNames.end(), n.text) != receiver->fieldNames.end();
        done({Type::simple(Type::Boolean), Value::integer(present ? 1 : 0), Effect::Pure}); return;
    }
    case Rule::TypeJoin: case Rule::Construct: case Rule::Tuple: case Rule::FieldIndex: case Rule::Unpack:
        recordRule(id); return;
    case Rule::Solve: case Rule::Choice: case Rule::Require:
        puzzleRule(id); return;
    case Rule::Entry: empty(); return;
    case Rule::CallState:
        if (const Fact* call = get(n.region)) empty(call->effect);
        return;
    case Rule::Constant: throw std::logic_error("constant without a fact");
    case Rule::Type: {
        if(n.key=="alias-join") {
            if(processingType) {if(const Type* joined=needType(id,n.slot)) publishType(id,*joined);}
            else if(const Fact* joined=get(n.slot)) done({joined->type,{},Effect::Pure});
            return;
        }
        if (!n.expanded) {
            n.expanded = true;
            Id frame=g.scope(n.scope,n.site,n.identity+"/type-values");g.scopes[frame].typeValues=true;
            // Type expressions evaluate their own pure dependencies. Earlier
            // runtime statements are a reaching-state source for named reads,
            // not a prerequisite for a literal dimension or a signature query.
            Id state=g.semantic(Rule::Entry,n.site,{},frame);g.nodes[state].previous=n.previous;
            if (syntax->body != none) n.annotation = build(syntax->body, frame, state).value;
            for (Id dimension : syntax->children) {
                if (syntax->text == "(tuple)") {
                    Id field;
                    if(n.key=="argument-type" || n.key=="equation-witness") {
                        Id index=literal(n.site,Type::simple(Type::Exact),Value::integer(n.inputs.size()));
                        Id supplied=g.semantic(Rule::Index,n.site,{n.slot,index},n.scope);
                        field=argumentType(dimension,n.scope,state,supplied);
                    } else field=type(dimension,n.scope,state);
                    if(n.key=="record-field") {
                        Id index=literal(n.site,Type::simple(Type::Exact),Value::integer(n.inputs.size()));
                        g.nodes[field].slot=g.semantic(Rule::Index,n.site,{n.slot,index},n.scope);
                        g.nodes[field].key="record-field";
                    }
                    n.inputs.push_back(field);continue;
                }
                Built b = build(dimension, frame, state); state = b.state;
                n.inputs.push_back(propertyValue(b.value));
            }
            Id decl = namedType(syntax->text, n.scope);
            if(decl!=none) {
                std::set<Id> visiting,checked;
                std::function<void(Id)> record;
                std::function<void(Id)> annotation=[&](Id site) {
                    const auto& source=g.nodes[site];
                    if(source.body!=none) return; // A type query has its own property obligations.
                    if(source.text=="(tuple)") {for(Id child:source.children) annotation(child);return;}
                    if(Id nested=namedType(source.text,n.scope);nested!=none) record(nested);
                };
                record=[&](Id declaration) {
                    if(checked.contains(declaration)) return;
                    if(!visiting.insert(declaration).second) fail(id,"recursive record types require indirection and are not allowed");
                    for(Id field:g.nodes[declaration].children)
                        if(g.nodes[field].annotation!=none) annotation(g.nodes[field].annotation);
                    visiting.erase(declaration);checked.insert(declaration);
                };
                record(decl);
            }
            if (decl != none) for (Id field : g.nodes[decl].children) {
                Id annotation = g.nodes[field].annotation;
                if (annotation != none && g.nodes[annotation].text == syntax->text)
                    fail(id, "recursive record types require indirection and are not allowed");
                Id requirement;
                if(annotation==none || dependentField(g,decl,annotation)) requirement=literal(field,{},{});
                else if(n.key=="constructor-type" || n.key=="argument-type" || n.key=="equation-witness") {
                    Id supplied;
                    if(n.key=="constructor-type") {
                        if(n.branches.size()>=g.nodes[n.slot].inputs.size()) fail(id,"constructor argument count does not match its fields");
                        supplied=g.nodes[n.slot].inputs[n.branches.size()];
                    } else {
                        supplied=n.slot;
                        for(Id dimension:syntax->children) {
                            (void)dimension;
                            Id zero=literal(n.site,Type::simple(Type::Exact),Value::integer(0));
                            supplied=g.semantic(Rule::Index,n.site,{supplied,zero},n.scope);
                        }
                        Id index=literal(n.site,Type::simple(Type::Exact),Value::integer(n.branches.size()));
                        supplied=g.semantic(Rule::Index,n.site,{supplied,index},n.scope);
                    }
                    requirement=argumentType(annotation,n.scope,state,supplied);
                } else requirement=type(annotation,n.scope,state);
                n.branches.push_back(requirement);
            }
        }
        std::vector<std::size_t> matchedDimensions;
        if((n.key=="record-field" || n.key=="equation-witness") && syntax->text!="(tuple)") {
            const Type* supplied=needType(id,n.slot);if(!supplied) return;
            Type remaining=*supplied;
            for(std::size_t i=0;i<n.inputs.size();++i) {
                remaining=resolvedType(remaining);
                if(remaining.kind==Type::Unknown) requireTypeProperties(remaining);
                if(remaining.kind!=Type::Array) fail(id,"array shape does not match its supplied witness");
                matchedDimensions.push_back(remaining.size);remaining=Type(*remaining.element);
            }
            std::reverse(matchedDimensions.begin(),matchedDimensions.end());
            if(n.key=="record-field" && n.ends.empty()) for(std::size_t i=0;i<n.inputs.size();++i) {
                Id expected=literal(n.site,Type::simple(Type::Exact),Value::integer(matchedDimensions[i]));
                n.ends.push_back(g.semantic(Rule::ShapeCheck,n.site,{n.inputs[i],expected},n.scope));
            }
        } else if (!all(n.inputs)) return;
        Type t;
        if (syntax->text == "(tuple)") {
            std::vector<Type> fields; std::vector<std::string> names;
            n.ends.clear();
            for (Id field : n.inputs) {
                const Fact* f = get(field); if (!f) return;
                names.push_back("__" + std::to_string(fields.size())); fields.push_back(f->type);
                n.ends.insert(n.ends.end(),g.nodes[field].ends.begin(),g.nodes[field].ends.end());
            }
            done({Type::record(none, "", std::move(names), std::move(fields)), {}, Effect::Pure}); return;
        }
        if (syntax->body != none) {
            const Type* queried = needType(id, n.annotation); if (!queried) return;
            t = *queried;
        }
        else if (syntax->text == "b") t = Type::simple(Type::Boolean);
        else if (syntax->text == "s") t = Type::simple(Type::String);
        else if (syntax->text == "f16" || syntax->text == "f32" || syntax->text == "f64")
            t = Type::floating(static_cast<unsigned>(std::stoul(syntax->text.substr(1))));
        else if (auto dot = syntax->text.find('.'); dot != std::string::npos) {
            const std::string& name = syntax->text;
            auto component = [&](std::size_t begin,std::size_t end) {
                unsigned value = 0;
                if (begin == end) fail(id,"fixed-point width requires decimal digits");
                for (std::size_t i=begin;i<end;++i) {
                    if (name[i] < '0' || name[i] > '9') fail(id,"invalid fixed-point type: #"+name);
                    unsigned digit = static_cast<unsigned>(name[i]-'0');
                    if (value > g.options.maxBits/10 || value*10+digit > g.options.maxBits)
                        fail(id,"fixed-point width exceeds solver bit budget",Diagnostic::Exhausted);
                    value=value*10+digit;
                }
                return value;
            };
            if (name[0] != 'i' && name[0] != 'u') fail(id,"invalid fixed-point type: #"+name);
            unsigned whole=component(1,dot), fraction=component(dot+1,name.size());
            if ((name[0]=='i' && !whole) || whole+fraction==0)
                fail(id,"fixed-point width must include storage and a signed sign bit");
            if(whole>g.options.maxBits-fraction)
                fail(id,"fixed-point width exceeds solver bit budget",Diagnostic::Exhausted);
            t=Type::fixed(whole,fraction,name[0]=='i');
        }
        else if (auto bits = width(syntax->text, g.options.maxBits)) t = Type::integer(*bits, syntax->text[0] == 'i');
        else {
            Id decl = namedType(syntax->text, n.scope);
            if (decl == none) {
                bool numericWidth=syntax->text.size()>1 && (syntax->text[0]=='i' || syntax->text[0]=='u' || syntax->text[0]=='f') &&
                    std::all_of(syntax->text.begin()+1,syntax->text.end(),[](char c){return c>='0' && c<='9';});
                if(numericWidth && syntax->text[0]!='f' &&
                   std::any_of(syntax->text.begin()+1,syntax->text.end(),[](char c){return c!='0';}))
                    fail(id,"integer width exceeds solver bit budget: #"+syntax->text,Diagnostic::Exhausted);
                fail(id, "unsupported type or integer width: #" + syntax->text,numericWidth ? Diagnostic::Invalid : Diagnostic::UndefinedType);
            }
            std::vector<Type> fields; std::vector<std::string> names;
            for (std::size_t i = 0; i < g.nodes[decl].children.size(); ++i) {
                Id field = g.nodes[decl].children[i];
                names.push_back(g.nodes[field].text);
                Id annotation = g.nodes[field].annotation;
                if (annotation == none) fields.push_back({});
                else if(processingType) {
                    Type property;
                    if(!dependentField(g,decl,annotation)) property.obligation=n.branches[i];
                    fields.push_back(property);
                }
                else {
                    const Fact* f = get(n.branches[i]); if (!f) return;
                    fields.push_back(f->type);
                }
            }
            t = Type::record(decl, syntax->text, std::move(names), std::move(fields));
            t.origin=id;
            if(processingType) t.schemaObligation=id;
            else if(!recordSchema(id,t)) return;
        }
        for (std::size_t i=0;i<n.inputs.size();++i) {
            if(n.key=="record-field" || n.key=="equation-witness") {t=Type::array(t,matchedDimensions[i]);continue;}
            Id input=n.inputs[i];
            const auto& size = *g.nodes[input].fact;
            if (size.value.symbolic()) requestChoice(size.value,id);
            if (!integer(size.type) || size.value.kind != Value::Number || size.effect >= Effect::Control)
                fail(id, "array size requires a compile-time integer without external effects");
            if (size.value.number < 0) fail(id, "array size is negative");
            if (size.value.number > g.options.maxElements) fail(id, "array size exceeds element budget",Diagnostic::Exhausted);
            t = Type::array(t, size.value.number.convert_to<std::size_t>());
        }
        if(processingType) {publishType(id,t);return;}
        t=resolvedType(t);requireTypeProperties(t);
        if(syntax->body!=none && !t.settled()) {publishType(id,t);return;}
        done({t, {}, Effect::Pure}); return;
    }
    case Rule::Global: {
        if(n.scope==none && typeEquations.contains(n.site)) globalTypeAliases(id);
        if (g.options.strictness>=1 && syntax->annotation==none && syntax->key!="process-binding")
            fail(id,"strictness 1 requires an explicit type for a new global: "+syntax->text);
        if (syntax->children.empty()) {
            const Fact* annotation=get(type(syntax->annotation,n.scope));if (!annotation) return;
            if (!annotation->type.concrete() || annotation->type.kind==Type::String) fail(id,"global storage requires a concrete runtime type");
            n.external=syntax->external;n.text=syntax->text;n.readonly=false;
            done({annotation->type,Value::runtime(),Effect::Pure});return;
        }
        prepareGlobal(id);
        if (const Fact* f = get(n.expansion)) {
            if (f->type.kind == Type::Empty) fail(id, "global initializer has no value");
            n.readonly=f->value.known() && f->effect<Effect::Control;
            if (!n.readonly) {
                if (syntax->annotation==none) fail(id,"global requires a constant, effect-free value or an explicit runtime storage type");
                if (!f->type.concrete() || f->type.kind==Type::String) fail(id,"global storage requires a concrete runtime type");
            }
            done({f->type,n.readonly ? f->value : Value::runtime(),Effect::Pure});
        }
        return;
    }
    case Rule::GlobalRead: {
        if (!get(n.inputs[0])) return;
        n.completion=g.nodes[n.inputs[0]].completion;
        if (!n.completion.normal) {empty(g.nodes[n.inputs[0]].fact->effect);return;}
        const Fact* definition=get(n.slot);if(!definition) return;
        done({definition->type,definition->value,g.nodes[n.slot].readonly ? Effect::Pure : Effect::External});return;
    }
    case Rule::Input: {
        if(n.expansion!=none) {if(const Fact* value=get(n.expansion)) done(*value);return;}
        const Fact* annotation = n.annotation == none ? nullptr : get(n.annotation);
        if (n.annotation != none && !annotation) return;
        if (n.inputs.empty()) {
            if (!annotation || !annotation->type.concrete()) fail(id, "runtime input requires a concrete type");
            if (annotation->type.kind != Type::Integer && annotation->type.kind != Type::Fixed && annotation->type.kind != Type::Float && annotation->type.kind != Type::Boolean &&
                !(annotation->type.kind == Type::Record && annotation->type.declaration != none))
                fail(id, "runtime ABI inputs require primitives or named records");
            done({annotation->type, Value::runtime(), Effect::Pure});
        } else if (const Fact* argument = get(n.inputs[0])) {
            Fact f = annotation ? convert(*argument, annotation->type, false, id) : *argument;
            f.effect = Effect::Pure;
            if(annotation && !(argument->type==f.type) && recordConversion(id,f)) return;
            done(std::move(f));
        }
        return;
    }
    case Rule::Slot: {
        const Type* property = needType(id,n.annotation != none ? n.annotation : n.inputs.at(0));
        if (!property) return;
        if (property->kind == Type::Empty) fail(id, "a local slot cannot contain a no-value result");
        Type t = *property;
        requireTypeProperties(t);
        if ((t.kind == Type::Record || t.kind == Type::Array) && !t.settled() && n.annotation != none && !n.inputs.empty()) {
            const Type* input = needType(id, n.inputs[0]); if (!input) return;
            t = conversionType(*input,t,false,id);
        }
        done({t, {}, Effect::Pure}); return;
    }
    case Rule::Adapt:
    case Rule::Cast: {
        if(n.expansion!=none) {if(const Fact* value=get(n.expansion)) done(*value);return;}
        if (!get(n.inputs[0])) return;
        n.completion = g.nodes[n.inputs[0]].completion;
        if (!n.completion.normal) { empty(g.nodes[n.inputs[0]].fact->effect); return; }
        if (!all(n.inputs)) return;
        Fact converted=convert(*g.nodes[n.inputs[0]].fact, g.nodes[n.inputs[1]].fact->type, n.rule == Rule::Cast, id);
        if(recordConversion(id,converted)) return;
        done(std::move(converted));return;
    }
    case Rule::Bind: {
        if (n.inputs.empty()) {
            if (Id local=constant(n.text,n.scope);local!=none) n.inputs.push_back(local);
            else if (auto target = g.targetFacts.find(n.text); target != g.targetFacts.end()) {
                n.inputs.push_back(target->second);
                if (const Fact* f = get(n.inputs[0])) done(*f);
                return;
            }
            if (n.inputs.empty()) {
            auto it = g.symbols.find(n.text);
            if (it == g.symbols.end()) fail(id, "undefined identifier: " + n.text, Diagnostic::UndefinedName);
            if (it->second.size() != 1 || g.nodes[it->second[0]].syntax != Syntax::Global)
                fail(id, "function names are not values: " + n.text);
            n.inputs.push_back(global(it->second[0]));
            }
        }
        if (const Fact* f = get(n.inputs[0])) done(*f);
        return;
    }
    case Rule::Step:
    case Rule::Store:
    case Rule::Finish: {
        Id stateInput = n.rule == Rule::Finish ? n.inputs[1] : n.inputs[0];
        if (!get(stateInput)) return;
        n.completion = g.nodes[stateInput].completion;
        if (!n.completion.normal) { empty(g.nodes[stateInput].fact->effect); return; }
        if (!all(n.inputs)) return;
        Id valueInput = n.rule == Rule::Finish ? n.inputs[0] : n.inputs[1];
        n.completion.include(g.nodes[valueInput].completion);
        n.completion.normal = g.nodes[valueInput].completion.normal;
        if (!n.completion.normal) { empty(std::max(g.nodes[stateInput].fact->effect,g.nodes[valueInput].fact->effect)); return; }
        const auto& a = *g.nodes[n.inputs[0]].fact; const auto& b = *g.nodes[n.inputs[1]].fact;
        if (n.rule == Rule::Step) empty(effects(a, b));
        else if (n.rule == Rule::Store) {
            bool persistent=g.nodes[n.slot].rule==Rule::Global;
            if (persistent) {
                if (!get(n.slot)) return;
                if (g.nodes[n.slot].readonly) fail(id,"cannot assign global: "+g.nodes[n.slot].text);
            }
            done({b.type,b.value,std::max(persistent ? Effect::External : Effect::Local,effects(a,b))});
        }
        else done({a.type, a.value, effects(a, b)});
        return;
    }
    case Rule::StateOf: {
        if (const Fact* f = get(n.inputs[0])) {
            n.endState = g.nodes[n.inputs[0]].endState;
            if (n.endState == none) throw std::logic_error("resolved control node has no exit state");
            if (const Fact* state = get(n.endState)) { n.completion = g.nodes[n.endState].completion; empty(effects(*f, *state)); }
        }
        return;
    }
    case Rule::PropertyValue: {
        const auto& source=g.nodes[n.inputs[0]];
        if(n.expansion==none) {
            if(source.rule==Rule::Load) n.expansion=propertyLoad(source.slot,source.inputs[0],source.site);
            else if(source.rule==Rule::Bind) {
                if(!needType(id,n.inputs[0])) return;
                if(source.inputs.empty()) return;
                n.expansion=propertyValue(source.inputs[0]);
            } else if(source.rule==Rule::Invoke) {
                if(!needType(id,n.inputs[0])) return;
                if(source.rule!=Rule::Invoke) {enqueue(id);return;}
                const auto& function=g.nodes[source.body];
                if(function.external || function.noinline || function.exported) n.expansion=n.inputs[0];
                else {
                    std::vector<Id> inputs;
                    for(Id input:source.inputs) inputs.push_back(g.nodes[input].rule==Rule::Deferred ? input : propertyValue(input));
                    Id call=g.semantic(Rule::PropertyCall,source.site,std::move(inputs),source.scope);
                    auto& projection=g.nodes[call];projection.text=source.text;projection.body=source.body;
                    projection.previous=source.previous;projection.receivers=source.receivers;
                    projection.receiverCount=source.receiverCount;projection.lexicalScope=source.lexicalScope;
                    projection.candidates=source.candidates;projection.signatureOrigin=n.inputs[0];
                    n.expansion=call;
                }
            } else if(source.rule==Rule::Finish || source.rule==Rule::Evaluate) {
                if(!needType(id,n.inputs[0])) return;
                const auto& completion=source.fact ? source.completion : source.typeCompletion;
                if(!completion.normal || !completion.exits.empty()) n.expansion=n.inputs[0];
                else n.expansion=propertyValue(source.rule==Rule::Finish ? source.inputs[0] : source.expansion);
            } else if(source.rule==Rule::Input && !source.inputs.empty()) {
                Id argument=propertyValue(source.inputs[0]);
                n.expansion=g.semantic(Rule::Input,source.site,{argument},source.scope);
                g.nodes[n.expansion].annotation=source.annotation;
                g.nodes[n.expansion].key=source.key;
            } else if(source.rule==Rule::Unary || source.rule==Rule::Math || (source.rule==Rule::Binary && !source.text.starts_with('.')) ||
                      source.rule==Rule::Cast || source.rule==Rule::Adapt || source.rule==Rule::Index ||
                      source.rule==Rule::Array || source.rule==Rule::Tuple || source.rule==Rule::Construct || source.rule==Rule::Update) {
                if(source.rule==Rule::Unary && source.text=="|") {
                    const Type* argument=needType(id,source.inputs[0]);if(!argument) return;
                    if(argument->kind==Type::Array) {done({Type::simple(Type::Exact),Value::integer(argument->size),Effect::Pure});return;}
                }
                std::vector<Id> inputs;
                for(std::size_t i=0;i<source.inputs.size();++i)
                    inputs.push_back(i==1 && (source.rule==Rule::Cast || source.rule==Rule::Adapt) ? source.inputs[i] : propertyValue(source.inputs[i]));
                n.expansion=g.semantic(source.rule,source.site,std::move(inputs),source.scope);
                g.nodes[n.expansion].text=source.text;g.nodes[n.expansion].annotation=source.annotation;
                if(source.key=="unchecked-record") g.nodes[n.expansion].key=source.key;
            } else n.expansion=n.inputs[0];
        }
        if(const Fact* value=get(n.expansion)) done(*value);
        return;
    }
    case Rule::PropertyCall: {
        if(!resolveCall(id,true)) return;
        if(n.expansion==none) instantiate(id,n.body);
        if(n.expansion==none) return;
        if(n.signature==none) n.signature=n.runtimeBackedge==none ? propertyValue(n.expansion) : n.signatureOrigin;
        if(const Fact* value=get(n.signature)) done(*value);
        return;
    }
    case Rule::PropertyLoad: {
        const auto& state=g.nodes[n.inputs[0]];
        if(n.expansion==none) {
            if(state.rule==Rule::Store || state.rule==Rule::Step) {
                const Type* before=needType(id,state.inputs[0]);if(!before) return;
                const auto& flow=g.nodes[state.inputs[0]];
                if(!(flow.fact ? flow.completion : flow.typeCompletion).normal) n.expansion=load(n.slot,n.inputs[0],n.site);
                else if(state.rule==Rule::Store) n.expansion=state.slot==n.slot ? propertyValue(state.inputs[1]) : propertyLoad(n.slot,state.previous,n.site);
                else {
                    const Type* expression=needType(id,state.inputs[1]);if(!expression) return;
                    Id after=g.nodes[state.inputs[1]].endState;
                    n.expansion=propertyLoad(n.slot,after==none ? state.previous : after,n.site);
                }
            } else if(state.rule==Rule::StateOf && !state.inputs.empty()) {
                const auto& operation=g.nodes[state.inputs[0]];
                if(operation.site!=none && operation.previous!=none && !operation.mayExit &&
                   !mayWrite(operation.site,operation.scope,n.slot))
                    n.expansion=propertyLoad(n.slot,operation.previous,n.site);
                else n.expansion=load(n.slot,n.inputs[0],n.site);
            } else if(state.rule==Rule::Entry && state.previous!=none && g.nodes[n.slot].scope!=state.scope) {
                const Node* call=state.region==none ? nullptr : &g.nodes[state.region];
                auto captured=call ? std::find(call->environment.begin(),call->environment.end(),n.slot) : std::vector<Id>::const_iterator{};
                if(call && captured!=call->environment.end()) {
                    Id seed=call->seeds[static_cast<std::size_t>(captured-call->environment.begin())];
                    n.expansion=g.semantic(Rule::LoopValue,n.site,{propertyValue(seed)});
                } else n.expansion=propertyLoad(n.slot,state.previous,n.site);
            }
            else n.expansion=load(n.slot,n.inputs[0],n.site);
        }
        if(const Fact* value=get(n.expansion)) done({value->type,value->value,Effect::Pure});
        return;
    }
    case Rule::Load: {
        if (!get(n.inputs[0])) return;
        const auto& state = g.nodes[n.inputs[0]];
        if (n.expansion == none) {
            if (state.rule == Rule::Entry) {
                if (state.previous == none || g.nodes[n.slot].scope == state.scope)
                    fail(id, "read of uninitialized local: " + g.nodes[n.slot].text);
                const Node* call = state.region == none ? nullptr : &g.nodes[state.region];
                auto captured = call ? std::find(call->environment.begin(),call->environment.end(),n.slot) : std::vector<Id>::const_iterator{};
                if (call && captured != call->environment.end())
                    n.expansion = g.semantic(Rule::LoopValue,n.site,{call->seeds[static_cast<std::size_t>(captured-call->environment.begin())]});
                else n.expansion = load(n.slot,state.previous,n.site);
            }
            else if (state.rule == Rule::CallState) {
                const auto& call = g.nodes[state.region];
                auto capture = std::find(call.environment.begin(),call.environment.end(),n.slot);
                if (capture == call.environment.end()) n.expansion = load(n.slot,state.previous,n.site);
                else n.expansion = g.semantic(Rule::LoopValue,n.site,{call.seeds[static_cast<std::size_t>(capture-call.environment.begin())]});
            }
            else if (state.rule == Rule::Store && state.slot == n.slot) n.expansion = state.inputs[1];
            else if (state.rule == Rule::Step && g.nodes[state.inputs[1]].endState!=none)
                n.expansion=load(n.slot,g.nodes[state.inputs[1]].endState,n.site);
            else if (state.rule == Rule::Store || state.rule == Rule::Step) n.expansion = load(n.slot, state.previous, n.site);
            else if (state.rule == Rule::StateOf) n.expansion = load(n.slot, state.endState, n.site);
            else if (state.rule == Rule::Resume) n.expansion = resumedLoad(n.slot,n.inputs[0],n.site);
            else if (state.rule == Rule::LoopHeader || state.rule == Rule::LoopExit) {
                const auto& loop = g.nodes[state.region];
                auto it = std::find(loop.carried.begin(), loop.carried.end(), n.slot);
                if (it == loop.carried.end()) n.expansion = load(n.slot, loop.previous, n.site);
                else {
                    Id seed = loop.seeds[static_cast<std::size_t>(it - loop.carried.begin())];
                    n.expansion = g.semantic(Rule::LoopValue, n.site, {seed});
                    g.nodes[n.expansion].region = state.region;
                }
            }
            else if (state.rule == Rule::Merge && state.text == "state") {
                bool left = g.nodes[state.inputs[1]].completion.normal, right = g.nodes[state.inputs[2]].completion.normal;
                if (!left || !right) n.expansion = load(n.slot,state.inputs[left ? 1 : 2],n.site);
                else {
                    Id a = load(n.slot, state.inputs[1], n.site), b = load(n.slot, state.inputs[2], n.site);
                    n.expansion = g.semantic(Rule::Merge, n.site, {state.inputs[0], a, b});
                }
            } else throw std::logic_error("load encountered invalid state edge: " + ruleName(state.rule));
        }
        if (const Fact* f = get(n.expansion)) done({f->type, f->value, Effect::Pure});
        return;
    }
    case Rule::Merge: {
        if (!all(n.inputs)) return;
        const auto& condition = *g.nodes[n.inputs[0]].fact;
        const auto& a = *g.nodes[n.inputs[1]].fact; const auto& b = *g.nodes[n.inputs[2]].fact;
        Effect e = std::max(condition.effect, effects(a, b));
        if (n.text == "state") {
            n.completion = g.nodes[n.inputs[1]].completion;
            n.completion.include(g.nodes[n.inputs[2]].completion);
            n.completion.normal = n.completion.normal || g.nodes[n.inputs[2]].completion.normal;
            empty(e);
        }
        else {
            if (!(a.type == b.type)) fail(id, "control-flow stores disagree on local type");
            Fact merged = condition.value.symbolic() ? chooseValue(condition, a, b, id) :
                Fact{a.type, a.value == b.value ? a.value : Value::runtime(), e};
            merged.effect = e; done(merged);
        }
        return;
    }
    case Rule::Unary: {
        const Fact* f = get(n.inputs[0]); if (!f) return;
        done(unary(n.text, *f, id)); return;
    }
    case Rule::Binary: {
        if (n.text.starts_with('.') && n.text != "..") { broadcastRule(id); return; }
        if (!all(n.inputs)) return;
        done(binary(n.text, *g.nodes[n.inputs[0]].fact, *g.nodes[n.inputs[1]].fact, id)); return;
    }
    case Rule::Array: {
        if(n.expansion!=none) {if(const Fact* value=get(n.expansion)) done(*value);return;}
        if (!all(n.inputs)) return;
        if (n.inputs.size() > g.options.maxElements) fail(id, "array exceeds element budget",Diagnostic::Exhausted);
        Type element;
        if (n.inputs.empty() && n.annotation != none) {
            const Type* prototype = needType(id,n.annotation); if (!prototype) return;
            element = *prototype;
        }
        Effect effect = Effect::Pure;
        for (Id input : n.inputs) {
            const auto& f = *g.nodes[input].fact;
            if (f.type.kind == Type::Empty) fail(id, "array element has no value");
            element = common(element, f.type, id); effect = std::max(effect, f.effect);
        }
        // Empty broadcast results still have a real element interface, even
        // though the prototype must never execute a sample operation.
        element=resolvedType(element);requireTypeProperties(element);
        if((element.kind==Type::Record || element.kind==Type::Array) &&
            std::any_of(n.inputs.begin(),n.inputs.end(),[&](Id input){return !(g.nodes[input].fact->type==element);})) {
            Id target=literal(n.site,element,{});std::vector<Id> converted;
            for(Id input:n.inputs) converted.push_back(adapt(input,target,n.site));
            n.expansion=g.semantic(Rule::Array,n.site,std::move(converted),n.scope);
            (void)get(n.expansion);return;
        }
        std::vector<Value> values;
        for (Id input : n.inputs) values.push_back(convert(*g.nodes[input].fact, element, false, id).value);
        done({Type::array(element, values.size()), Value::array(std::move(values)), effect}); return;
    }
    case Rule::Index: {
        if (!all(n.inputs)) return;
        const auto& array = *g.nodes[n.inputs[0]].fact; const auto& index = *g.nodes[n.inputs[1]].fact;
        if (array.type.kind == Type::Record) {
            if (index.value.kind != Value::Number || index.value.number < 0 || index.value.number >= array.type.fields.size())
                fail(id, "record field index must be a valid compile-time position");
            std::size_t offset = index.value.number.convert_to<std::size_t>();
            done({array.type.fields[offset], array.value.kind == Value::Elements ? array.value.elements[offset] : Value::runtime(), effects(array, index)});
            return;
        }
        if (array.type.kind != Type::Array && array.type.kind != Type::String) fail(id, "indexing requires an array or string");
        if (!integer(index.type)) fail(id, "index must be an integer");
        Type t = array.type.kind == Type::String ? Type::integer(8, false) : *array.type.element;
        std::size_t size = array.type.kind == Type::String ? array.value.bytes.size() : array.type.size;
        if (array.type.kind == Type::String && !array.value.known()) fail(id, "indexing a runtime string is not implemented");
        if (index.value.symbolic() || guarded(id)) { done(symbolicIndex(array, index, id)); return; }
        if (!index.value.known()) {
            if (!size) fail(id, "index into an empty array or string");
            done({t, Value::runtime(), std::max(Effect::Control, effects(array, index))}); return;
        }
        if (index.value.number < 0 || index.value.number >= size) fail(id, "index out of bounds",Diagnostic::ValueTrap);
        auto offset = index.value.number.convert_to<std::size_t>();
        Value v = Value::runtime();
        if (array.value.kind == Value::Bytes) v = Value::integer(static_cast<unsigned char>(array.value.bytes[offset]));
        else if (array.value.kind == Value::Elements) v = array.value.elements.at(offset);
        done({t, v, effects(array, index)}); return;
    }
    case Rule::Update: {
        if(n.expansion!=none) {if(const Fact* value=get(n.expansion)) done(*value);return;}
        if (!all(n.inputs)) return;
        const auto& array = *g.nodes[n.inputs[0]].fact;
        const auto& index = *g.nodes[n.inputs[1]].fact;
        if (array.type.kind == Type::Record) {
            if (index.value.kind != Value::Number || index.value.number < 0 || index.value.number >= array.type.fields.size())
                fail(id, "record field update requires a valid compile-time position");
            std::size_t offset = index.value.number.convert_to<std::size_t>();
            Fact item = convert(*g.nodes[n.inputs[2]].fact, array.type.fields[offset], false, id);
            if(array.type.declaration!=none && std::any_of(g.nodes[array.type.declaration].children.begin(),g.nodes[array.type.declaration].children.end(),[&](Id field){return dependentField(g,array.type.declaration,g.nodes[field].annotation);})) {
                std::vector<Id> fields;
                for(std::size_t i=0;i<array.type.fields.size();++i) {
                    if(i==offset) fields.push_back(adapt(n.inputs[2],literal(n.site,array.type.fields[i],{}),n.site));
                    else fields.push_back(g.semantic(Rule::Index,n.site,{n.inputs[0],literal(n.site,Type::simple(Type::Exact),Value::integer(i))},n.scope));
                }
                n.expansion=g.semantic(Rule::Construct,n.site,std::move(fields),n.scope);
                g.nodes[n.expansion].annotation=literal(n.site,array.type,{});
                (void)get(n.expansion);return;
            }
            auto values = array.value.kind == Value::Elements ? array.value.elements : std::vector<Value>(array.type.fields.size(), Value::runtime());
            values[offset] = item.value;
            Fact result{array.type, Value::array(std::move(values)), std::max(effects(array, index), item.effect)};
            if(recordConversion(id,result)) return;
            done(std::move(result));return;
        }
        if (array.type.kind != Type::Array) fail(id, "element assignment requires a mutable array value");
        if (!integer(index.type)) fail(id, "array index must be an integer");
        Fact replacement = convert(*g.nodes[n.inputs[2]].fact, *array.type.element, false, id);
        if (index.value.symbolic() || guarded(id)) {
            (void)symbolicIndex(array, index, id);
            std::vector<Value> values;
            for (std::size_t i = 0; i < array.type.size; ++i) {
                Fact condition = binary("==", index, {Type::simple(Type::Exact), Value::integer(i), Effect::Pure}, id);
                Fact old{*array.type.element, array.value.kind == Value::Elements ? array.value.elements[i] : Value::runtime(), array.effect};
                values.push_back(chooseValue(condition, replacement, old, id).value);
            }
            done({array.type, Value::array(std::move(values)), std::max(effects(array, index), replacement.effect)}); return;
        }
        if (!array.type.size || (index.value.known() &&
            (index.value.number < 0 || index.value.number >= array.type.size))) fail(id, "index out of bounds",Diagnostic::ValueTrap);
        std::vector<Value> values = array.value.kind == Value::Elements ? array.value.elements :
            std::vector<Value>(array.type.size, Value::runtime());
        if (index.value.known()) values[index.value.number.convert_to<std::size_t>()] = replacement.value;
        else for (auto& value : values) if (!(value == replacement.value)) value = Value::runtime();
        Effect effect = std::max(effects(array, index), replacement.effect);
        if (!index.value.known()) effect = std::max(effect, Effect::Control);
        Fact result{array.type, Value::array(std::move(values)), effect};
        if(recordConversion(id,result)) return;
        done(std::move(result));
        return;
    }
    case Rule::Sort: sortRule(id);return;
    case Rule::ShapeCheck: recordRule(id);return;
    case Rule::Loop: case Rule::LoopHeader: case Rule::LoopExit:
    case Rule::LoopValue: case Rule::LoopItem: case Rule::Repeat:
        loopRule(id); return;
    case Rule::Select:
    case Rule::ShortCircuit: {
        if(n.rule==Rule::ShortCircuit && logicalRule(id,false)) return;
        const Fact* guard = get(n.inputs[0]); if (!guard) return;
        Fact condition = convert(*guard, Type::simple(Type::Boolean), false, id);
        if (condition.value.symbolic() && hasCompletion(n.site,n.scope)) requestChoice(condition.value,id);
        const bool known = condition.value.known();
        const bool shortCircuit = n.rule == Rule::ShortCircuit;
        if (!n.expanded) {
            n.expanded = true;
            if (n.typeExpanded) { n.branches.clear(); n.ends.clear(); n.typeExpanded = false; }
            if (shortCircuit) {
                bool skip = known && (condition.value.number != 0) == (n.text == "||");
                if (skip) { n.endState = n.previous; done(condition); return; }
                Id branchScope = g.scope(n.scope, n.site, n.identity + "/short-circuit");
                if (!known && !condition.value.symbolic()) g.scopes[branchScope].runtimeControl = true;
                if (condition.value.symbolic()) g.scopes[branchScope].guard = guardedBy(n.scope, condition, n.text == "&&", id);
                Built b = build(syntax->children[1], branchScope, n.previous);
                n.branches = {b.value}; n.ends = {b.state};
                if (known) n.endState = b.state;
                else {
                    Id merge = g.semantic(Rule::Merge, n.site, n.text == "&&" ?
                        std::vector<Id>{n.inputs[0], b.state, n.previous} :
                        std::vector<Id>{n.inputs[0], n.previous, b.state}, n.scope);
                    g.nodes[merge].text = "state"; n.endState = merge;
                }
            } else {
                std::vector<std::size_t> arms = known ? std::vector<std::size_t>{condition.value.number != 0 ? 1U : 2U} :
                    std::vector<std::size_t>{1, 2};
                for (auto arm : arms) {
                    Built b;
                    // A branch has lexical scope even when its body is a single expression.
                    Id branchScope = g.scope(n.scope, n.site, g.nodes[g.scopes[n.scope].node].identity + "/arm" + std::to_string(arm) + "/" + g.identity(n.site));
                    if (!known && !condition.value.symbolic()) g.scopes[branchScope].runtimeControl = true;
                    if (condition.value.symbolic()) g.scopes[branchScope].guard = guardedBy(n.scope, condition, arm == 1, id);
                    if (arm < syntax->children.size()) b = build(syntax->children[arm], branchScope, n.previous);
                    else b = {unit(n.site), n.previous};
                    n.branches.push_back(b.value); n.ends.push_back(b.state);
                }
                if (known) n.endState = n.ends[0];
                else {
                    n.endState = g.semantic(Rule::Merge, n.site, {n.inputs[0], n.ends[0], n.ends[1]}, n.scope);
                    g.nodes[n.endState].text = "state";
                }
            }
        }
        if (condition.value.symbolic() && !shortCircuit && syntax->children.size() == 3) {
            const Type* a; const Type* b;
            try { a = needType(id,n.branches[0]); b = needType(id,n.branches[1]); }
            catch (const Error&) { requestChoice(condition.value,id); }
            if (!a || !b) return;
            if (!(*a == *b) && a->kind != Type::Exact && b->kind != Type::Exact) requestChoice(condition.value,id);
        }
        bool ready = all(n.branches);
        if (!all(n.ends)) ready = false;
        if (!ready) return;
        Effect effect = condition.effect;
        for (Id state : n.ends) effect = std::max(effect, g.nodes[state].fact->effect);
        n.completion.normal = shortCircuit && !known;
        for (Id state : n.ends) {
            n.completion.include(g.nodes[state].completion);
            n.completion.normal = n.completion.normal || g.nodes[state].completion.normal;
        }
        if (!n.completion.normal) { empty(effect); return; }
        Fact a = *g.nodes[n.branches[0]].fact;
        if (shortCircuit) {
            if (!g.nodes[n.ends[0]].completion.normal) {
                done({Type::simple(Type::Boolean),Value::integer(n.text == "||" ? 1 : 0),effect}); return;
            }
            a = convert(a, Type::simple(Type::Boolean), false, id);
            if (condition.value.symbolic()) {
                Fact fixed{Type::simple(Type::Boolean), Value::integer(n.text == "||" ? 1 : 0), Effect::Pure};
                Fact selected = n.text == "&&" ? chooseValue(condition, a, fixed, id) : chooseValue(condition, fixed, a, id);
                selected.effect = effect; done(selected); return;
            }
            // A constant RHS may determine the value; the conditional effect and
            // state edges are retained independently.
            Value v = a.value;
            if (!known && (!a.value.known() || (a.value.number != 0) == (n.text == "&&"))) v = Value::runtime();
            done({Type::simple(Type::Boolean), v, effect}); return;
        }
        if (syntax->children.size() == 2) { empty(effect); return; }
        if (known) { a.effect = effect; done(a); return; }
        if (!g.nodes[n.ends[0]].completion.normal || !g.nodes[n.ends[1]].completion.normal) {
            Fact selected = *g.nodes[n.branches[g.nodes[n.ends[0]].completion.normal ? 0 : 1]].fact;
            selected.effect = effect; done(selected); return;
        }
        Fact b = *g.nodes[n.branches[1]].fact;
        if (!(a.type == b.type) && a.type.kind != Type::Exact && b.type.kind != Type::Exact)
            fail(id, "runtime conditional branches must have the same type: " + a.type.str() + " and " + b.type.str());
        Type t = common(a.type, b.type, id);
        a = convert(a, t, false, id); b = convert(b, t, false, id);
        Fact selected = condition.value.symbolic() ? chooseValue(condition, a, b, id) :
            Fact{t, a.value == b.value ? a.value : Value::runtime(), effect};
        selected.effect = effect; done(selected); return;
    }
    case Rule::Iterate: {
        if (hasCompletion(n.site,n.scope)) { loopRule(id); return; }
        const Fact* iterable = get(n.inputs[0]); if (!iterable) return;
        if (iterable->type.kind != Type::Array) fail(id, "unrolled iteration requires a fixed-size array or range", Diagnostic::NotIterable);
        if (n.text == "@@" && iterable->value.symbolic()) requestChoice(iterable->value,id);
        if (n.text == "@@" && !iterable->value.known()) fail(id, "sorted iteration requires compile-time values");
        if (n.text=="@@") {
            if (n.annotation==none) {
                n.annotation=g.semantic(Rule::Sort,n.site,{n.inputs[0]},n.scope);
                g.nodes[n.annotation].previous=n.previous;
            }
            if (!get(n.annotation)) return;
        }
        if (!n.expanded) {
            n.expanded = true;
            const std::size_t size = iterable->type.size;
            if (size > g.options.maxElements) fail(id, "iteration exceeds element budget",Diagnostic::Exhausted);
            Id array = n.text=="@@" ? n.annotation : n.inputs[0];
            Id state = n.previous;
            for (std::size_t i = 0; i < size; ++i) {
                Id iteration = g.scope(n.scope, n.site, g.nodes[g.scopes[n.scope].node].identity + "/" + g.identity(n.site) + "/iteration" + std::to_string(i));
                Id index = literal(n.site, Type::simple(Type::Exact), Value::integer(i));
                Id item = g.semantic(Rule::Index, n.site, {array, index}, iteration);
                Id variable = g.semantic(Rule::Slot, n.site, {item}, iteration);
                g.nodes[variable].text = "_"; g.nodes[variable].readonly = true;
                g.scopes[iteration].slots["_"] = variable;
                Id store = g.semantic(Rule::Store, n.site, {state, item}, iteration);
                g.nodes[store].slot = variable; g.nodes[store].previous = state;
                Built body = build(syntax->children[1], iteration, store);
                state = body.state;
            }
            n.endState = state;
        }
        if (const Fact* f = get(n.endState)) empty(effects(*f, *iterable));
        return;
    }
    case Rule::Resource: {
        resourceRule(id);return;
    }
    case Rule::Invoke: {
        if (!resolveCall(id, false)) return;
        if (n.rule != Rule::Invoke) { rule(id); return; }
        if (n.endState == none) n.endState = n.previous;
        const auto& function = g.nodes[n.body];
        if (function.text == ".@@" && !g.nodes[n.inputs[0]].fact->value.known())
            fail(id,"sorted iteration method requires a compile-time receiver");
        Effect effect = Effect::Pure;
        for (Id arg : n.inputs) if (g.nodes[arg].rule != Rule::Deferred) effect = std::max(effect, g.nodes[arg].fact->effect);
        if (function.external) {
            Type result = Type::simple(Type::Empty);
            if (function.annotation != none) {
                const Fact* t = get(type(function.annotation,n.lexicalScope,n.lexicalScope==none ? none : n.previous)); if (!t) return;
                result = t->type;
            }
            if (result.kind != Type::Empty && result.kind != Type::Integer && result.kind != Type::Fixed && result.kind != Type::Float && result.kind != Type::Boolean &&
                !(result.kind == Type::Record && result.declaration != none && result.concrete()))
                fail(id, "external result must be a concrete primitive or named record");
            bool prepared=n.branches.size()==function.children.size();
            std::vector<Id> interface;
            for (std::size_t i = 0; i < function.children.size(); ++i) {
                Id requirement=parameterType(id,n.body,i,n.lexicalScope);
                const Fact* annotation=get(requirement);
                if (!annotation) return;
                const auto& t = annotation->type;
                if (t.kind != Type::Integer && t.kind != Type::Fixed && t.kind != Type::Float && t.kind != Type::Boolean && t.kind != Type::String &&
                    !(t.kind == Type::Record && t.declaration != none && t.concrete()))
                    fail(id, "external parameters require concrete primitives, named records or constant strings");
                if (t.kind == Type::String && !g.nodes[n.inputs[i]].fact->value.known())
                    fail(id, "external string arguments must be compile-time constants");
                interface.push_back(requirement);
            }
            if(!prepared) {
                n.branches=std::move(interface);
                for(std::size_t i=0;i<n.inputs.size();++i) n.inputs[i]=adapt(n.inputs[i],n.branches[i],n.site);
            }
            if(!all(n.inputs)) return;
            done({result, result.kind == Type::Empty ? Value{} : Value::runtime(), Effect::External});
            return;
        }
        if (n.expansion == none) instantiate(id, n.body);
        if (n.expansion == none) return;
        if (n.runtimeBackedge != none) {
            const Type* signature = needType(id,id); if (!signature) return;
            requireTypeProperties(*signature);
            if (!signature->concrete() && signature->kind != Type::Empty)
                fail(id,"runtime recursive return interface needs a concrete type");
            done({*signature,signature->kind == Type::Empty ? Value{} : Value::runtime(),externalRecursion.contains(n.body) ? Effect::External : Effect::Control});
            return;
        }
        if (const Fact* f = get(n.expansion)) {
            n.completion = g.nodes[n.expansion].completion;
            if (f->effect < Effect::Control && !f->value.symbolic() && n.lexicalScope == none && function.receiverCount == 0 &&
                (n.scope == none || g.scopes[n.scope].puzzle == none) &&
                std::none_of(n.inputs.begin(),n.inputs.end(),[&](Id arg) { return g.nodes[arg].rule == Rule::Deferred; })) pureCalls[n.key] = n.expansion;
            done({f->type, f->value, std::max(effect, f->effect)});
        }
        return;
    }
    case Rule::None: throw std::logic_error("structural site scheduled as semantic obligation");
    }
    throw std::logic_error("missing rule");
}
} // namespace vx
