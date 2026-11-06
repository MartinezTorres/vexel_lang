#include "vexel.hpp"
#include <algorithm>

namespace vx {
void Solver::loopRule(Id id) {
    auto& n = g.nodes[id];
    if(n.signatureOrigin!=none) {
        if(const Fact* value=need(id,n.signatureOrigin)) {
            n.endState=g.nodes[n.signatureOrigin].endState;n.completion=g.nodes[n.signatureOrigin].completion;
            publish(id,*value);
        }
        return;
    }
    auto get = [&](Id input) { return need(id, input); };
    auto empty = [&](Effect effect) { publish(id, {Type::simple(Type::Empty), {}, effect}); };
    if (n.rule == Rule::LoopHeader) {
        // This boundary supplies a region signature, not an unfinished iteration.
        empty(Effect::Pure); return;
    }
    if (n.rule == Rule::LoopExit) {
        if (const Fact* f = get(n.region)) { n.completion = g.nodes[n.region].completion; empty(f->effect); }
        return;
    }
    if (n.rule == Rule::LoopValue || n.rule == Rule::LoopItem) {
        if (const Fact* f = get(n.inputs[0]))
            publish(id, {n.rule == Rule::LoopItem ? *f->type.element : f->type, Value::runtime(), Effect::Pure});
        return;
    }
    const auto& syntax = g.nodes[n.site];
    auto close = [&](Id state) {
        if (n.key != "closed") { n.key = "closed"; n.endState = resume(state,id,"loop",n.site,n.scope); }
        const Fact* f = get(n.endState); if (!f) return;
        n.completion = g.nodes[n.endState].completion; empty(f->effect);
    };
    if (n.key == "closed") { close(n.endState); return; }
    if (n.rule == Rule::Iterate) {
        const Fact* iterable = get(n.inputs[0]); if (!iterable) return;
        if (iterable->type.kind != Type::Array) fail(id,"unrolled iteration requires a fixed-size array or range", Diagnostic::NotIterable);
        if (n.text == "@@" && iterable->value.symbolic()) requestChoice(iterable->value,id);
        if (n.text == "@@" && !iterable->value.known()) fail(id,"sorted iteration requires compile-time values");
        if (!n.expanded) {
            n.expanded = true; n.endState = n.previous; n.annotation = n.inputs[0];
            if (n.text == "@@") {
                n.annotation=g.semantic(Rule::Sort,n.site,{n.inputs[0]},n.scope);
                g.nodes[n.annotation].previous=n.previous;
            }
        }
        if (n.text=="@@" && !get(n.annotation)) return;
        if (n.expansion != none) {
            if (!get(n.expansion)) return;
            n.endState = n.expansion; n.expansion = none;
        }
        if (!get(n.endState)) return;
        if (n.phase == iterable->type.size || !g.nodes[n.endState].completion.normal) { close(n.endState); return; }
        Id frame = g.scope(n.scope,n.site,n.identity+"/iteration"+std::to_string(n.phase));
        g.scopes[frame].loop = id;
        Id index = literal(n.site,Type::simple(Type::Exact),Value::integer(n.phase++));
        Id item = g.semantic(Rule::Index,n.site,{n.annotation,index},frame);
        Id variable = g.semantic(Rule::Slot,n.site,{item},frame);
        g.nodes[variable].text = "_"; g.nodes[variable].readonly = true; g.scopes[frame].slots["_"] = variable;
        Id store = g.semantic(Rule::Store,n.site,{n.endState,item},frame);
        g.nodes[store].slot = variable; g.nodes[store].previous = n.endState;
        Built body = build(syntax.children[1],frame,store);
        n.branches.push_back(body.value); n.ends.push_back(body.state);
        n.expansion = resume(body.state,id,"continue",n.site,frame);
        (void)get(n.expansion); enqueue(id); return;
    }
    if (n.rule == Rule::Repeat) {
        if (!n.expanded) { n.expanded = true; n.endState = n.previous; }
        if (n.phase == 0) {
            Id frame = g.scope(n.scope, n.site, n.identity + "/test" + std::to_string(n.ends.size() / 2));
            g.scopes[frame].loop = id;
            Built condition = build(syntax.children[0], frame, n.endState);
            n.expansion = g.semantic(Rule::Finish, n.site, {condition.value, condition.state}, frame);
            n.branches.push_back(condition.value); n.ends.push_back(condition.state);
            n.phase = 1;
        }
        const Fact* f = get(n.expansion);
        if (!f) return;
        if (n.phase == 1) {
            if (!g.nodes[n.expansion].completion.normal) { close(n.ends.back()); return; }
            Fact condition = convert(*f, Type::simple(Type::Boolean), false, id);
            if (condition.value.symbolic()) requestChoice(condition.value,id);
            if (!condition.value.known()) fail(id, "repeat condition must be known on every iteration; use !@ for a runtime loop");
            if (condition.value.number == 0) { close(n.ends.back()); return; }
            if (n.ends.size() / 2 >= g.options.maxElements) fail(id, "repeat exceeds iteration budget (--max-elements)",Diagnostic::Exhausted);
            Id frame = g.scope(n.scope, n.site, n.identity + "/iteration" + std::to_string(n.ends.size() / 2));
            g.scopes[frame].loop = id;
            Built body = build(syntax.children[1], frame, n.ends.back());
            n.expansion = g.semantic(Rule::Finish, n.site, {body.value, body.state}, frame);
            n.branches.push_back(body.value); n.ends.push_back(body.state);
            n.phase = 2;
            if (!get(n.expansion)) return;
        }
        if (n.phase == 2) {
            n.expansion = resume(n.ends.back(),id,"continue",n.site,n.scope); n.phase = 3;
        }
        if (!get(n.expansion)) return;
        n.endState = n.expansion;
        if (!g.nodes[n.endState].completion.normal) { close(n.endState); return; }
        n.expansion = none; n.phase = 0;
        enqueue(id); // The next iteration is another worklist obligation.
        return;
    }

    // Establish an entry, a closed signature, one condition/body region, and
    // an exit. Never use the first iteration's values as invariants.
    if (!get(n.previous)) return;
    const bool conditionLoop = n.text == "condition";
    if (!n.expanded) {
        n.expanded = true;
        if (conditionLoop) {
            Id frame = g.scope(n.scope, n.site, n.identity + "/initial-test");
            Built test = build(syntax.children[0], frame, n.previous);
            n.expansion = g.semantic(Rule::Finish, n.site, {test.value, test.state}, frame);
            n.inputs.push_back(n.expansion);
            n.endState = test.state;
        }
    }
    const Fact* initial = get(n.inputs.at(0));
    if (!initial) return;
    if (conditionLoop) {
        Fact condition = convert(*initial, Type::simple(Type::Boolean), false, id);
        if (condition.value.known() && condition.value.number == 0) { empty(initial->effect); return; }
    } else {
        if (initial->type.kind != Type::Array) fail(id, "runtime iteration requires a fixed-size array", Diagnostic::NotIterable);
        if (!initial->type.size) { n.endState = n.previous; empty(initial->effect); return; }
        if (!initial->type.element->concrete())
            fail(id, "runtime iteration needs a concrete element type; annotate the array or a range operand");
    }
    if (n.phase == 0) {
        // Include lexical captures in the write footprint. Ordinary function
        // parameters are copies; expression argument sites are walked in their
        // caller scope. A visited set closes recursive callable relationships.
        std::set<Id> written;
        struct Visit { Id site, scope; std::set<std::string> parameters; };
        std::vector<Visit> todo{{syntax.children[1],n.scope,{}}};
        std::set<std::pair<Id,Id>> visited;
        if (conditionLoop) todo.push_back({syntax.children[0],n.scope,{}});
        while (!todo.empty()) {
            Visit current = std::move(todo.back()); todo.pop_back();
            if (!visited.insert({current.site,current.scope}).second) continue;
            const auto& site = g.nodes[current.site];
            std::string dispatched = site.syntax == Syntax::Call ? site.text :
                site.syntax == Syntax::Binary || site.syntax == Syntax::Unary || site.syntax == Syntax::Iterate ? "."+site.text : "";
            auto callables = dispatched.empty() ? std::vector<Scope::Callable>{} : functions(dispatched,current.scope);
            std::size_t receivers = site.syntax == Syntax::Call ? site.receiverCount : callables.empty() ? 0 : 1;
            for (std::size_t i = 0; i < receivers; ++i) {
                Id base = site.children[i];
                while (g.nodes[base].syntax == Syntax::Index || g.nodes[base].syntax == Syntax::Member) base = g.nodes[base].children[0];
                if (g.nodes[base].syntax == Syntax::Name) {
                    Id receiver = slot(g.nodes[base].text,current.scope);
                    if (receiver != none) {
                        if (g.nodes[receiver].rule == Rule::Alias) receiver = g.nodes[receiver].slot;
                        if (!g.nodes[receiver].readonly) written.insert(receiver);
                    }
                }
            }
            if (site.syntax == Syntax::Name && site.text.starts_with('$')) {
                for (Id scope = current.scope; scope != none; scope = g.scopes[scope].parent) {
                    auto found = g.scopes[scope].expressions.find(site.text);
                    if (found == g.scopes[scope].expressions.end()) continue;
                    todo.push_back({found->second.site,found->second.scope,{}}); break;
                }
            }
            if (site.syntax == Syntax::Assign) {
                Id base = site.children[0];
                while (g.nodes[base].syntax == Syntax::Index || g.nodes[base].syntax == Syntax::Member) base = g.nodes[base].children[0];
                if (g.nodes[base].syntax == Syntax::Name) {
                    Id variable = current.parameters.contains(g.nodes[base].text) ? none : slot(g.nodes[base].text, current.scope);
                    if (variable != none && g.nodes[variable].rule == Rule::Alias) variable = g.nodes[variable].slot;
                    if (variable != none) written.insert(variable);
                }
            }
            if (site.syntax == Syntax::Destructure) for (std::size_t i = 0; i+1 < site.children.size(); ++i) {
                const auto& name = g.nodes[site.children[i]].text;
                Id variable = current.parameters.contains(name) ? none : slot(name,current.scope);
                if (variable != none && g.nodes[variable].rule == Rule::Alias) variable = g.nodes[variable].slot;
                if (variable != none) written.insert(variable);
            }
            for (const auto& fn : callables) {
                if (fn.scope == none) continue;
                std::set<std::string> parameters;
                for (Id param : g.nodes[fn.declaration].children) parameters.insert(g.nodes[param].text);
                todo.push_back({g.nodes[fn.declaration].body,fn.scope,std::move(parameters)});
            }
            for (Id child : site.children) todo.push_back({child,current.scope,current.parameters});
        }
        for (Id variable : written) {
            n.carried.push_back(variable);
            n.seeds.push_back(load(variable, n.previous, n.site));
        }
        n.phase = 1;
    }
    bool ready = true;
    for (Id seed : n.seeds) {
        const Fact* f = get(seed);
        if (!f) ready = false;
        else if (!f->type.concrete())
            fail(id, "runtime loop-carried local needs a concrete type; annotate the local before the loop");
    }
    if (!ready) return;
    if (n.phase == 1) {
        n.region = g.semantic(Rule::LoopHeader, n.site, n.seeds, n.scope);
        g.nodes[n.region].region = id;
        Id state = n.region;
        Id frame = g.scope(n.scope, n.site, n.identity + "/body");
        g.scopes[frame].loop = id;
        if (conditionLoop) {
            Built condition = build(syntax.children[0], frame, state);
            n.branches.push_back(condition.value); n.ends.push_back(condition.state);
            state = condition.state;
        } else {
            Id item = g.semantic(Rule::LoopItem, n.site, {n.inputs[0]}, frame);
            g.nodes[item].region = id; n.annotation = item;
            Id variable = g.semantic(Rule::Slot, n.site, {item}, frame);
            g.nodes[variable].text = "_"; g.nodes[variable].readonly = true;
            g.scopes[frame].slots["_"] = variable;
            Id store = g.semantic(Rule::Store, n.site, {state, item}, frame);
            g.nodes[store].slot = variable; g.nodes[store].previous = state;
            state = store;
        }
        Built body = build(syntax.children[1], frame, state);
        n.branches.push_back(body.value); n.ends.push_back(body.state);
        g.nodes[n.region].runtimeBackedge = body.state;
        n.endState = g.semantic(Rule::LoopExit, n.site, {}, n.scope);
        g.nodes[n.endState].region = id;
        n.phase = 2;
    }
    Effect effect = std::max(Effect::Control, initial->effect);
    for (Id branch : n.branches) {
        if (const Fact* f = get(branch)) effect = std::max(effect, f->effect);
        else ready = false;
    }
    for (Id end : n.ends) {
        if (const Fact* f = get(end)) effect = std::max(effect, f->effect);
        else ready = false;
    }
    if (!ready) return;
    if (conditionLoop) (void)convert(*g.nodes[n.branches[0]].fact, Type::simple(Type::Boolean), false, id);
    n.completion = g.nodes[n.previous].completion;
    for (Id end : n.ends) for (Id exit : g.nodes[end].completion.exits)
        if (g.nodes[exit].region != id) { Completion completion; completion.exits = {exit}; n.completion.include(completion); }
    n.completion.normal = true;
    empty(effect);
}
} // namespace vx
