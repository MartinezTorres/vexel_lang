#include "vexel.hpp"
#include <algorithm>

namespace vx {
bool Solver::hasCompletion(Id site, Id scope) const {
    std::vector<std::pair<Id,Id>> todo{{site,scope}};
    std::set<std::pair<Id,Id>> expressions;
    while (!todo.empty()) {
        auto [at,bindings] = todo.back(); todo.pop_back();
        const auto& source = g.nodes[at];
        if (source.syntax == Syntax::Return) return true;
        if (source.syntax == Syntax::Function) continue;
        if (source.syntax == Syntax::Name && source.text.starts_with('$') && expressions.insert({at,bindings}).second) {
            for (Id frame = bindings; frame != none; frame = g.scopes[frame].parent) {
                auto found = g.scopes[frame].expressions.find(source.text);
                if (found == g.scopes[frame].expressions.end()) continue;
                todo.push_back({found->second.site,found->second.scope}); break;
            }
        }
        for (Id child : source.children) todo.push_back({child,bindings});
    }
    return false;
}
Id Solver::resume(Id state, Id target, const std::string& mode, Id site, Id scope) {
    Id result = g.semantic(Rule::Resume,site,{state},scope);
    g.nodes[result].region = target; g.nodes[result].text = mode;
    return result;
}
Id Solver::resumedLoad(Id variable, Id state, Id site) {
    const auto& boundary = g.nodes[state];
    const auto& before = g.nodes[boundary.inputs[0]];
    std::vector<Id> sources;
    if (before.completion.normal) sources.push_back(load(variable,boundary.inputs[0],site));
    for (Id exit : before.completion.exits) {
        const auto& jump = g.nodes[exit];
        bool consumed = jump.region == boundary.region &&
            (boundary.text == "function" ? jump.text == "return" :
             boundary.text == "continue" ? jump.text == "continue" : jump.text != "return");
        if (consumed) sources.push_back(load(variable,jump.inputs[0],site));
    }
    if (sources.empty()) fail(site,"no continuing path supplies a local value");
    if (sources.size() == 1) return sources[0];
    return g.semantic(Rule::ExitJoin,site,std::move(sources),boundary.scope);
}
void Solver::sequenceStatement(Id id, std::size_t index) {
    auto& sequence = g.nodes[id];
    if (index < sequence.branches.size()) return;
    Id before = index ? sequence.ends.back() : sequence.previous;
    Built statement = build(g.nodes[sequence.site].children.at(index),sequence.scope,before);
    sequence.branches.push_back(statement.value); sequence.ends.push_back(statement.state);
}
void Solver::completionRule(Id id, bool typesOnly) {
    auto& n = g.nodes[id];
    std::map<Id,Fact> properties;
    auto get = [&](Id input) -> const Fact* {
        if (!typesOnly) return need(id,input);
        const Type* type = needType(id,input); if (!type) return nullptr;
        properties[input] = {*type,type->kind == Type::Empty ? Value{} : Value::runtime(),Effect::Pure};
        return &properties[input];
    };
    auto flow = [&](Id input) -> const Completion& {
        return typesOnly && !g.nodes[input].fact ? g.nodes[input].typeCompletion : g.nodes[input].completion;
    };
    Completion& completion = typesOnly ? n.typeCompletion : n.completion;
    auto done = [&](Fact fact) { if (typesOnly) publishType(id,fact.type); else publish(id,std::move(fact)); };
    auto empty = [&](Effect effect) { done({Type::simple(Type::Empty),{},effect}); };
    if(n.rule==Rule::CustomIteration || n.rule==Rule::IterationBody) {
        const Fact* state=get(n.endState);if(!state) return;
        completion=flow(n.endState);empty(state->effect);return;
    }
    if (n.rule == Rule::Evaluate) {
        const Fact* previous = get(n.previous); if (!previous) return;
        completion = flow(n.previous);
        if (!completion.normal) { n.endState = n.previous; empty(previous->effect); return; }
        if (n.expansion == none) {
            Built expression = buildNow(n.site,n.scope,n.previous);
            n.expansion = expression.value; n.endState = expression.state;
        }
        const Fact* state = get(n.endState); if (!state) return;
        completion = flow(n.endState);
        if (!completion.normal) { empty(state->effect); return; }
        const Fact* value = get(n.expansion); if (!value) return;
        Fact result = *value; result.effect = std::max(result.effect,state->effect); done(std::move(result)); return;
    }
    if (n.rule == Rule::Sequence) {
        const auto& source = g.nodes[n.site];
        if (typesOnly) {
            Id previous = n.previous, value = none;
            for (std::size_t i = 0; ; ++i) {
                if (!get(previous)) return;
                completion = flow(previous);
                if (!completion.normal) { empty(Effect::Pure); return; }
                if (i == source.children.size()) {
                    if (value == none) empty(Effect::Pure);
                    else if (const Fact* result = get(value)) done(*result);
                    return;
                }
                sequenceStatement(id,i);
                previous = n.ends[i];
                value = g.nodes[source.children[i]].terminated ? none : n.branches[i];
            }
        }
        while (true) {
            const Fact* state = get(n.endState); if (!state) return;
            n.completion = g.nodes[n.endState].completion;
            if (!n.completion.normal) { empty(state->effect); return; }
            if (n.expansion != none) {
                const Fact* value = get(n.expansion); if (!value) return;
                n.completion.include(g.nodes[n.expansion].completion);
            }
            if (n.phase == source.children.size()) {
                Fact result = n.expansion == none ? Fact{Type::simple(Type::Empty),{},state->effect} : *g.nodes[n.expansion].fact;
                result.effect = std::max(result.effect,state->effect); publish(id,std::move(result)); return;
            }
            Id statement = source.children[n.phase++];
            sequenceStatement(id,n.phase-1);
            n.expansion = g.nodes[statement].terminated ? unit(statement) : n.branches[n.phase-1];
            n.endState = n.ends[n.phase-1];
        }
    }
    if (n.rule == Rule::Exit) {
        const Fact* state = get(n.inputs[0]); if (!state) return;
        completion = flow(n.inputs[0]);
        if (!completion.normal) { empty(state->effect); return; }
        const Fact* value = get(n.inputs[1]); if (!value) return;
        completion.include(flow(n.inputs[1]));
        completion.normal = false;
        completion.exits.push_back(id);
        empty(std::max(state->effect,value->effect)); return;
    }
    if (n.rule == Rule::Resume) {
        const Fact* before = get(n.inputs[0]); if (!before) return;
        const auto& beforeCompletion = flow(n.inputs[0]);
        completion.normal = beforeCompletion.normal; completion.exits.clear();
        for (Id exit : beforeCompletion.exits) {
            const auto& jump = g.nodes[exit];
            bool consumed = jump.region == n.region && (n.text == "function" ? jump.text == "return" :
                n.text == "continue" ? jump.text == "continue" : jump.text != "return");
            if (consumed) completion.normal = true;
            else completion.exits.push_back(exit);
        }
        empty(before->effect); return;
    }
    if (n.rule == Rule::FunctionResult) {
        const Fact* state = get(n.inputs[1]); if (!state) return;
        const auto& bodyCompletion = flow(n.inputs[1]);
        completion.exits.clear();
        std::vector<Id> values;
        if (bodyCompletion.normal) values.push_back(n.inputs[0]);
        for (Id exit : bodyCompletion.exits) {
            const auto& jump = g.nodes[exit];
            if (jump.region != n.region || jump.text != "return") {
                completion.exits.push_back(exit); continue;
            }
            values.push_back(jump.inputs[1]);
        }
        Type result;
        std::optional<Type> firstType;
        bool differingTypes = false;
        if (n.annotation != none) {
            const Fact* target = get(n.annotation); if (!target) return;
            result = target->type;
        }
        for (Id value : values) {
            const Fact* f = get(value); if (!f) return;
            if (firstType && !(*firstType == f->type)) differingTypes = true;
            if (!firstType) firstType = f->type;
        }
        // A constant condition can remove an exit with a different type. Do
        // not publish its conservative join as a final type before resolving
        // that condition's execution context.
        if (typesOnly && differingTypes && n.annotation == none && !g.nodes[n.inputs[1]].fact) {
            // Different promise handles are not yet evidence of different
            // return interfaces. Close those type properties before deciding
            // that selecting a path requires the condition's value.
            for(Id value:values) requireTypeProperties(get(value)->type);
            (void)need(id,n.inputs[1]); return;
        }
        if(n.annotation==none) for(Id value:values) result=common(result,get(value)->type,id);
        else {
            Type target=result;result={};
            for(Id value:values) result=common(result,conversionType(get(value)->type,target,false,id),id);
        }
        if (values.empty()) { completion.normal = false; empty(state->effect); return; }
        if (typesOnly) { publishType(id,result); return; }
        Fact output = convert(*get(values[0]),result,false,id);
        output.effect = std::max(output.effect,state->effect);
        for (Id value : values) {
            Fact f = convert(*get(value),result,false,id);
            if (!(f.value == output.value)) output.value = Value::runtime();
            output.effect = std::max(output.effect,f.effect);
        }
        done(std::move(output)); return;
    }
    if (n.rule == Rule::ExitJoin) {
        std::optional<Fact> result;
        for (Id input : n.inputs) {
            const Fact* f = get(input); if (!f) return;
            if (!result) result = *f;
            else {
                if (!(result->type == f->type)) fail(id,"completion paths disagree on local type");
                if (!(result->value == f->value)) result->value = Value::runtime();
            }
        }
        result->effect = Effect::Pure; done(*result); return;
    }
    throw std::logic_error("unknown completion rule");
}
} // namespace vx
