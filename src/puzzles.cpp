#include "constraints.hpp"
#include <algorithm>
#include <sstream>

namespace vx {
bool Solver::guarded(Id at) const {
    Id scope = g.nodes[at].scope;
    return !evaluatingRelation && scope != none && g.scopes[scope].guard.symbolic();
}
Value Solver::guardedBy(Id scope, const Fact& condition, bool positive, Id at) {
    Fact branch = positive ? condition : unary("!", condition, at);
    Fact parent{Type::simple(Type::Boolean), g.scopes[scope].guard, Effect::Pure};
    if (parent.value.kind == Value::Number) return parent.value.number == 0 ? parent.value : branch.value;
    return relation({parent, branch}, at, [&](const std::vector<Fact>& values) {
        return Fact{Type::simple(Type::Boolean), Value::integer(values[0].value.number != 0 && values[1].value.number != 0), Effect::Pure};
    }, Type::simple(Type::Boolean)).value;
}
Fact Solver::chooseValue(const Fact& condition, const Fact& yes, const Fact& no, Id at) const {
    Type type = common(yes.type, no.type, at);
    Fact a = convert(yes, type, false, at), b = convert(no, type, false, at);
    Effect effect = std::max(condition.effect, std::max(a.effect, b.effect));
    if (a.value == b.value) return {type, a.value, effect};
    if (type.kind == Type::Array || type.kind == Type::Record) {
        std::vector<Value> elements;
        for (std::size_t i = 0; i < type.size; ++i) {
            const Type& field = type.kind == Type::Array ? *type.element : type.fields[i];
            elements.push_back(chooseValue(condition, {field, a.value.kind == Value::Elements ? a.value.elements.at(i) : Value::runtime(), a.effect},
                {field, b.value.kind == Value::Elements ? b.value.elements.at(i) : Value::runtime(), b.effect}, at).value);
        }
        return {type, Value::array(std::move(elements)), effect};
    }
    return relation({condition, a, b}, at, [&](const std::vector<Fact>& values) {
        return values[0].value.number != 0 ? values[1] : values[2];
    }, type);
}
Fact Solver::symbolicIndex(const Fact& array, const Fact& index, Id at) const {
    const auto size = array.type.kind == Type::String ? array.value.bytes.size() : array.type.size;
    const Type element = array.type.kind == Type::String ? Type::integer(8, false) : *array.type.element;
    (void)relation({index}, at, [&](const std::vector<Fact>& inputs) {
        if (inputs[0].value.number < 0 || inputs[0].value.number >= size) fail(at, "index out of bounds",Diagnostic::ValueTrap);
        return Fact{Type::simple(Type::Boolean), Value::integer(1), Effect::Pure};
    }, Type::simple(Type::Boolean));
    auto item = [&](std::size_t i) -> Fact {
        Value value = array.value.kind == Value::Bytes ? Value::integer(static_cast<unsigned char>(array.value.bytes[i])) :
            array.value.kind == Value::Elements ? array.value.elements[i] : Value::runtime();
        return {element, value, std::max(array.effect, index.effect)};
    };
    if (!size) return {element, Value::runtime(), array.effect}; // The bounds relation is contradictory.
    Fact result = item(0);
    for (std::size_t i = 1; i < size; ++i) {
        Fact condition = relation({index}, at, [&](const std::vector<Fact>& inputs) {
            return Fact{Type::simple(Type::Boolean), Value::integer(inputs[0].value.number == i), Effect::Pure};
        }, Type::simple(Type::Boolean));
        result = chooseValue(condition, item(i), result, at);
    }
    return result;
}
Fact Solver::relation(const std::vector<Fact>& inputs, Id at,
        const std::function<Fact(const std::vector<Fact>&)>& operation, Type expected) const {
    if (expected.kind == Type::Array || expected.kind == Type::Record) {
        std::vector<std::size_t> path;
        std::size_t leaves=0;
        std::function<Value(const Type&)> project = [&](const Type& type) -> Value {
            if (type.kind == Type::Array || type.kind == Type::Record) {
                std::vector<Value> fields;
                for (std::size_t i=0;i<type.size;++i) {
                    path.push_back(i); fields.push_back(project(type.kind==Type::Array ? *type.element : type.fields[i])); path.pop_back();
                }
                return Value::array(std::move(fields));
            }
            ++leaves;
            return relation(inputs,at,[&](const std::vector<Fact>& values) {
                Fact output=operation(values);
                if (!(output.type==expected)) fail(at,"finite relation has candidate-dependent result types");
                Value value=output.value;
                for (std::size_t index : path) { Value child=value.elements.at(index);value=std::move(child); }
                return Fact{type,value,output.effect};
            },type).value;
        };
        Value value=project(expected);
        if (!leaves) (void)relation(inputs,at,[&](const std::vector<Fact>& values) {
            Fact output=operation(values);
            if (!(output.type==expected)) fail(at,"finite relation has candidate-dependent result types");
            return Fact{Type::simple(Type::Boolean),Value::integer(1),output.effect};
        },Type::simple(Type::Boolean));
        Effect effect=Effect::Pure;
        for (const auto& input : inputs) effect=std::max(effect,input.effect);
        return {expected,std::move(value),effect};
    }
    Id context = none;
    std::vector<Id> variables;
    std::function<void(const Value&)> collect = [&](const Value& v) {
        if (v.kind == Value::Symbolic) {
            if (context != none && context != v.context) fail(at, "symbolic values cannot escape their solve context");
            context = v.context;
            if (std::find(variables.begin(), variables.end(), v.variable) == variables.end()) variables.push_back(v.variable);
        }
        for (const auto& item : v.elements) collect(item);
    };
    for (const auto& input : inputs) collect(input.value);
    Value guard = Value::integer(1);
    if (guarded(at)) { guard = g.scopes[g.nodes[at].scope].guard; collect(guard); }
    if (context == none) throw std::logic_error("finite relation without symbolic inputs");
    auto& p = *g.puzzles.at(context);
    std::size_t combinations = 1;
    for (Id v : variables) {
        std::size_t size = p.variables[v].values.size();
        if (combinations > g.options.maxConstraintChecks / size || combinations * size > 1000000) {
            p.result.outcome=ConstraintProblem::Outcome::Exhausted;
            fail(at, "finite relation expansion budget exhausted; split the relation into smaller expressions",Diagnostic::Exhausted);
        }
        combinations *= size;
    }
    std::map<Id, Big> assignment;
    std::function<Value(const Value&)> substitute = [&](const Value& v) -> Value {
        if (v.kind == Value::Symbolic) return Value::integer(assignment.at(v.variable));
        Value result = v;
        for (auto& item : result.elements) item = substitute(item);
        return result;
    };
    std::vector<std::vector<std::size_t>> rows;
    std::vector<Big> outputs;
    std::vector<Big> rowOutputs;
    std::optional<Type> outputType = expected.kind == Type::Unknown ? std::optional<Type>{} : expected;
    Effect effect = Effect::Pure;
    for (const auto& input : inputs) effect = std::max(effect, input.effect);
    std::string lastError;
    for (std::size_t tuple = 0; tuple < combinations; ++tuple) {
        std::size_t remaining = tuple;
        std::vector<std::size_t> row;
        for (Id v : variables) {
            std::size_t selected = remaining % p.variables[v].values.size(); remaining /= p.variables[v].values.size();
            row.push_back(selected); assignment[v] = p.variables[v].values[selected];
        }
        auto concrete = inputs;
        for (auto& f : concrete) f.value = substitute(f.value);
        Fact out;
        if (substitute(guard).number == 0) {
            rows.push_back(std::move(row)); outputs.push_back(0); rowOutputs.push_back(0); continue;
        }
        struct Evaluation {
            bool& flag; bool previous;
            explicit Evaluation(bool& f) : flag(f), previous(f) { flag = true; }
            ~Evaluation() { flag = previous; }
        } evaluation(evaluatingRelation);
        try { out = operation(concrete); }
        catch (const Error& error) {
            // Only a failed evaluation rules out an assignment. A malformed
            // relation or exhausted implementation budget is not a proof.
            std::string message = error.what();
            if (error.category!=Diagnostic::ValueTrap) throw;
            lastError = std::move(message); continue;
        }
        if (out.value.kind != Value::Number) fail(at, "finite relation must produce a scalar number or boolean");
        if (outputType && !(*outputType == out.type)) fail(at, "finite relation has candidate-dependent result types");
        outputType = out.type;
        rows.push_back(std::move(row)); outputs.push_back(out.value.number); rowOutputs.push_back(out.value.number);
    }
    if (!outputType || outputs.empty()) {
        // An empty relation is a scoped contradiction, never a valid answer.
        Type fallback = outputType ? *outputType : inputs[0].type;
        Id out = p.variable(g.identity(at) + "/impossible", {0});
        p.relation({out}, {}, g.nodes[at].span, lastError.empty() ? "no admissible evaluation" : lastError);
        return {fallback, Value::symbolic(context, out), effect};
    }
    Id out = p.variable(g.nodes[at].identity + "/result", std::move(outputs));
    const auto& domain = p.variables[out].values;
    for (std::size_t i = 0; i < rows.size(); ++i)
        rows[i].push_back(static_cast<std::size_t>(std::lower_bound(domain.begin(), domain.end(), rowOutputs[i])-domain.begin()));
    variables.push_back(out);
    p.relation(std::move(variables), std::move(rows), g.nodes[at].span, g.nodes[at].text);
    if (domain.size() == 1 && !guarded(at)) return {*outputType,Value::integer(domain[0]),effect};
    return {*outputType, Value::symbolic(context, out), effect};
}
void Solver::puzzleRule(Id id) {
    auto& n = g.nodes[id];
    if (n.rule == Rule::Solve) {
        if (!n.expanded) {
            checkpoint(id);
            n.expanded = true;
            g.puzzles[id] = std::make_shared<ConstraintProblem>(g.options,n.identity);
            Id scope = g.scope(n.scope, n.site, g.nodes[id].identity + "/puzzle");
            g.scopes[scope].puzzle = id;
            Built body = build(g.nodes[n.site].children[0], scope, n.previous);
            n.expansion = g.semantic(Rule::Finish, n.site, {body.value, body.state}, scope);
        }
        const Fact* value = need(id, n.expansion); if (!value) return;
        if (value->effect >= Effect::Control) fail(id, "solve regions require a closed pure calculation");
        std::vector<Id> projection;
        std::function<void(const Value&)> collect = [&](const Value& v) {
            if (v.kind == Value::Runtime) fail(id, "solve region depends on runtime input");
            if (v.kind == Value::Symbolic) {
                if (v.context != id) fail(id, "nested solve cannot capture unresolved choices from its parent");
                projection.push_back(v.variable);
            }
            for (const auto& item : v.elements) collect(item);
        };
        collect(value->value);
        auto& p = *g.puzzles.at(id);
        auto result = p.solve(projection);
        if (result.outcome == ConstraintProblem::Outcome::Exhausted) fail(id, "constraint search budget exhausted; remaining alternatives are unresolved",Diagnostic::Exhausted);
        if (result.outcome == ConstraintProblem::Outcome::Unsatisfiable) {
            std::string why;
            if (result.conflict != none) why = "\n  conflict at " + g.location(p.relations[result.conflict].source) + ": " + p.relations[result.conflict].label;
            fail(id, "constraint system has no solution" + why);
        }
        if (result.outcome == ConstraintProblem::Outcome::Ambiguous) fail(id, "constraint result is ambiguous: at least two observable answers satisfy the system",Diagnostic::SearchAmbiguous);
        std::function<Value(const Value&)> materialize = [&](const Value& v) -> Value {
            if (v.kind == Value::Symbolic) return Value::integer(result.assignment.at(v.variable));
            Value out = v;
            for (auto& item : out.elements) item = materialize(item);
            return out;
        };
        publish(id, {value->type, materialize(value->value), value->effect}); return;
    }
    bool ready = true;
    for (Id input : n.inputs) if (!need(id, input)) ready = false;
    if (!ready) return;
    Id context = g.scopes[n.scope].puzzle;
    auto& p = *g.puzzles.at(context);
    const auto& input = *g.nodes[n.inputs[0]].fact;
    if (input.effect >= Effect::Control) fail(id, "constraint input must be pure");
    if (n.rule == Rule::Require) {
        Fact condition = convert(input, Type::simple(Type::Boolean), false, id);
        Value guard = g.scopes[n.scope].guard;
        if (guard.kind == Value::Symbolic) {
            std::vector<Id> variables{guard.variable};
            if (condition.value.kind == Value::Symbolic) variables.push_back(condition.value.variable);
            std::vector<std::vector<std::size_t>> tuples;
            const auto& gd = p.variables[guard.variable].values;
            for (std::size_t i = 0; i < gd.size(); ++i) {
                if (variables.size() == 1) {
                    if (gd[i] == 0 || (condition.value.kind == Value::Number && condition.value.number != 0)) tuples.push_back({i});
                } else {
                    const auto& cd = p.variables[condition.value.variable].values;
                    for (std::size_t j = 0; j < cd.size(); ++j) if (gd[i] == 0 || cd[j] != 0) tuples.push_back({i,j});
                }
            }
            p.relation(std::move(variables), std::move(tuples), n.span, "guarded predicate");
            publish(id, {Type::simple(Type::Empty), {}, input.effect}); return;
        }
        if (condition.value.kind == Value::Symbolic) {
            if (condition.value.context != context) fail(id, "constraint crosses an unresolved solve context");
            p.require(condition.value.variable, 1, n.span, "required predicate");
        } else if (condition.value.kind == Value::Number) {
            if (!condition.value.number) p.relation({}, {}, n.span, "required predicate is false");
        } else fail(id, "constraint predicate depends on runtime input");
        publish(id, {Type::simple(Type::Empty), {}, input.effect}); return;
    }
    const Type target = g.nodes[n.inputs[1]].fact->type;
    if (input.type.kind != Type::Array || !input.value.known()) fail(id, "finite choice requires a constant array domain");
    std::size_t created = 0;
    std::function<Value(const Type&)> make = [&](const Type& t) -> Value {
        if (t.kind == Type::Array || t.kind == Type::Record) {
            std::vector<Value> values;
            for (std::size_t i = 0; i < t.size; ++i) values.push_back(make(t.kind == Type::Array ? *t.element : t.fields[i]));
            return Value::array(std::move(values));
        }
        if (t.kind != Type::Integer && t.kind != Type::Fixed && t.kind != Type::Float && t.kind != Type::Boolean) fail(id, "finite choices require numeric or boolean scalar elements");
        if (++created > g.options.maxElements) fail(id, "choice shape exceeds element budget",Diagnostic::Exhausted);
        std::vector<Big> domain;
        for (const auto& item : input.value.elements) {
            Fact candidate = convert({*input.type.element, item, Effect::Pure}, t, false, id);
            if (candidate.value.kind != Value::Number) fail(id, "choice domain must contain scalar numbers");
            domain.push_back(candidate.value.number);
        }
        std::string key = n.identity + "/" + std::to_string(created-1);
        if (auto assumption = g.options.structuralAssumptions.find(key); assumption != g.options.structuralAssumptions.end()) {
            if (std::find(domain.begin(),domain.end(),assumption->second) == domain.end()) fail(id,"structural choice is outside its current domain");
            p.variable(key,{assumption->second},true);
            return Value::integer(assumption->second);
        }
        Id var = p.variable(key, std::move(domain), true);
        if (p.variables[var].values.size() == 1) return Value::integer(p.variables[var].values[0]);
        return Value::symbolic(context, var);
    };
    publish(id, {target, make(target), input.effect});
}
} // namespace vx
