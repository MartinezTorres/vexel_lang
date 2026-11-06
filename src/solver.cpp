#include "vexel.hpp"
#include "floating.hpp"
#include "constraints.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>

namespace vx {
Id Solver::literal(Id site, Type t, Value v) {
    Id id = g.semantic(Rule::Constant, site);
    g.nodes[id].fact = Fact{std::move(t), std::move(v), Effect::Pure}; return id;
}
Id Solver::unit(Id site) { return literal(site, Type::simple(Type::Empty), {}); }
Id Solver::type(Id site, Id scopeId, Id state) {
    auto scopedKey = std::make_tuple(site, scopeId, state);
    if (auto found = scopedAnnotations.find(scopedKey); found != scopedAnnotations.end()) return found->second;
    if (scopeId == none) {
        if (auto it = annotations.find(site); it != annotations.end()) return it->second;
    }
    Id bindings = scopeId == none ? none : snapshot(scopeId, site);
    Id id = g.semantic(Rule::Type, site, {}, bindings); g.nodes[id].previous = state;
    if (scopeId == none) annotations[site] = id;
    scopedAnnotations[scopedKey] = id;
    return id;
}
Id Solver::argumentType(Id site, Id scopeId, Id state, Id argument) {
    std::function<bool(Id)> contextual=[&](Id at) {
        const auto& syntax=g.nodes[at];
        if(syntax.body!=none) return false; // A type query supplies its own context.
        if(syntax.text=="(tuple)") return std::any_of(syntax.children.begin(),syntax.children.end(),contextual);
        return namedType(syntax.text,scopeId)!=none;
    };
    if(!contextual(site)) return type(site,scopeId,state);
    // An open requirement belongs to this application. Sharing a cached
    // declaration type would let the first supplied value specialize other uses.
    Id bindings=scopeId==none ? none : snapshot(scopeId,site);
    Id id=g.semantic(Rule::Type,site,{},bindings);
    g.nodes[id].previous=state;g.nodes[id].key="argument-type";g.nodes[id].slot=argument;
    return id;
}
Id Solver::parameterType(Id call, Id function, std::size_t index, Id lexical) {
    Id parameter=g.nodes[function].children.at(index),annotation=g.nodes[parameter].annotation;
    auto key=std::make_tuple(call,parameter,lexical);
    if(auto found=parameterAnnotations.find(key);found!=parameterAnnotations.end()) return found->second;
    Id state=lexical==none ? none : g.nodes[call].previous;
    Id result=argumentType(annotation,lexical,state,g.nodes[call].inputs.at(index));
    parameterAnnotations[key]=result;
    return result;
}
Id Solver::global(Id decl) {
    if (auto it = globals.find(decl); it != globals.end()) return it->second;
    Id id = g.semantic(Rule::Global, decl); globals[decl] = id; return id;
}
Id Solver::constant(const std::string& name,Id scopeId) const {
    for (Id at=scopeId;at!=none;at=g.scopes[at].parent)
        if (auto found=g.scopes[at].constants.find(name);found!=g.scopes[at].constants.end()) return found->second;
    return none;
}
Id Solver::binding(const std::string& name,Id scopeId) {
    if (Id local=slot(name,scopeId);local!=none) return local;
    if (Id scoped=constant(name,scopeId);scoped!=none) return scoped;
    auto found=g.symbols.find(name);
    if (found!=g.symbols.end() && found->second.size()==1 && g.nodes[found->second[0]].syntax==Syntax::Global)
        return global(found->second[0]);
    return none;
}
Id Solver::slot(const std::string& name, Id scopeId) const {
    while (scopeId != none) {
        const auto& scope = g.scopes.at(scopeId);
        if (auto it = scope.slots.find(name); it != scope.slots.end()) return it->second;
        scopeId = scope.parent;
    }
    return none;
}
Id Solver::namedType(const std::string& name, Id scopeId) const {
    while (scopeId != none) {
        if (auto found = g.scopes[scopeId].types.find(name); found != g.scopes[scopeId].types.end()) return found->second;
        scopeId = g.scopes[scopeId].parent;
    }
    auto found = g.types.find(name); return found == g.types.end() ? none : found->second;
}
std::vector<Scope::Callable> Solver::functions(const std::string& name, Id scopeId) const {
    std::vector<Scope::Callable> result;
    std::set<std::pair<Id,Id>> seen;
    for (Id at = scopeId; at != none; at = g.scopes[at].parent)
        if (auto found = g.scopes[at].functions.find(name); found != g.scopes[at].functions.end())
            for (const auto& candidate:found->second)
                if (seen.emplace(candidate.declaration,candidate.scope).second) result.push_back(candidate);
    if (auto found = g.symbols.find(name); found != g.symbols.end())
        for (Id declaration : found->second) if (g.nodes[declaration].syntax == Syntax::Function) result.push_back({declaration,none});
    return result;
}
Id Solver::snapshot(Id scopeId, Id site) {
    Id frozen = g.scope(none, site, g.nodes[g.scopes[scopeId].node].identity + "/bindings/" + g.identity(site));
    g.scopes[frozen].caller = scopeId;
    g.scopes[frozen].puzzle = g.scopes[scopeId].puzzle;
    g.scopes[frozen].guard = g.scopes[scopeId].guard;
    g.scopes[frozen].function = g.scopes[scopeId].function;
    g.scopes[frozen].loop = g.scopes[scopeId].loop;
    g.scopes[frozen].runtimeControl = g.scopes[scopeId].runtimeControl;
    g.scopes[frozen].typeValues=g.scopes[scopeId].typeValues;
    g.scopes[frozen].modulePath = g.scopes[scopeId].modulePath;
    for (Id at = scopeId; at != none; at = g.scopes[at].parent)
        { for (const auto& binding : g.scopes[at].slots) g.scopes[frozen].slots.insert(binding);
          for (const auto& binding : g.scopes[at].constants) g.scopes[frozen].constants.insert(binding);
          for (const auto& binding : g.scopes[at].types) g.scopes[frozen].types.insert(binding); }
    for (Id at = scopeId; at != none; at = g.scopes[at].parent) {
        for (const auto& [name,bindings] : g.scopes[at].functions) {
            (void)bindings;
            if (!g.scopes[frozen].functions.contains(name)) {
                auto candidates=functions(name,scopeId);
                std::erase_if(candidates,[](const auto& candidate){return candidate.scope==none;});
                g.scopes[frozen].functions[name]=std::move(candidates);
            }
        }
        for (const auto& binding : g.scopes[at].expressions) g.scopes[frozen].expressions.insert(binding);
    }
    return frozen;
}
Id Solver::adapt(Id value, Id target, Id site, bool cast) {
    return g.semantic(cast ? Rule::Cast : Rule::Adapt, site, {value, target}, g.nodes[value].scope);
}
Id Solver::load(Id variable, Id state, Id site) {
    if(g.nodes[variable].rule==Rule::Input && g.nodes[variable].key=="record-field") return variable;
    const auto key = std::make_pair(variable, state);
    if (auto found = loads.find(key); found != loads.end()) return found->second;
    if (g.nodes[variable].rule == Rule::Alias) {
        Id value = load(g.nodes[variable].slot,state,site);
        for (Id offset : g.nodes[variable].inputs) value = g.semantic(Rule::Index,site,{value,offset},g.nodes[variable].scope);
        loads[key] = value; return value;
    }
    Id id = g.semantic(g.nodes[variable].rule==Rule::Global ? Rule::GlobalRead : Rule::Load, site, {state});
    g.nodes[id].slot = variable; loads[key] = id; return id;
}
Id Solver::propertyLoad(Id variable,Id state,Id site) {
    const auto& location=g.nodes[variable];
    if(location.rule==Rule::Global || (location.rule==Rule::Input && location.key=="record-field")) return load(variable,state,site);
    const auto key=std::make_pair(variable,state);
    if(auto found=propertyLoads.find(key);found!=propertyLoads.end()) return found->second;
    if(location.rule==Rule::Alias) {
        Id value=propertyLoad(location.slot,state,site);
        for(Id offset:location.inputs) value=g.semantic(Rule::Index,site,{value,offset},location.scope);
        propertyLoads[key]=value;return value;
    }
    Id value=g.semantic(Rule::PropertyLoad,site,{state});g.nodes[value].slot=variable;
    propertyLoads[key]=value;return value;
}
Id Solver::propertyValue(Id value) {
    if(auto found=propertyValues.find(value);found!=propertyValues.end()) return found->second;
    const auto& source=g.nodes[value];
    Id projection=g.semantic(Rule::PropertyValue,source.site,{value},source.scope);
    propertyValues[value]=projection;return projection;
}
Built Solver::finishExpression(Id value, Id state) {
    Id next = g.semantic(Rule::Step, g.nodes[value].site, {state, value}, g.nodes[value].scope);
    g.nodes[next].previous = state;
    return {value, next};
}
Built Solver::block(Id site, Id scopeId, Id state, bool functionBody) {
    if (!functionBody) scopeId = g.scope(scopeId, site, g.nodes[g.scopes[scopeId].node].identity + "/" + g.identity(site));
    const auto declarations=moduleDeclarations(g,site);
    for (Id statement : declarations) if (g.nodes[statement].syntax == Syntax::RecordDecl) {
        const auto& decl = g.nodes[statement];
        if (namedType(decl.text, scopeId) != none) fail(statement, "shadowing or duplicate type: " + decl.text);
        g.scopes[scopeId].types[decl.text] = statement;
    }
    Id result = unit(site);
    for (Id statement : declarations) {
        const auto& declaration=g.nodes[statement]; const auto& name=declaration.text;
        if (declaration.syntax!=Syntax::Function && declaration.syntax!=Syntax::Global) continue;
        if (slot(name,scopeId)!=none || constant(name,scopeId)!=none || g.targetFacts.contains(name))
            fail(statement,"shadowing or duplicate symbol: "+name);
        if (declaration.syntax==Syntax::Global) {
            if (g.symbols.contains(name) || !functions(name,scopeId).empty()) fail(statement,"shadowing or duplicate symbol: "+name);
            std::string key=g.scopes[scopeId].modulePath+g.identity(statement);
            auto found=g.moduleBindings.find(key);
            Id definition=found==g.moduleBindings.end() ? g.semantic(Rule::Global,statement,{},scopeId) : found->second;
            g.moduleBindings[key]=definition;
            g.scopes[scopeId].constants[name]=definition;
        } else {
            if (auto found=g.symbols.find(name);found!=g.symbols.end())
                for (Id other:found->second) if(g.nodes[other].syntax!=Syntax::Function) fail(statement,"shadowing or duplicate symbol: "+name);
            for (const auto& other:functions(name,scopeId))
                if (declaration.exported || declaration.external || g.nodes[other.declaration].exported || g.nodes[other.declaration].external)
                    fail(statement,"duplicate ABI function: "+name);
            g.scopes[scopeId].functions[name].push_back({statement,scopeId});
        }
    }
    bool moduleMembers=std::any_of(declarations.begin(),declarations.end(),[&](Id declaration) {
        return g.nodes[declaration].syntax==Syntax::Global ||
            std::find(g.nodes[site].children.begin(),g.nodes[site].children.end(),declaration)==g.nodes[site].children.end();
    });
    if (moduleMembers) {
        Id environment=snapshot(scopeId,site);
        // A module is instantiated per lexical scope, not per activation. Its
        // own declarations can see lexical declarations, but never borrow an
        // importing activation's automatic variables or expression arguments.
        g.scopes[environment].slots.clear();g.scopes[environment].expressions.clear();
        g.scopes[environment].caller=none;g.scopes[environment].function=none;g.scopes[environment].loop=none;
        for (Id declaration:declarations) {
            const auto& member=g.nodes[declaration];
            if (member.syntax==Syntax::Global) {
                Id definition=g.scopes[scopeId].constants.at(member.text);
                if (g.nodes[definition].scope==scopeId) g.nodes[definition].scope=environment;
            } else if (member.syntax==Syntax::Function &&
                       std::find(g.nodes[site].children.begin(),g.nodes[site].children.end(),declaration)==g.nodes[site].children.end()) {
                for (Id target:{scopeId,environment})
                    for (auto& candidate:g.scopes[target].functions[member.text])
                        if (candidate.declaration==declaration && candidate.scope==scopeId) candidate.scope=environment;
            }
        }
    }
    if (hasCompletion(site,scopeId)) {
        Id sequence = g.semantic(Rule::Sequence,site,{},scopeId);
        g.nodes[sequence].previous = state; g.nodes[sequence].endState = state;
        return {sequence,g.semantic(Rule::StateOf,site,{sequence},scopeId)};
    }
    for (Id statement : g.nodes[site].children) {
        const auto& s = g.nodes[statement];
        Built b = build(statement, scopeId, state);
        state = b.state;
        result = s.terminated ? unit(statement) : b.value;
    }
    return {result, state};
}
Built Solver::build(Id site, Id scopeId, Id state) {
    if (state != none && g.nodes[state].mayExit && (!g.nodes[state].fact || !g.nodes[state].completion.normal)) {
        Id evaluation = g.semantic(Rule::Evaluate,site,{},scopeId);
        g.nodes[evaluation].previous = state; g.nodes[evaluation].mayExit = true;
        Id next = g.semantic(Rule::StateOf,site,{evaluation},scopeId); g.nodes[next].mayExit = true;
        return {evaluation,next};
    }
    Built result = buildNow(site,scopeId,state);
    const bool mayExit = hasCompletion(site,scopeId) || (state != none && g.nodes[state].mayExit);
    g.nodes[result.value].mayExit = g.nodes[result.value].mayExit || mayExit;
    g.nodes[result.state].mayExit = g.nodes[result.state].mayExit || mayExit;
    return result;
}
Built Solver::buildNow(Id site, Id scopeId, Id state) {
    const Node& s = g.nodes[site];
    auto simple = [&](Rule r, std::vector<Id> inputs) {
        Id id = g.semantic(r, site, std::move(inputs), scopeId);
        g.nodes[id].text = s.text; g.nodes[id].previous = state;
        return finishExpression(id, state);
    };
    auto compound = [&](const std::string& op,Id left,Id right,Id root,const std::vector<Id>& offsets) {
        auto candidates = functions("."+op,scopeId);
        if (candidates.empty()) {
            Id value = g.semantic(Rule::Binary,site,{left,right},scopeId); g.nodes[value].text = op;g.nodes[value].previous=state;
            if(op.starts_with('.') && op!="..") state=g.semantic(Rule::StateOf,site,{value},scopeId);
            return value;
        }
        Node source = s; source.syntax = Syntax::Call; source.text = "."+op;
        source.key = "fallback-binary"; source.receiverCount = 1; source.identity += "/compound";
        Id operation = g.add(std::move(source));
        Id alias = g.semantic(Rule::Alias,site,offsets,scopeId); g.nodes[alias].slot = root;
        Id call = g.semantic(Rule::Invoke,operation,{left,right},snapshot(scopeId,site));
        auto& node = g.nodes[call]; node.text = "."+op; node.previous = state;
        node.receiverCount = 1; node.receivers = {alias};
        for (const auto& candidate : candidates) {
            const auto& fn = g.nodes[candidate.declaration];
            if (fn.children.size() == 2 && fn.receiverCount == 1) {
                node.candidates.push_back(candidate.declaration); node.lexicalScope = candidate.scope;
            }
        }
        state = g.semantic(Rule::StateOf,site,{call},scopeId);
        return call;
    };
    switch (s.syntax) {
    case Syntax::Destructure: {
        Built rhs = build(s.children.back(),scopeId,state);
        Id tuple = g.semantic(Rule::Unpack,site,{rhs.value},scopeId);
        g.nodes[tuple].phase = static_cast<unsigned>(s.children.size()-1);
        Id temporary = g.semantic(Rule::Slot,site,{tuple},scopeId);
        g.nodes[temporary].readonly = true;
        std::string name = "@tuple" + std::to_string(temporary);
        g.nodes[temporary].text = name; g.scopes[scopeId].slots[name] = temporary;
        state = g.semantic(Rule::Store,site,{rhs.state,tuple},scopeId);
        g.nodes[state].slot = temporary; g.nodes[state].previous = rhs.state;
        for (std::size_t i = 0; i+1 < s.children.size(); ++i) {
            Node reference; reference.syntax = Syntax::Name; reference.span = s.span; reference.text = name;
            reference.identity = s.identity + "/tuple";
            Id base = g.add(std::move(reference));
            Node index; index.syntax = Syntax::Number; index.span = s.span; index.text = std::to_string(i);
            index.identity = s.identity + "/position" + std::to_string(i);
            Id offset = g.add(std::move(index));
            Node item; item.syntax = Syntax::Index; item.span = s.span; item.children = {base,offset};
            item.identity = s.identity + "/field" + std::to_string(i);
            Id value = g.add(std::move(item));
            Node assignment; assignment.syntax = Syntax::Assign; assignment.text = "="; assignment.span = s.span;
            assignment.identity = s.identity + "/assign" + std::to_string(i); assignment.children = {s.children[i],value};
            state = build(g.add(std::move(assignment)),scopeId,state).state;
        }
        g.scopes[scopeId].slots.erase(name);
        return {unit(site),state};
    }
    case Syntax::Return: {
        Id target = s.text == "return" ? g.scopes[scopeId].function : g.scopes[scopeId].loop;
        if (target == none) fail(site,s.text + " has no enclosing " + (s.text == "return" ? "function" : "loop"));
        Id value = unit(site);
        if (!s.children.empty()) { Built expression = build(s.children[0],scopeId,state); value = expression.value; state = expression.state; }
        Id exit = g.semantic(Rule::Exit,site,{state,value},scopeId);
        g.nodes[exit].text = s.text; g.nodes[exit].region = target; g.nodes[exit].previous = state;
        return {exit,exit};
    }
    case Syntax::Probe: {
        Built base = build(s.children[0], scopeId, state);
        // Only the receiver's type is demanded; its value/effects are not.
        return simple(Rule::Probe, {base.value});
    }
    case Syntax::RecordDecl: case Syntax::Function: case Syntax::Import: case Syntax::Global: return {unit(site), state};
    case Syntax::Member: {
        Built base = build(s.children[0], scopeId, state); state = base.state;
        Id index = g.semantic(Rule::FieldIndex, site, {base.value}, scopeId);
        g.nodes[index].text = s.text;
        return simple(Rule::Index, {base.value, index});
    }
    case Syntax::Solve: return simple(Rule::Solve, {});
    case Syntax::Choice:
    case Syntax::Require: {
        if (g.scopes[scopeId].puzzle == none) fail(site, "finite choices and constraints require a ?{...} solve region");
        Built value = build(s.children[0], scopeId, state); state = value.state;
        std::vector<Id> inputs{value.value};
        if (s.annotation != none) inputs.push_back(type(s.annotation, scopeId, state));
        return simple(s.syntax == Syntax::Choice ? Rule::Choice : Rule::Require, std::move(inputs));
    }
    case Syntax::Number: {
        if (s.text.find('.') != std::string::npos)
            return finishExpression(literal(site,Type::floating(64),Value::integer(floatLiteral(s.text,g.options.maxBits,s.span))),state);
        Big value = 0;
        unsigned base = s.text.size() > 2 && s.text[0] == '0' && (s.text[1] == 'x' || s.text[1] == 'X') ? 16 : 10;
        for (std::size_t i = base == 16 ? 2 : 0; i < s.text.size(); ++i) {
            char c = s.text[i];
            unsigned d = static_cast<unsigned>(c <= '9' ? c - '0' : c <= 'F' ? c - 'A' + 10 : c - 'a' + 10);
            value *= base; value += d;
            if (value != 0 && boost::multiprecision::msb(value) >= g.options.maxBits)
                fail(site, "integer exceeds solver bit budget",Diagnostic::Exhausted);
        }
        return finishExpression(literal(site, Type::simple(Type::Exact), Value::integer(value)), state);
    }
    case Syntax::String: return finishExpression(literal(site, Type::simple(Type::String), Value::string(s.text)), state);
    case Syntax::Character:
        return finishExpression(literal(site, Type::integer(8, false), Value::integer(static_cast<unsigned char>(s.text[0]))), state);
    case Syntax::Group: return build(s.children[0], scopeId, state);
    case Syntax::Block: return block(site, scopeId, state);
    case Syntax::Name: {
        if (s.text.starts_with('$')) {
            for (Id at = scopeId; at != none; at = g.scopes[at].parent) {
                auto found = g.scopes[at].expressions.find(s.text);
                if (found == g.scopes[at].expressions.end()) continue;
                auto binding = found->second;
                Id bindings = g.scope(binding.scope,site,g.identity(site)+"/expression-use");
                g.scopes[bindings].guard = g.scopes[scopeId].guard;
                g.scopes[bindings].puzzle = g.scopes[scopeId].puzzle;
                g.scopes[bindings].runtimeControl=g.scopes[bindings].runtimeControl || g.scopes[scopeId].runtimeControl;
                if (binding.iteration) {
                    Id item = slot("_",scopeId);
                    if (item == none) fail(site,"iteration method must bind '_' before evaluating its body");
                    Id alias = g.semantic(Rule::Alias,site,{},bindings);
                    if (g.nodes[item].rule == Rule::Alias) { g.nodes[alias].slot = g.nodes[item].slot; g.nodes[alias].inputs = g.nodes[item].inputs; }
                    else g.nodes[alias].slot = item;
                    g.nodes[alias].readonly = true;
                    g.scopes[bindings].slots["_"] = alias;
                }
                Built expression = build(binding.site, bindings, state);
                if (binding.annotation != none)
                    expression=finishExpression(adapt(expression.value,argumentType(binding.annotation,at,state,expression.value),site),expression.state);
                if(binding.iteration) {
                    Id body=g.semantic(Rule::IterationBody,site,{expression.state},bindings);
                    auto& evaluation=g.nodes[body];evaluation.previous=state;evaluation.region=g.scopes[bindings].loop;
                    evaluation.endState=resume(expression.state,evaluation.region,"continue",site,bindings);
                    return {body,g.semantic(Rule::StateOf,site,{body},bindings)};
                }
                return expression;
            }
            fail(site, "undefined expression parameter: " + s.text, Diagnostic::UndefinedName);
        }
        Id id = g.semantic(Rule::Bind, site, {}, scopeId);
        g.nodes[id].previous = state;
        auto& n = g.nodes[id]; n.text = s.text;
        n.slot = binding(s.text, scopeId);
        if (n.slot != none) n.inputs.push_back(g.scopes[scopeId].typeValues ? propertyLoad(n.slot,state,site) : load(n.slot, state, site));
        return finishExpression(id, state);
    }
    case Syntax::Declare:
    case Syntax::Assign: {
        Id namedSlot = s.syntax == Syntax::Assign && g.nodes[s.children[0]].syntax == Syntax::Name ? slot(g.nodes[s.children[0]].text,scopeId) : none;
        if (s.syntax == Syntax::Assign && (g.nodes[s.children[0]].syntax == Syntax::Index || g.nodes[s.children[0]].syntax == Syntax::Member ||
            (namedSlot != none && g.nodes[namedSlot].rule == Rule::Alias))) {
            std::vector<Id> indices;
            Id base = s.children[0];
            while (g.nodes[base].syntax == Syntax::Index || g.nodes[base].syntax == Syntax::Member) {
                indices.push_back(base);
                base = g.nodes[base].children[0];
            }
            if (g.nodes[base].syntax != Syntax::Name) fail(site, "array assignment requires a local value as its base");
            Id variable = binding(g.nodes[base].text, scopeId);
            if (variable == none) fail(site, "array assignment requires an existing local: " + g.nodes[base].text);
            if (g.nodes[variable].rule!=Rule::Global && g.nodes[variable].readonly) fail(site, "cannot assign iteration binding '_'");
            std::reverse(indices.begin(), indices.end());
            std::vector<Id> offsets;
            if (g.nodes[variable].rule == Rule::Alias) {
                offsets = g.nodes[variable].inputs; variable = g.nodes[variable].slot;
            }
            std::size_t prefix = offsets.size();
            for (Id index : indices) {
                if (g.nodes[index].syntax == Syntax::Member) offsets.push_back(none);
                else { Built b = build(g.nodes[index].children[1], scopeId, state); state = b.state; offsets.push_back(b.value); }
            }
            // Evaluate the designation once and check it before the RHS. The
            // compound value is captured here; unrelated RHS writes survive.
            Id old = load(variable, state, site);
            for (std::size_t i = 0; i < offsets.size(); ++i) {
                if (offsets[i] == none) {
                    offsets[i] = g.semantic(Rule::FieldIndex, indices[i-prefix], {old}, scopeId);
                    g.nodes[offsets[i]].text = g.nodes[indices[i-prefix]].text;
                }
                Id index = offsets[i];
                old = g.semantic(Rule::Index, site, {old, index}, scopeId);
                g.nodes[old].previous = state;
                state = finishExpression(old, state).state;
            }
            Id value;
            if (s.text == "&&=" || s.text == "||=") {
                value = g.semantic(Rule::ShortCircuit, site, {old}, snapshot(scopeId, site));
                g.nodes[value].text = s.text.substr(0, 2); g.nodes[value].previous = state;
                Id receiver=g.semantic(Rule::Alias,site,offsets,scopeId);g.nodes[receiver].slot=variable;
                g.nodes[value].receivers={receiver};
                state = g.semantic(Rule::StateOf, site, {value}, scopeId);
            } else {
                Built b = build(s.children[1], scopeId, state); value = b.value; state = b.state;
                if (s.text != "=") {
                    value = compound(s.text.substr(0,s.text.size()-1),old,value,variable,offsets);
                }
            }
            value = adapt(value, old, site);
            Id latest = load(variable, state, site);
            std::vector<Id> containers{latest};
            for (std::size_t i = 0; i + 1 < offsets.size(); ++i)
                containers.push_back(g.semantic(Rule::Index, site, {containers.back(), offsets[i]}, scopeId));
            Id changed = value;
            for (std::size_t i = offsets.size(); i-- > 0;)
                changed = g.semantic(Rule::Update, site, {containers[i], offsets[i], changed}, scopeId);
            Id store = g.semantic(Rule::Store, site, {state, changed}, scopeId);
            g.nodes[store].slot = variable; g.nodes[store].previous = state;
            // An element assignment yields the element, not the containing array.
            Id result = g.semantic(Rule::Finish, site, {value, store}, scopeId);
            return {result, store};
        }
        std::string name = s.syntax == Syntax::Declare ? s.text : g.nodes[s.children[0]].text;
        if (g.targetFacts.contains(name)) fail(site, "cannot assign or shadow target fact: " + name);
        Id variable = binding(name, scopeId);
        bool declarationName=g.symbols.contains(name) || constant(name,scopeId)!=none || !functions(name,scopeId).empty();
        if (s.syntax == Syntax::Declare && (variable != none || declarationName))
            fail(site, "shadowing or duplicate local: " + name);
        if (variable == none && declarationName) fail(site, "cannot assign global: " + name);
        if (variable!=none && g.nodes[variable].rule==Rule::Global && g.nodes[g.nodes[variable].site].annotation==none)
            fail(site,"cannot assign global: "+name);
        if (variable != none && g.nodes[variable].rule!=Rule::Global && g.nodes[variable].readonly) fail(site, "cannot assign iteration binding '_'");
        bool fresh = variable == none;
        if (fresh && s.annotation == none && g.options.strictness >= 1)
            fail(site,"strictness 1 requires an explicit type for a new local: "+name);
        if (fresh && s.syntax == Syntax::Assign && s.text != "=") fail(site, "undefined local: " + name, Diagnostic::UndefinedName);
        Id value = none;
        if (s.syntax == Syntax::Declare) {
            if (!s.children.empty()) {
                Built b = build(s.children[0], scopeId, state); value = b.value; state = b.state;
            }
        } else if (s.text == "=") {
            Built b = build(s.children[1], scopeId, state); value = b.value; state = b.state;
        } else {
            Id old = load(variable, state, site);
            if (s.text == "&&=" || s.text == "||=") {
                value = g.semantic(Rule::ShortCircuit, site, {old}, snapshot(scopeId, site));
                g.nodes[value].text = s.text.substr(0, 2); g.nodes[value].previous = state;
                Id receiver=g.semantic(Rule::Alias,site,{},scopeId);g.nodes[receiver].slot=variable;
                g.nodes[value].receivers={receiver};
                state = g.semantic(Rule::StateOf, site, {value}, scopeId);
            } else {
                Built b = build(s.children[1], scopeId, state); state = b.state;
                value = compound(s.text.substr(0,s.text.size()-1),old,b.value,variable,{});
            }
        }
        if (fresh) {
            variable = g.semantic(Rule::Slot, site, {}, scopeId);
            auto& local = g.nodes[variable]; local.text = name;
            if (s.annotation != none) local.annotation = value==none ? type(s.annotation,scopeId,state) : argumentType(s.annotation,scopeId,state,value);
            if (value != none) local.inputs = {value};
            g.scopes[scopeId].slots[name] = variable;
        }
        if (value == none) return finishExpression(unit(site), finishExpression(variable, state).state);
        value = adapt(value, variable, site);
        Id store = g.semantic(Rule::Store, site, {state, value}, scopeId);
        g.nodes[store].slot = variable; g.nodes[store].previous = state;
        return {s.syntax == Syntax::Declare ? unit(site) : store, store};
    }
    case Syntax::Unary: {
        if (!functions("."+s.text,scopeId).empty()) {
            Node call = s; call.syntax = Syntax::Call; call.text = "."+s.text;
            call.receiverCount = 1; call.key = "fallback-unary"; call.identity += "/operator";
            return callExpression(g.add(std::move(call)),scopeId,state);
        }
        Built a = build(s.children[0], scopeId, state); state = a.state;
        return simple(Rule::Unary, {a.value});
    }
    case Syntax::Binary: {
        if (s.text != "&&" && s.text != "||" && !functions("."+s.text,scopeId).empty()) {
            Node call = s; call.syntax = Syntax::Call; call.text = "."+s.text;
            call.receiverCount = 1; call.key = "fallback-binary"; call.identity += "/operator";
            return callExpression(g.add(std::move(call)),scopeId,state);
        }
        Id receiver=none;
        Built a;
        if((s.text=="&&" || s.text=="||") && !functions("."+s.text,scopeId).empty()) {
            auto location=place(s.children[0],scopeId,state);bool temporary=!location;
            if(location) a={location->value,location->state};
            else {
                a=build(s.children[0],scopeId,state);
                Id slot=g.semantic(Rule::Slot,site,{a.value},scopeId);
                Id store=g.semantic(Rule::Store,site,{a.state,a.value},scopeId);g.nodes[store].slot=slot;g.nodes[store].previous=a.state;
                a.state=store;location=Place{slot,a.value,store,{}};
            }
            receiver=g.semantic(Rule::Alias,site,location->offsets,scopeId);g.nodes[receiver].slot=location->slot;
            if(temporary) g.nodes[receiver].key="temporary";
        } else a=build(s.children[0], scopeId, state);
        state = a.state;
        if (s.text == "&&" || s.text == "||") {
            Id id = g.semantic(Rule::ShortCircuit, site, {a.value}, snapshot(scopeId, site));
            g.nodes[id].text = s.text; g.nodes[id].previous = state;
            if(receiver!=none) g.nodes[id].receivers={receiver};
            Id output = g.semantic(Rule::StateOf, site, {id}, scopeId);
            return {id, output};
        }
        Built b = build(s.children[1], scopeId, state); state = b.state;
        return simple(Rule::Binary, {a.value, b.value});
    }
    case Syntax::Array:
    case Syntax::Construct:
    case Syntax::Tuple: {
        std::vector<Id> args;
        for (Id arg : s.children) {
            Built b = build(arg, scopeId, state); args.push_back(b.value); state = b.state;
        }
        Built result = simple(s.syntax == Syntax::Construct ? Rule::Construct :
            s.syntax == Syntax::Tuple ? Rule::Tuple : Rule::Array, std::move(args));
        if (s.syntax == Syntax::Construct) {
            Id annotation=type(s.annotation,scopeId,state);
            g.nodes[result.value].annotation=annotation;
            // This descriptor belongs to this constructor's lexical instance.
            // Generic field types can select invariant overloads without
            // executing the constructor or closing its schema first.
            g.nodes[annotation].key="constructor-type";g.nodes[annotation].slot=result.value;
        }
        return result;
    }
    case Syntax::Call: case Syntax::MethodProbe: return callExpression(site,scopeId,state);
    case Syntax::Select:
    case Syntax::Iterate: {
        if (s.syntax == Syntax::Iterate && !functions("."+s.text,scopeId).empty()) {
            Node call = s; call.syntax = Syntax::Call; call.text = "."+s.text;
            call.receiverCount = 1; call.key = "fallback-iterate"; call.identity += "/method";
            return callExpression(g.add(std::move(call)),scopeId,state);
        }
        Built a = build(s.children[0], scopeId, state);
        Id id = g.semantic(s.syntax == Syntax::Select ? Rule::Select : Rule::Iterate, site, {a.value}, snapshot(scopeId, site));
        g.nodes[id].previous = a.state; g.nodes[id].text = s.text;
        Id output = g.semantic(Rule::StateOf, site, {id}, scopeId);
        return {id, output};
    }
    case Syntax::Loop:
    case Syntax::Repeat: {
        std::vector<Id> inputs;
        if (s.text != "condition") {
            Built a = build(s.children[0], scopeId, state); inputs = {a.value}; state = a.state;
        }
        Id id = g.semantic(s.syntax == Syntax::Loop ? Rule::Loop : Rule::Repeat, site, std::move(inputs), snapshot(scopeId, site));
        g.nodes[id].previous = state; g.nodes[id].text = s.text;
        return {id, g.semantic(Rule::StateOf, site, {id}, scopeId)};
    }
    case Syntax::Index: {
        Built a = build(s.children[0], scopeId, state);
        Built b = build(s.children[1], scopeId, a.state); state = b.state;
        return simple(Rule::Index, {a.value, b.value});
    }
    case Syntax::Cast: {
        Built a = build(s.children[1], scopeId, state);
        return finishExpression(adapt(a.value, argumentType(s.children[0],scopeId,state,a.value), site, true), a.state);
    }
    case Syntax::Resource: return simple(Rule::Resource, {});
    default: fail(site, "unsupported structural site in demanded expression");
    }
}
void Solver::exportRoots() {
    for (const auto& [name, definitions] : g.symbols) {
        for (Id decl : definitions) {
            const auto& d = g.nodes[decl];
            if (!d.exported) continue;
            if (d.syntax == Syntax::Global) roots.push_back({name, global(decl), true, true});
            else {
                std::vector<Id> args;
                for (Id param : d.children) {
                    if (g.nodes[param].annotation == none) fail(param, "exported parameters need concrete types");
                    Id input = g.semantic(Rule::Input, param);
                    g.nodes[input].annotation = type(g.nodes[param].annotation);
                    g.nodes[input].text = g.nodes[param].text;
                    args.push_back(input);
                }
                Id callId = g.semantic(Rule::Invoke, decl, std::move(args));
                g.nodes[callId].text = name; g.nodes[callId].candidates = {decl};
                roots.push_back({name, callId, true, false});
            }
        }
    }
    if (roots.empty()) throw Error({}, "no observable roots; export a declaration or select --root/--call");
}
void Solver::root(const std::string& name) {
    auto it = g.symbols.find(name);
    if (it == g.symbols.end() || g.nodes[it->second[0]].syntax != Syntax::Global)
        throw Error({}, "unknown global root: " + name);
    roots.push_back({name, global(it->second[0]), false, true});
}
void Solver::call(const std::string& name, const std::vector<std::string>& args) {
    std::vector<Id> values;
    for (const std::string& arg : args) {
        Big n = 0;
        bool negative = !arg.empty() && arg[0] == '-';
        std::size_t start = negative ? 1 : 0;
        if (arg.size() == start) throw Error({}, "--arg requires a decimal integer");
        for (std::size_t i = start; i < arg.size(); ++i) {
            if (arg[i] < '0' || arg[i] > '9') throw Error({}, "--arg requires a decimal integer");
            n *= 10; n += arg[i] - '0';
            if (n != 0 && boost::multiprecision::msb(n) >= g.options.maxBits) throw Error({}, "argument exceeds bit budget",Diagnostic::Exhausted);
        }
        if (negative) n = -n;
        values.push_back(literal(none, Type::simple(Type::Exact), Value::integer(n)));
    }
    Id id = g.semantic(Rule::Invoke, none, std::move(values)); g.nodes[id].text = name;
    roots.push_back({name, id, false, false});
}
void Solver::enqueue(Id id) {
    auto& n = g.nodes.at(id);
    if (!n.queued && !n.fact) { n.queued = true; queue.push_back(id * 2); }
}
void Solver::demand(Id id) {
    auto& n = g.nodes.at(id);
    if (!n.demanded) { n.demanded = true; enqueue(id); }
}
const Fact* Solver::need(Id owner, Id dependency) {
    auto& n = g.nodes.at(owner);
    if (std::find(n.dependencies.begin(), n.dependencies.end(), dependency) == n.dependencies.end()) {
        n.dependencies.push_back(dependency);
    }
    auto& subscribers = processingType ? g.nodes.at(dependency).valueTypeUsers : g.nodes.at(dependency).users;
    if (std::find(subscribers.begin(), subscribers.end(), owner) == subscribers.end()) subscribers.push_back(owner);
    demand(dependency);
    const auto& f = g.nodes.at(dependency).fact;
    return f ? &*f : nullptr;
}
void Solver::publish(Id id, Fact fact) {
    auto& n = g.nodes.at(id);
    if (n.fact) {
        if (!(n.fact->type == fact.type) || !(n.fact->value == fact.value) || n.fact->effect != fact.effect)
            throw std::logic_error("attempted non-monotone fact update at " + n.identity);
        return;
    }
    n.fact = std::move(fact);
    if (n.completion.normal || n.rule == Rule::FunctionResult) publishType(id, n.fact->type);
    if (g.options.trace) std::cerr << "known " << n.identity << " " << n.fact->type.str() << " = " << n.fact->value.str() << '\n';
    for (Id user : n.users) enqueue(user);
    for (Id user : n.valueTypeUsers) enqueueType(user);
}
std::string Solver::explanation(Id id) const {
    std::map<Id, Id> visited{{id, none}};
    std::deque<Id> todo{id};
    Id found = none;
    while (!todo.empty() && found == none) {
        Id at = todo.front(); todo.pop_front();
        for (const auto& root : roots) if (root.node == at) { found = at; break; }
        for (Id next : g.nodes[at].users) {
            if (visited.emplace(next, at).second) todo.push_back(next);
        }
    }
    if (found == none) return {};
    std::vector<Id> path;
    while (found != id) { path.push_back(found); found = visited.at(found); }
    std::string out;
    unsigned count = 0;
    for (auto it = path.rbegin(); it != path.rend() && count < 8; ++it) {
        const auto& n = g.nodes[*it];
        if (n.rule == Rule::Step || n.rule == Rule::StateOf) continue;
        out += "\n  required by " + ruleName(n.rule) + (n.text.empty() ? "" : " '" + n.text + "'") +
            " at " + g.location(n.span); ++count;
    }
    return out;
}
[[noreturn]] void Solver::fail(Id id, const std::string& message, Diagnostic category) const {
    throw Error(g.nodes.at(id).span, message + explanation(id), category);
}
void Solver::drainWorklist() {
    while (fifoHead < queue.size()) {
        if (++steps > g.options.maxSteps) throw Error({}, "resolution step budget exhausted (--max-steps)",Diagnostic::Exhausted);
        Id id;
        if (g.options.schedule == "fifo") id = queue[fifoHead++];
        else {
            std::size_t index = queue.size() - 1;
            if (g.options.schedule == "random")
                index = std::uniform_int_distribution<std::size_t>(fifoHead, queue.size()-1)(rng);
            id = queue[index]; queue[index] = queue.back(); queue.pop_back();
        }
        bool typeTask = id % 2 != 0; id /= 2;
        auto& n = g.nodes[id];
        processingType = typeTask;
        auto evaluate=[&]() {
            try {if(typeTask) typeRule(id);else rule(id);}
            catch(const TypePropertiesRequired& pending) {
                for(Id property:pending.types) (void)needType(id,property);
                for(Id schema:pending.schemas) (void)need(id,schema);
            }
        };
        try {
        if (typeTask) {
            n.typeQueued = false;
            if ((!n.inferredType || !n.inferredType->settled()) && !n.fact && !n.typeError) {
                try { evaluate(); }
                catch (const Error& error) {
                    n.typeError = error;
                    for (Id subscriber : n.typeUsers)
                        if (subscriber % 2) enqueueType(subscriber / 2); else enqueue(subscriber / 2);
                }
            }
        }
        else { n.queued = false; if (!n.fact) evaluate(); }
        } catch (const ChoiceRequired& request) {
            if (!ownedPuzzles.contains(request.context)) throw;
            processingType = false; branch(request);
        }
        catch (const Error& error) {
            if (n.scope == none || !g.scopes[n.scope].guard.symbolic() || error.category==Diagnostic::Exhausted ||
                error.category==Diagnostic::InvalidReplay || error.category==Diagnostic::SearchAmbiguous) throw;
            try { requestChoice(g.scopes[n.scope].guard,id); }
            catch (const ChoiceRequired& request) {
                if (!ownedPuzzles.contains(request.context)) throw;
                processingType = false; branch(request);
            }
        }
        processingType = false;
    }
}
void Solver::solveWorklist() {
    for (const auto& r : roots) demand(r.node);
    do {
        drainWorklist();
    } while ([&]() {
        try {
            if(inferRecursiveInterfaces() || inferTypeEquations() || verifyTypeEquations() || verifyTypeProperties()) return true;
            bool complete=std::all_of(roots.begin(),roots.end(),[&](const Root& root) {return g.nodes[root.node].fact.has_value();});
            if(complete) return false;
            // A finite branch may be the reason a pending obligation is needed.
            // At propagation fixpoint, explore remaining structural choices
            // before treating that pending obligation as an unresolved result.
            for(const auto& [context,problem]:g.puzzles) {
                const auto& node=g.nodes[context];
                if(node.fact || !node.demanded) continue;
                // A contradiction in the already established relations remains
                // a contradiction when more obligations are added. Conversely,
                // a satisfying partial assignment cannot close the result.
                ConstraintProblem feasibility=*problem;
                auto result=feasibility.solve({});
                if(result.outcome==ConstraintProblem::Outcome::Unsatisfiable) {
                    *problem=std::move(feasibility);
                    fail(context,"constraint system has no solution in its established relations");
                }
                if(result.outcome==ConstraintProblem::Outcome::Exhausted) {
                    *problem=std::move(feasibility);
                    fail(context,"constraint feasibility budget exhausted; remaining obligations are unresolved",Diagnostic::Exhausted);
                }
                for(Id variable=0;variable<problem->variables.size();++variable) {
                    const auto& choice=problem->variables[variable];
                    if(choice.decision && choice.values.size()>1 && !g.options.structuralAssumptions.contains(choice.name))
                        requestChoice(Value::symbolic(context,variable),node.expansion);
                }
            }
            return false;
        }
        catch(const ChoiceRequired& request) {
            if(!ownedPuzzles.contains(request.context)) throw;
            processingType=false;branch(request);return true;
        }
    }());
    for (Id id = 0; id < g.nodes.size(); ++id) {
        const auto& n = g.nodes[id];
        if (n.rule != Rule::Invoke || n.runtimeBackedge == none) continue;
        const auto& target = g.nodes[n.runtimeBackedge];
        if (n.signatureOrigin != none) {
            if (n.inferredType && target.inferredType) {
                Type merged;
                try { merged = common(*n.inferredType,*target.inferredType,id); }
                catch (const Error&) { throw SignatureRevision{n.runtimeBackedge,target.key}; }
                if (!(merged == *n.inferredType)) throw RecursionTypeRequired{n.runtimeBackedge,target.key,merged};
            }
            continue;
        }
        if (!n.fact) continue;
        if (target.fact && target.fact->effect == Effect::External && n.fact->effect != Effect::External)
            throw RecursionEffectRequired{n.body,n.runtimeBackedge};
        if (target.fact && !(n.fact->type == target.fact->type))
            throw RecursionTypeRequired{n.runtimeBackedge,target.key,common(n.fact->type,target.fact->type,id)};
    }
    for (const auto& root : roots) {
        const auto& n = g.nodes[root.node];
        if (!n.fact) {
            std::string message = "unresolved dependency cycle or missing information for '" + root.name + "'";
            std::set<Id> seen;
            Id at = root.node;
            for (unsigned i = 0; i < 16; ++i) {
                if (!seen.insert(at).second) { message += "\n  cycle closes here"; break; }
                const auto& pending = g.nodes[at];
                message += "\n  waiting: " + ruleName(pending.rule) + (pending.text.empty() ? "" : " " + pending.text) +
                    " at " + g.location(pending.span);
                auto next = std::find_if(pending.dependencies.begin(), pending.dependencies.end(), [&](Id d) { return !g.nodes[d].fact; });
                if (next == pending.dependencies.end()) break;
                at = *next;
            }
            fail(root.node, message,Diagnostic::Unresolved);
        }
        const auto& fact = *n.fact;
        if (root.abi && fact.type.kind != Type::Empty && !fact.type.concrete())
            fail(root.node, "observable ABI type is not concrete: " + fact.type.str() + "; add a type annotation");
        if (root.abi && !root.constant && fact.type.kind == Type::Array)
            fail(root.node, "top-level arrays are not supported at function ABI boundaries");
        if (root.abi && !root.constant && fact.type.kind == Type::Record && fact.type.declaration == none)
            fail(root.node,"anonymous tuples are not supported at function ABI boundaries; use a named record");
        if (root.abi && fact.type.kind == Type::String && !fact.value.known())
            fail(root.node, "string results must be compile-time constants");
        if (root.constant && (!fact.value.known() || fact.effect >= Effect::Control))
            fail(root.node, "global requires a constant, effect-free value");
    }
}
} // namespace vx
