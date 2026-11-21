#include "machine.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace vx::relational {
namespace {
using TermPair=std::pair<const Term*,const Term*>;
struct TermPairHash {
    std::size_t operator()(const TermPair& pair) const {
        auto a=std::hash<const Term*>{}(pair.first),b=std::hash<const Term*>{}(pair.second);
        return a^(b+0x9e3779b9U+(a<<6)+(a>>2));
    }
};
// Shallow patterns dominate clause matching and unification. Keep their few
// exact pairs inline; larger shared graphs still use a complete hash set.
// This memo is local to one binding view, just like the traversal it serves.
class TermPairs {
    std::array<TermPair,8> small;
    std::size_t count=0;
    std::unordered_set<TermPair,TermPairHash> large;
public:
    bool insert(const Term* a,const Term* b) {
        const TermPair pair{a,b};
        if(count<=small.size()) {
            if(std::find(small.begin(),small.begin()+static_cast<std::ptrdiff_t>(count),pair)!=
               small.begin()+static_cast<std::ptrdiff_t>(count)) return false;
            if(count<small.size()) {small[count++]=pair;return true;}
            large.insert(small.begin(),small.end());++count;
        }
        return large.insert(pair).second;
    }
    void clear() {
        if(count>small.size()) large.clear();
        count=0;
    }
};
// A rewrite owns its memo only for one immutable binding view or clause
// instantiation. Shared inputs produce shared outputs; untouched structure is
// retained. No result can survive rollback into a different binding context.
template<class Prepare>
Ref rewriteTerms(const Ref& input,Prepare prepare) {
    auto root=prepare(input);
    auto leaf=[](const Ref& value){return value->closed || value->kind!=Term::Constructor || value->arguments.empty();};
    if(leaf(root)) return root;
    struct Frame {Ref term;std::vector<Ref> arguments;};
    auto finish=[](Frame& frame)->Ref {
        if(frame.arguments==frame.term->arguments) return frame.term;
        auto changed=std::make_shared<Term>(*frame.term);
        changed->arguments=std::move(frame.arguments);
        changed->closed=std::all_of(changed->arguments.begin(),changed->arguments.end(),ground);
        return changed;
    };
    // Most relation patterns are shallow. They need no traversal stack or
    // hash allocation when every rewritten child is already a leaf.
    Frame first{root,{}};
    for(const auto& argument:root->arguments) {
        auto child=prepare(argument);
        if(!leaf(child)) break;
        first.arguments.push_back(std::move(child));
    }
    if(first.arguments.size()==root->arguments.size()) return finish(first);
    std::vector<Frame> pending;pending.push_back(std::move(first));
    std::unordered_map<const Term*,Ref> memo;
    while(!pending.empty()) {
        auto& frame=pending.back();
        if(frame.arguments.size()<frame.term->arguments.size()) {
            auto child=prepare(frame.term->arguments[frame.arguments.size()]);
            if(leaf(child)) frame.arguments.push_back(std::move(child));
            else if(auto found=memo.find(child.get());found!=memo.end()) frame.arguments.push_back(found->second);
            else pending.push_back({std::move(child),{}});
            continue;
        }
        auto result=finish(frame);
        if(pending.size()==1) return result;
        memo.emplace(frame.term.get(),result);pending.pop_back();
        pending.back().arguments.push_back(std::move(result));
    }
    throw std::logic_error("empty term rewrite");
}
}
Ref Terms::variable(std::string name) {
    auto term = std::make_shared<Term>(); term->kind = Term::Variable;
    term->variable = next++; term->name = std::move(name); return term;
}
Ref Terms::number(Integer value) {
    Ref* cached=value==0 ? &zero : value==1 ? &one : nullptr;
    if(cached && *cached) return *cached;
    auto term = std::make_shared<Term>(); term->kind = Term::Number;term->closed=true;
    term->number = std::move(value);
    if(cached) *cached=term;
    return term;
}
Ref Terms::make(std::string name, std::vector<Ref> arguments) {
    auto term = std::make_shared<Term>(); term->kind = Term::Constructor;
    term->name = std::move(name); term->arguments = std::move(arguments);term->closed=std::all_of(term->arguments.begin(),term->arguments.end(),ground);return term;
}
namespace {
std::string renderTerm(const Ref& root,std::size_t limit) {
    struct Frame {const Term* term;std::size_t next=0;};
    std::vector<Frame> pending;std::string result;bool truncated=false;
    auto append=[&](std::string_view text) {
        const auto count=std::min(text.size(),limit-result.size());
        result.append(text.data(),count);truncated|=count!=text.size();
    };
    auto begin=[&](const Term* term) {
        if(term->kind==Term::Variable) append("?"+term->name+":"+std::to_string(term->variable));
        else if(term->kind==Term::Number) append(term->number.str());
        else {
            append(term->name);
            if(!truncated && !term->arguments.empty()) {append("(");pending.push_back({term});}
        }
    };
    begin(root.get());
    while(!pending.empty() && !truncated) {
        auto& frame=pending.back();
        if(frame.next==frame.term->arguments.size()) {append(")");pending.pop_back();continue;}
        if(frame.next) append(",");
        const auto child=frame.term->arguments[frame.next++].get();
        if(!truncated) begin(child);
    }
    if(truncated) result+="...";
    return result;
}
}
std::string show(const Ref& term) {
    return renderTerm(term,std::numeric_limits<std::size_t>::max());
}
std::string show(const Ref& term,std::size_t limit) {return renderTerm(term,limit);}
bool identical(const Ref& left, const Ref& right) {
    if (left == right) return true;
    if (left->kind != right->kind) return false;
    if (left->kind == Term::Variable) return left->variable == right->variable;
    if (left->kind == Term::Number) return left->number == right->number;
    if (left->name != right->name || left->arguments.size() != right->arguments.size()) return false;
    if (left->arguments.empty()) return true;
    std::vector<TermPair> pending;
    TermPairs visited;
    pending.emplace_back(left.get(),right.get());
    while(!pending.empty()) {
        auto [a,b]=pending.back();pending.pop_back();
        if(a==b) continue;
        if(a->kind!=b->kind) return false;
        if(a->kind==Term::Variable) {if(a->variable!=b->variable) return false;continue;}
        if(a->kind==Term::Number) {if(a->number!=b->number) return false;continue;}
        if(a->name!=b->name || a->arguments.size()!=b->arguments.size()) return false;
        if(!visited.insert(a,b)) continue;
        for(std::size_t i=a->arguments.size();i>0;--i)
            pending.emplace_back(a->arguments[i-1].get(),b->arguments[i-1].get());
    }
    return true;
}
bool ground(const Ref& term) {
    return term->closed;
}
std::size_t RetryEffort::next(std::size_t ordinary,std::size_t ceiling) {
    if(attempts<std::numeric_limits<std::size_t>::max()) ++attempts;
    if(!ordinary || !ceiling) return 0;
    if(ordinary>=ceiling) return ceiling;
    if(attempts&(attempts-1)) return ordinary;
    return attempts>ceiling/ordinary ? ceiling : attempts*ordinary;
}
void Bindings::Transaction::value(Id id) {
    if(old.values.contains(id)) return;
    auto found=bindings.values.find(id);old.values[id]=found==bindings.values.end() ? Ref{} : found->second;
}
void Bindings::Transaction::domain(Id id) {
    if(old.domains.contains(id)) return;
    auto found=bindings.domains.find(id);old.domains[id]=found==bindings.domains.end() ? std::nullopt : std::optional(found->second);
}
void Bindings::Transaction::integer(Id id) {if(!bindings.integers.contains(id)) old.integers.insert(id);}
Bindings::Transaction::~Transaction() {
    for(const auto& [id,value]:old.values) {if(value) bindings.values[id]=value;else bindings.values.erase(id);}
    for(const auto& [id,domain]:old.domains) {if(domain) bindings.domains[id]=*domain;else bindings.domains.erase(id);}
    for(Id id:old.integers) bindings.integers.erase(id);
    bindings.revision=revision;bindings.transaction=previous;bindings.touched=std::move(touched);
}
Bindings::Patch Bindings::Transaction::changes() const {
    Patch patch;patch.integers=old.integers;
    for(const auto& [id,unused]:old.values) {(void)unused;auto found=bindings.values.find(id);patch.values[id]=found==bindings.values.end() ? Ref{} : found->second;}
    for(const auto& [id,unused]:old.domains) {(void)unused;auto found=bindings.domains.find(id);patch.domains[id]=found==bindings.domains.end() ? std::nullopt : std::optional(found->second);}
    return patch;
}
void Bindings::setValue(Id id,Ref value) {
    if(transaction) transaction->value(id);
    if(value) values[id]=std::move(value);else values.erase(id);touched.insert(id);++revision;
}
void Bindings::setDomain(Id id,std::optional<std::set<Integer>> domain) {
    if(transaction) transaction->domain(id);
    if(domain) domains[id]=std::move(*domain);else domains.erase(id);touched.insert(id);++revision;
}
void Bindings::setInteger(Id id) {
    if(integers.contains(id)) return;
    if(transaction) transaction->integer(id);
    integers.insert(id);touched.insert(id);++revision;
}
void Bindings::apply(const Patch& patch) {
    for(const auto& [id,value]:patch.values) setValue(id,value);
    for(const auto& [id,domain]:patch.domains) setDomain(id,domain);
    for(Id id:patch.integers) setInteger(id);
}
Ref Bindings::walk(Ref term) const {
    while (term->kind == Term::Variable) {
        auto found = values.find(term->variable);
        if (found == values.end()) break;
        term = found->second;
    }
    return term;
}
Ref Bindings::resolve(const Ref& input) const {
    return rewriteTerms(input,[&](const Ref& term){return walk(term);});
}
bool Bindings::occurs(Id variable, const Ref& input) const {
    auto term = walk(input);
    // Immutable ground structure contains no promise, including through a
    // shared subgraph. Its contents cannot acquire bindings in a later branch.
    if (term->closed) return false;
    if (term->kind == Term::Variable) return term->variable == variable;
    std::vector<Ref> pending{term};std::unordered_set<const Term*> visited;
    while(!pending.empty()) {
        auto value=walk(pending.back());pending.pop_back();
        if(value->closed) continue;
        if(value->kind==Term::Variable) {if(value->variable==variable) return true;continue;}
        if(!visited.insert(value.get()).second) continue;
        pending.insert(pending.end(),value->arguments.begin(),value->arguments.end());
    }
    return false;
}
bool Bindings::integer(const Ref& input) {
    auto term = walk(input);
    if (term->kind == Term::Constructor) return false;
    if (term->kind == Term::Variable) setInteger(term->variable);
    return true;
}
bool Bindings::constrain(Ref term,const std::set<Integer>& domain) {
    term=walk(term);
    if(term->kind==Term::Constructor) return false;
    if(term->kind==Term::Number) return domain.contains(term->number);
    auto narrowed=domain;
    if(auto found=domains.find(term->variable);found!=domains.end()) {
        std::set<Integer> intersection;
        std::set_intersection(narrowed.begin(),narrowed.end(),found->second.begin(),found->second.end(),std::inserter(intersection,intersection.end()));
        narrowed=std::move(intersection);
    }
    if(narrowed.empty()) return false;
    setInteger(term->variable);
    if(narrowed.size()==1) {
        auto value=std::make_shared<Term>();value->kind=Term::Number;value->closed=true;value->number=*narrowed.begin();
        return unify(term,value);
    }
    auto found=domains.find(term->variable);
    if(found==domains.end() || found->second!=narrowed) {setDomain(term->variable,std::move(narrowed));}
    return true;
}
bool Bindings::unify(Ref left, Ref right) {
    if(left==right) return true;
    // Retain the roots while raw pointer pairs are memoized. Constraint work
    // may allocate new terms; allocator address reuse must never alias a pair.
    std::vector<std::pair<Ref,Ref>> pending;
    TermPairs visited;
    auto merge=[&](Ref a,Ref b) {
        a=walk(std::move(a));b=walk(std::move(b));
        if(a==b || (a->kind==Term::Variable && b->kind==Term::Variable && a->variable==b->variable)) return true;
        if(a->kind!=Term::Variable && b->kind==Term::Variable) std::swap(a,b);
        // A fresh interface aliases the existing promise, preserving its root.
        if(a->kind==Term::Variable && b->kind==Term::Variable && a->variable<b->variable) std::swap(a,b);
        if(a->kind==Term::Variable) {
            if(integers.contains(a->variable) && !integer(b)) return false;
            if(occurs(a->variable,b)) return false;
            if(auto found=domains.find(a->variable);found!=domains.end()) {
                auto domain=found->second;
                if(!constrain(b,domain)) return false;
                b=walk(b);setDomain(a->variable,std::nullopt);
            }
            setValue(a->variable,b);return true;
        }
        if(a->kind!=b->kind) return false;
        if(a->kind==Term::Number) {if(a->number!=b->number) return false;return true;}
        if(a->name!=b->name || a->arguments.size()!=b->arguments.size()) return false;
        if(a->arguments.empty()) return true;
        // Within one call bindings only accumulate constraints. A pair whose
        // children have already unified cannot become unequal without a later
        // binding itself failing. Never retain this memo across transactions.
        if(!visited.insert(a.get(),b.get())) return true;
        for(std::size_t i=a->arguments.size();i>0;--i)
            pending.emplace_back(a->arguments[i-1],b->arguments[i-1]);
        return true;
    };
    if(!merge(left,right)) return false;
    while(!pending.empty()) {
        auto [a,b]=std::move(pending.back());pending.pop_back();
        if(!merge(std::move(a),std::move(b))) return false;
    }
    return true;
}

Goal equal(Ref a, Ref b) { return {Goal::Equal, {a,b}, "equality"}; }
Goal finite(Ref value,const std::vector<Ref>& domain) {auto args=domain;args.insert(args.begin(),std::move(value));return {Goal::Finite,std::move(args),"finite domain"};}
Goal different(Ref a, Ref b) { return {Goal::Different, {a,b}, "difference"}; }
Goal add(Ref a, Ref b, Ref c) { return {Goal::Add, {a,b,c}, "sum"}; }
Goal multiply(Ref a, Ref b, Ref c) { return {Goal::Multiply, {a,b,c}, "product"}; }
Goal modulo(Ref a, Ref b, Ref c) { return {Goal::Modulo, {a,b,c}, "nonnegative remainder"}; }
Goal compute(std::string op, Ref a, Ref b, Ref result) {return {Goal::Compute,{a,b,result},std::move(op)};}
Goal booleanOr(Ref a, Ref b, Ref result) {return {Goal::BooleanOr,{a,b,result},"boolean disjunction"};}
Goal lessEqual(Ref a, Ref b) { return {Goal::LessEqual, {a,b}, "bound"}; }
Goal disjoint(Ref a, Ref n, Ref b, Ref m) { return {Goal::Disjoint, {a,n,b,m}, "disjoint storage"}; }
Goal call(std::string name, std::vector<Ref> arguments) { return {Goal::Call, std::move(arguments), std::move(name)}; }
Goal require(std::string name) { return {Goal::Require, {}, std::move(name)}; }
Goal build(std::string name, std::vector<Ref> arguments) { return {Goal::Build, std::move(arguments), std::move(name)}; }
Goal witness(std::string name, Ref artifact) {return {Goal::Witness,{std::move(artifact)},std::move(name)};}
Goal ambiguity(std::string reason) { return {Goal::Ambiguity, {}, std::move(reason)}; }
Goal unknown(std::string reason) { return {Goal::Unknown, {}, std::move(reason)}; }
Goal awaitInput(Ref value,std::string name) { return {Goal::AwaitInput,{std::move(value)},std::move(name)}; }
void Program::define(std::string name, std::size_t arity, std::vector<Clause> clauses, bool closed) {
    if (relations.contains(name)) throw std::invalid_argument("duplicate relation: " + name);
    for (const auto& clause : clauses)
        if (clause.head.size() != arity) throw std::invalid_argument("clause arity: " + name);
    relations.emplace(std::move(name), Relation{arity, closed, std::move(clauses)});
}
void Program::domain(std::string name, const std::vector<Ref>& values) {
    std::vector<Clause> clauses;
    // Labels explain a choice; they do not identify or compare its value.
    // A shared term may have exponentially many printed paths. Bound only
    // this diagnostic rendering, retaining the complete head for solving.
    for (const auto& value : values) clauses.push_back({{value}, {}, renderTerm(value,256)});
    define(std::move(name), 1, std::move(clauses));
}
std::string outcomeName(Outcome outcome) {
    switch (outcome) {
    case Outcome::Resolved: return "resolved";
    case Outcome::Contradictory: return "contradictory";
    case Outcome::Ambiguous: return "ambiguous";
    case Outcome::Unresolved: return "unresolved";
    case Outcome::Exhausted: return "exhausted";
    }
    throw std::logic_error("unknown outcome");
}
Machine::Machine(Program& source, Options settings)
    : program(source), options(std::move(settings)), random(options.seed) {
    if (options.schedule != "fifo" && options.schedule != "lifo" && options.schedule != "random")
        throw std::invalid_argument("unknown worklist schedule");
    if (options.branches != "forward" && options.branches != "reverse" && options.branches != "random")
        throw std::invalid_argument("unknown branch order");
}
bool Machine::tick() {
    if (result.stats.work >= options.maxWork || (options.budget && options.budget->work>=options.budget->maxWork)) { exhausted = true; return false; }
    ++result.stats.work;if(options.budget) ++options.budget->work;return true;
}
Ref Machine::fresh(const Ref& term, std::map<Id, Ref>& variables) {
    return rewriteTerms(term,[&](const Ref& value) {
        if(value->kind!=Term::Variable) return value;
        auto [entry,inserted]=variables.try_emplace(value->variable);
        if(inserted) entry->second=program.terms.variable(value->name);
        return entry->second;
    });
}
std::vector<Machine::Expansion> Machine::expand(Context& context, std::size_t index) {
    const auto& goal=context.goals.at(index);std::vector<Expansion> alternatives;
    auto found=program.relations.find(goal.label);
    if(found!=program.relations.end()) {
        const auto& relation=found->second;
        if(relation.arity!=goal.arguments.size()) throw std::invalid_argument("call arity: "+goal.label);
        TermPairs compatiblePairs;
        std::function<bool(Ref,const Ref&)> compatible=[&](Ref supplied,const Ref& pattern) {
            supplied=context.bindings.walk(supplied);
            if(supplied->kind==Term::Variable || pattern->kind==Term::Variable) return true;
            if(supplied==pattern) return true;
            if(supplied->kind!=pattern->kind) return false;
            if(supplied->kind==Term::Number) return supplied->number==pattern->number;
            if(supplied->name!=pattern->name || supplied->arguments.size()!=pattern->arguments.size()) return false;
            if(!compatiblePairs.insert(supplied.get(),pattern.get())) return true;
            for(std::size_t i=0;i<supplied->arguments.size();++i) if(!compatible(supplied->arguments[i],pattern->arguments[i])) return false;
            return true;
        };
        for(const auto& clause:relation.clauses) {
            if(!tick()) return {};
            bool matches=true;compatiblePairs.clear();
            for(std::size_t i=0;i<clause.head.size();++i) if(!compatible(goal.arguments[i],clause.head[i])) {matches=false;break;}
            if(!matches) continue;
            auto& bindings=context.bindings;Bindings::Transaction transaction(bindings);std::map<Id,Ref> variables;std::vector<Goal> added;
            for(std::size_t i=0;i<clause.head.size();++i) if(!bindings.unify(goal.arguments[i],fresh(clause.head[i],variables))) {matches=false;break;}
            if(!matches) continue;
            for(auto body:clause.body) {
                for(auto& argument:body.arguments) argument=fresh(argument,variables);
                if(body.kind<=Goal::Disjoint) {
                    if(!tick()) return {};
                    auto progress=propagate(body,bindings);
                    if(progress==Progress::Conflict) {matches=false;++result.stats.contradictions;break;}
                    if(progress==Progress::Done) continue;
                }
                added.push_back(std::move(body));
            }
            if(matches) alternatives.push_back({transaction.changes(),std::move(added),goal.label+":"+clause.label});
        }
    }
    if(found==program.relations.end() || !found->second.closed)
        alternatives.push_back({{},{unknown("open relation: "+goal.label)},goal.label+":open"});
    return alternatives;
}
Machine::Progress Machine::propagate(const Goal& goal, Bindings& bindings) {
    if (goal.kind == Goal::Unknown) return Progress::Pending;
    if (goal.kind == Goal::AwaitInput) return ground(bindings.resolve(goal.arguments.at(0))) ? Progress::Done : Progress::Pending;
    if(goal.kind==Goal::Finite) {
        if(goal.arguments.empty()) throw std::invalid_argument("finite domain needs a value");
        std::set<Integer> domain;
        for(std::size_t i=1;i<goal.arguments.size();++i) {
            auto term=bindings.walk(goal.arguments[i]);
            if(term->kind==Term::Variable) return Progress::Pending;
            if(term->kind!=Term::Number) return Progress::Conflict;
            domain.insert(term->number);
        }
        return bindings.constrain(goal.arguments[0],domain) ? Progress::Done : Progress::Conflict;
    }
    const auto expected = goal.kind == Goal::Disjoint ? 4u :
        (goal.kind == Goal::Add || goal.kind == Goal::Multiply || goal.kind == Goal::Modulo || goal.kind == Goal::Compute || goal.kind == Goal::BooleanOr) ? 3u : 2u;
    if (goal.arguments.size() != expected) throw std::invalid_argument("primitive arity: " + goal.label);
    const auto& args = goal.arguments;
    if (goal.kind == Goal::Equal) return bindings.unify(args[0],args[1]) ? Progress::Done : Progress::Conflict;
    if (goal.kind == Goal::Different) {
        auto left = bindings.resolve(args[0]), right = bindings.resolve(args[1]);
        if (identical(left,right)) return Progress::Conflict;
        for(unsigned side=0;side<2;++side) {
            const auto& variable=side ? right : left;const auto& value=side ? left : right;
            if(variable->kind==Term::Variable && value->kind==Term::Number) if(auto found=bindings.domains.find(variable->variable);found!=bindings.domains.end()) {
                auto narrowed=found->second;narrowed.erase(value->number);
                return bindings.constrain(variable,narrowed) ? Progress::Done : Progress::Conflict;
            }
        }
        if(left->kind==Term::Variable && right->kind==Term::Variable && bindings.domains.contains(left->variable) && bindings.domains.contains(right->variable)) {
            const auto& a=bindings.domains.at(left->variable);const auto& b=bindings.domains.at(right->variable);
            if(std::none_of(a.begin(),a.end(),[&](const Integer& value){return b.contains(value);})) return Progress::Done;
        }
        if(left->kind==Term::Variable && right->kind==Term::Variable) return Progress::Pending;
        if((left->kind==Term::Variable && right->kind==Term::Number) || (right->kind==Term::Variable && left->kind==Term::Number)) return Progress::Pending;
        Bindings::Transaction trial(bindings);
        if (!bindings.unify(left,right)) return Progress::Done;
        return Progress::Pending;
    }
    std::vector<std::optional<Integer>> numbers;
    for (const auto& argument : args) {
        // Primitive simplification must retain domain promises in the context.
        // Otherwise 0*x=0 could disappear before x becomes a nonnumeric term.
        if (!bindings.integer(argument)) return Progress::Conflict;
        auto value = bindings.walk(argument);
        if (value->kind == Term::Constructor) return Progress::Conflict;
        numbers.push_back(value->kind == Term::Number ? std::optional<Integer>(value->number) : std::nullopt);
    }
    auto bind = [&](std::size_t at, Integer value) {
        return bindings.unify(args[at],program.terms.number(std::move(value))) ? Progress::Done : Progress::Conflict;
    };
    auto check = [](bool truth) { return truth ? Progress::Done : Progress::Conflict; };
    auto& a = numbers[0]; auto& b = numbers[1];
    if (goal.kind == Goal::LessEqual) {
        if(a && b) return check(*a<=*b);
        auto domain=[&](std::size_t at) -> std::set<Integer> {
            auto term=bindings.walk(args[at]);
            if(term->kind==Term::Number) return {term->number};
            auto found=bindings.domains.find(term->variable);return found==bindings.domains.end() ? std::set<Integer>{} : found->second;
        };
        auto first=domain(0),second=domain(1);
        if(!first.empty() && !second.empty()) {
            std::set<Integer> low,high;
            for(const auto& value:first) if(value<=*second.rbegin()) low.insert(value);
            for(const auto& value:second) if(value>=*first.begin()) high.insert(value);
            if(!bindings.constrain(args[0],low) || !bindings.constrain(args[1],high)) return Progress::Conflict;
            if(*low.rbegin()<=*high.begin()) return Progress::Done;
        }
        return Progress::Pending;
    }
    if (goal.kind == Goal::Disjoint) {
        if (std::any_of(numbers.begin(),numbers.end(),[](const auto& n) { return n && *n < 0; })) return Progress::Conflict;
        if (!std::all_of(numbers.begin(),numbers.end(),[](const auto& n) { return n.has_value(); })) return Progress::Pending;
        return check(*b == 0 || *numbers[3] == 0 || *a + *b <= *numbers[2] || *numbers[2] + *numbers[3] <= *a);
    }
    auto& c = numbers[2];
    if(goal.kind==Goal::BooleanOr) {
        for(const auto& n:numbers) if(n && (*n<0 || *n>1)) return Progress::Conflict;
        if(a && b) return bind(2,(*a!=0 || *b!=0) ? 1 : 0);
        if((a && *a==1) || (b && *b==1)) {
            if(bind(2,1)==Progress::Conflict) return Progress::Conflict;
            // Keep the Boolean obligation on the unknown operand alive.
            return Progress::Pending;
        }
        if(c && *c==0) {
            if(bind(0,0)==Progress::Conflict || bind(1,0)==Progress::Conflict) return Progress::Conflict;
            return Progress::Done;
        }
        if(a && *a==0) {
            if(!bindings.unify(args[1],args[2])) return Progress::Conflict;
        } else if(b && *b==0) {
            if(!bindings.unify(args[0],args[2])) return Progress::Conflict;
        }
        return Progress::Pending;
    }
    if (goal.kind == Goal::Compute) {
        const auto& op=goal.label;
        if((op=="/" || op=="%") && b && *b==0) return Progress::Conflict;
        if((op=="<<" || op==">>") && b && (*b<0 || *b>1000000)) return Progress::Conflict;
        if(!a || !b) return Progress::Pending;
        if(op=="/") return bind(2,*a / *b);
        if(op=="%") return bind(2,*a % *b);
        if(op=="&") return bind(2,*a & *b);
        if(op=="|") return bind(2,*a | *b);
        if(op=="^") return bind(2,*a ^ *b);
        if(op=="<<") return bind(2,*a << b->convert_to<unsigned>());
        if(op==">>") return bind(2,*a >> b->convert_to<unsigned>());
        throw std::logic_error("unknown integer operation: "+op);
    }
    if (goal.kind == Goal::Modulo) {
        if (b && *b <= 0) return Progress::Conflict;
        if (c && (*c < 0 || (b && *c >= *b))) return Progress::Conflict;
        if (a && b) { Integer remainder = *a % *b; if (remainder < 0) remainder += *b; return bind(2,remainder); }
        return Progress::Pending;
    }
    if (goal.kind == Goal::Add) {
        if (a && b) return bind(2,*a + *b);
        if (a && c) return bind(1,*c - *a);
        if (b && c) return bind(0,*c - *b);
        return Progress::Pending;
    }
    if (goal.kind == Goal::Multiply) {
        if (a && b) return bind(2,*a * *b);
        auto divide = [&](const Integer& divisor, std::size_t at) {
            if (divisor == 0) return check(*c == 0);
            if (*c % divisor != 0) return Progress::Conflict;
            return bind(at,*c / divisor);
        };
        if (a && c) return divide(*a,1);
        if (b && c) return divide(*b,0);
        return Progress::Pending;
    }
    throw std::logic_error("invalid primitive");
}
Machine::Progress Machine::linear(const std::vector<Goal>& goals, Bindings& bindings) {
    // Fraction-free elimination closes jointly determined integer properties.
    // This applies equally to values, array extents, sizes and offsets; there
    // is no type-specific seeding or widening rule in this propagation step.
    struct Row { std::map<Id,Integer> coefficients; Integer right = 0; };
    std::vector<Row> rows;
    std::map<Id,Ref> variables;
    auto gcd = [](Integer a, Integer b) {
        if (a < 0) a = -a;
        if (b < 0) b = -b;
        while (b != 0) { Integer remainder = a % b; a = b; b = remainder; }
        return a;
    };
    auto normalize = [&](Row& row) {
        Integer divisor = 0;
        for (auto it = row.coefficients.begin(); it != row.coefficients.end();) {
            if (it->second == 0) it = row.coefficients.erase(it);
            else { divisor = gcd(divisor,it->second); ++it; }
        }
        if (row.coefficients.empty()) return row.right == 0;
        if (row.right % divisor != 0) return false;
        if (row.coefficients.begin()->second < 0) divisor = -divisor;
        for (auto& [id,coefficient] : row.coefficients) { (void)id; coefficient /= divisor; }
        row.right /= divisor;
        return true;
    };
    for (const auto& goal : goals) {
        if (!tick()) return Progress::Pending;
        if (goal.kind != Goal::Add && goal.kind != Goal::Multiply) continue;
        Row row;
        auto term = [&](Ref input, const Integer& coefficient) {
            input = bindings.walk(input);
            if (input->kind == Term::Number) row.right -= coefficient * input->number;
            else if (input->kind == Term::Variable) {
                row.coefficients[input->variable] += coefficient;
                variables[input->variable] = input;
            }
        };
        if (goal.kind == Goal::Add) {
            term(goal.arguments[0],1); term(goal.arguments[1],1); term(goal.arguments[2],-1);
        } else {
            auto a = bindings.walk(goal.arguments[0]), b = bindings.walk(goal.arguments[1]);
            if (a->kind == Term::Number) term(b,a->number);
            else if (b->kind == Term::Number) term(a,b->number);
            else continue; // A genuinely nonlinear relation keeps its obligation.
            term(goal.arguments[2],-1);
        }
        if (!normalize(row)) return Progress::Conflict;
        if (!row.coefficients.empty()) rows.push_back(std::move(row));
    }
    std::size_t pivot = 0;
    for (const auto& [variable,reference] : variables) {
        (void)reference;
        auto found = std::find_if(rows.begin()+static_cast<std::ptrdiff_t>(pivot),rows.end(),
            [&](const Row& row) { return row.coefficients.contains(variable); });
        if (found == rows.end()) continue;
        std::iter_swap(rows.begin()+static_cast<std::ptrdiff_t>(pivot),found);
        for (std::size_t at = 0; at < rows.size(); ++at) {
            if (at == pivot || !rows[at].coefficients.contains(variable)) continue;
            if (!tick()) return Progress::Pending;
            Integer common = gcd(rows[pivot].coefficients.at(variable),rows[at].coefficients.at(variable));
            Integer scale = rows[pivot].coefficients.at(variable) / common;
            Integer subtract = rows[at].coefficients.at(variable) / common;
            for (auto& [id,coefficient] : rows[at].coefficients) { (void)id; coefficient *= scale; }
            for (const auto& [id,coefficient] : rows[pivot].coefficients) rows[at].coefficients[id] -= subtract * coefficient;
            rows[at].right = rows[at].right * scale - rows[pivot].right * subtract;
            if (!normalize(rows[at])) return Progress::Conflict;
        }
        ++pivot;
    }
    bool changed = false;
    for (const auto& row : rows) if (row.coefficients.size() == 1) {
        const auto& [variable,coefficient] = *row.coefficients.begin();
        if (row.right % coefficient != 0) return Progress::Conflict;
        if (!bindings.unify(variables.at(variable),program.terms.number(row.right/coefficient))) return Progress::Conflict;
        changed = true;
    }
    return changed ? Progress::Done : Progress::Pending;
}
namespace {
bool same(const std::vector<Ref>& first, const std::vector<Ref>& second) {
    if (first.size() != second.size()) return false;
    for (std::size_t i = 0; i < first.size(); ++i) if (!identical(first[i],second[i])) return false;
    return true;
}
// A cursor streams the historical display order. Shared ground subgraphs
// compare as one piece; names may themselves contain delimiters, so tokens
// cannot simply be compared as structural tuples.
struct TextCursor {
    struct Piece {Ref term;std::string text;std::size_t offset=0;};
    std::vector<Piece> pending;
    explicit TextCursor(const std::vector<Ref>& terms) {
        for(auto it=terms.rbegin();it!=terms.rend();++it) {
            text("\n");pending.push_back({*it,{}});
        }
    }
    TextCursor(Ref term,std::string_view prefix) {
        pending.push_back({std::move(term),{}});text(std::string(prefix));
    }
    void text(std::string value) {
        if(!value.empty()) pending.push_back({{},std::move(value)});
    }
    void expand() {
        auto term=pending.back().term;pending.pop_back();
        if(term->kind!=Term::Constructor) {text(show(term));return;}
        if(!term->arguments.empty()) {
            text(")");
            for(std::size_t i=term->arguments.size();i>0;--i) {
                pending.push_back({term->arguments[i-1],{}});
                if(i>1) text(",");
            }
            text("(");
        }
        text(term->name);
    }
    void consume(std::size_t length) {
        auto& piece=pending.back();piece.offset+=length;
        if(piece.offset==piece.text.size()) pending.pop_back();
    }
};
int compareText(TextCursor a,TextCursor b) {
    while(!a.pending.empty() && !b.pending.empty()) {
        const auto& left=a.pending.back();const auto& right=b.pending.back();
        if(left.term || right.term) {
            // Variable names occur in displays but not in structural identity.
            if(left.term && right.term && (left.term==right.term ||
               (ground(left.term) && ground(right.term) && identical(left.term,right.term)))) {
                a.pending.pop_back();b.pending.pop_back();
            } else {
                if(left.term) a.expand();
                if(right.term) b.expand();
            }
            continue;
        }
        std::string_view x(left.text.data()+left.offset,left.text.size()-left.offset);
        std::string_view y(right.text.data()+right.offset,right.text.size()-right.offset);
        auto length=std::min(x.size(),y.size());
        auto order=x.substr(0,length).compare(y.substr(0,length));
        if(order) return order;
        a.consume(length);b.consume(length);
    }
    return a.pending.empty() ? (b.pending.empty() ? 0 : -1) : 1;
}
bool realizationLess(const std::vector<Ref>& first,const std::vector<Ref>& second) {
    return compareText(TextCursor(first),TextCursor(second))<0;
}
}
int compareTermText(const Ref& first,std::string_view firstPrefix,
                    const Ref& second,std::string_view secondPrefix) {
    return compareText(TextCursor(first,firstPrefix),TextCursor(second,secondPrefix));
}
void Machine::finish(const Context& context, const Request& request) {
    Model model;
    for(const auto& [name,term]:context.witnesses) {
        auto artifact=context.bindings.resolve(term);
        if(!ground(artifact)) {
            ++result.stats.unfinished;
            if(result.pending.size()<32) result.pending.push_back("unbound artifact: "+name);
            return;
        }
        model.witnesses.emplace(name,std::move(artifact));
    }
    auto collect = [&](const std::vector<Ref>& terms, std::vector<Ref>& destination) {
        for (const auto& term : terms) {
            auto value = context.bindings.resolve(term);
            if (!ground(value)) return false;
            destination.push_back(std::move(value));
        }
        return true;
    };
    if (!collect(request.observation,model.observation) || !collect(request.realization,model.realization)) {
        ++result.stats.unfinished;
        if (result.pending.size() < 32) result.pending.push_back("unbound demanded projection");
        return;
    }
    for(const auto& name:request.observedWitnesses) {
        auto found=model.witnesses.find(name);
        model.observation.push_back(found==model.witnesses.end() ? program.terms.make("Absent") :
            program.terms.make("Present",{found->second}));
    }
    if(request.observationClosure) {
        auto closed=request.observationClosure(model.observation,model.witnesses);
        if(!closed || !std::all_of(closed->begin(),closed->end(),ground)) {
            ++result.stats.unfinished;
            if(result.pending.size()<32) result.pending.push_back("unclosed observable artifact graph");
            return;
        }
        model.observationKey=std::move(*closed);
    } else model.observationKey=model.observation;
    for (const auto& objective : request.minimize) {
        auto value = context.bindings.walk(objective);
        if (value->kind == Term::Variable) {
            ++result.stats.unfinished;
            if (result.pending.size() < 32) result.pending.push_back("unbound realization objective");
            return;
        }
        if (value->kind != Term::Number) throw std::invalid_argument("objective must be an integer");
        model.cost.push_back(value->number);
    }
    ++result.stats.models;
    model.assumptions = context.assumptions;
    if (result.selected && !same(result.selected->observationKey,model.observationKey)) {
        result.outcome = Outcome::Ambiguous; result.alternative = std::move(model); return;
    }
    if (!result.selected || model.cost < result.selected->cost ||
        (model.cost == result.selected->cost && realizationLess(model.realization,result.selected->realization)))
        result.selected = std::move(model);
}
void Machine::search(Context context, const Request& request, std::size_t depth) {
    if(exhausted || result.outcome==Outcome::Ambiguous) return;
    if(depth>options.maxDepth) {exhausted=true;return;}
    result.stats.maxDepth=std::max(result.stats.maxDepth,depth);
    auto append=[](Context& at,const std::vector<Goal>& goals) {
        for(const auto& goal:goals) {Id id=at.nextGoal++;at.goals.emplace(id,goal);at.ready.insert(id);}
    };
    auto unsubscribe=[](Context& at,Id id) {
        auto found=at.waiting.find(id);if(found==at.waiting.end()) return;
        for(Id variable:found->second) {auto watcher=at.watchers.find(variable);if(watcher!=at.watchers.end()) {watcher->second.erase(id);if(watcher->second.empty()) at.watchers.erase(watcher);}}
        at.waiting.erase(found);
    };
    auto subscribe=[&](Id id,const Goal& goal) {
        std::set<Id> variables;
        std::unordered_set<const Term*> visited;
        std::function<void(const Ref&)> visit=[&](const Ref& term) {
            if(term->kind==Term::Variable) {
                if(!variables.insert(term->variable).second) return;
                auto found=context.bindings.values.find(term->variable);if(found!=context.bindings.values.end()) visit(found->second);
            } else if(!term->closed && visited.insert(term.get()).second)
                for(const auto& argument:term->arguments) visit(argument);
        };
        for(const auto& argument:goal.arguments) visit(argument);
        for(Id variable:variables) context.watchers[variable].insert(id);
        context.waiting[id]=std::move(variables);
    };
    auto wake=[&] {
        auto touched=std::move(context.bindings.touched);context.bindings.touched.clear();
        for(Id variable:touched) {
            auto found=context.watchers.find(variable);if(found==context.watchers.end()) continue;
            for(Id id:found->second) {context.ready.insert(id);context.choices.erase(id);}
        }
    };
    wake();
    for(;;) {
        while(!context.ready.empty()) {
            if(!tick()) return;
            auto selected=context.ready.begin();
            if(options.schedule=="lifo") selected=std::prev(context.ready.end());
            else if(options.schedule=="random") std::advance(selected,random()%context.ready.size());
            Id id=*selected;context.ready.erase(selected);unsubscribe(context,id);context.choices.erase(id);
            auto previousRevision=context.bindings.revision;
            auto foundGoal=context.goals.find(id);if(foundGoal==context.goals.end()) continue;
            const auto goal=foundGoal->second;bool done=false;
            if(goal.kind==Goal::Witness) {
                auto [found,inserted]=context.witnesses.emplace(goal.label,goal.arguments.at(0));
                if(!inserted && !context.bindings.unify(found->second,goal.arguments[0])) {++result.stats.contradictions;return;}
                done=true;
            } else if(goal.kind==Goal::Require) {
                auto replacement=request.overrides.find(goal.label);
                auto found=program.bundles.find(goal.label);
                auto supplied=replacement==request.overrides.end() && request.overrideBundle ? request.overrideBundle(goal.label) : std::optional<std::vector<Goal>>{};
                if(replacement==request.overrides.end() && !supplied && found==program.bundles.end()) throw std::logic_error("undefined obligation bundle: "+goal.label);
                if(context.activated.insert(goal.label).second) {
                    if(replacement!=request.overrides.end()) append(context,replacement->second);
                    else if(supplied) append(context,*supplied);
                    else append(context,found->second);
                    if(replacement!=request.overrides.end() || supplied) context.assumptions.push_back("assume:"+goal.label);
                }
                done=true;
            } else if(goal.kind==Goal::Build) {
                auto found=program.builders.find(goal.label);
                if(found==program.builders.end()) throw std::logic_error("undefined obligation builder: "+goal.label);
                const auto& builder=found->second;
                if(goal.arguments.size()!=builder.arity || builder.inputs>builder.arity) throw std::logic_error("builder arity: "+goal.label);
                std::vector<Ref> args;bool ready=true;
                for(std::size_t i=0;i<goal.arguments.size();++i) {
                    args.push_back(i<builder.inputs ? context.bindings.resolve(goal.arguments[i]) : goal.arguments[i]);
                    if(i<builder.inputs && !ground(args.back())) ready=false;
                }
                if(ready) {
                    try {append(context,builder.construct(args));}
                    catch(const ConstructionLimit& limit) {exhausted=true;if(result.pending.size()<32) result.pending.push_back(limit.what());return;}
                    done=true;
                }
            } else if(goal.kind==Goal::Call) {
                auto alternatives=expand(context,id);
                if(exhausted) return;
                if(alternatives.empty()) {++result.stats.contradictions;return;}
                if(alternatives.size()==1) {
                    context.bindings.apply(alternatives[0].bindings);append(context,alternatives[0].goals);done=true;
                } else context.choices[id]=alternatives.size();
            } else if(goal.kind!=Goal::Ambiguity) {
                auto progress=propagate(goal,context.bindings);
                if(progress==Progress::Conflict) {++result.stats.contradictions;return;}
                done=progress==Progress::Done;
            }
            wake();
            if(done) context.goals.erase(id);
            else {
                subscribe(id,goal);
                // A partially simplified constraint can ground its own last
                // dependency while it is unsubscribed. It must get a final
                // validation pass even when there is no variable left to watch.
                if(context.bindings.revision!=previousRevision) context.ready.insert(id);
            }
        }
        std::vector<Goal> equations;
        for(const auto& [id,goal]:context.goals) {(void)id;if(goal.kind==Goal::Add || goal.kind==Goal::Multiply) equations.push_back(goal);}
        auto progress=linear(equations,context.bindings);
        if(exhausted) return;
        if(progress==Progress::Conflict) {++result.stats.contradictions;return;}
        wake();if(!context.ready.empty()) continue;
        break;
    }
    std::optional<Id> choice;
    for(const auto& [id,count]:context.choices) if(!choice || count<context.choices.at(*choice)) choice=id;
    auto domain=context.bindings.domains.end();
    for(auto it=context.bindings.domains.begin();it!=context.bindings.domains.end();++it)
        if(domain==context.bindings.domains.end() || it->second.size()<domain->second.size()) domain=it;
    std::vector<Expansion> decision;
    if(domain!=context.bindings.domains.end() && (!choice || domain->second.size()<context.choices.at(*choice))) {
        choice.reset();auto variable=std::make_shared<Term>();variable->kind=Term::Variable;variable->variable=domain->first;auto candidates=domain->second;
        for(const auto& value:candidates) {
            Bindings::Transaction transaction(context.bindings);
            if(context.bindings.unify(variable,program.terms.number(value))) decision.push_back({transaction.changes(),{},"finite:"+value.str()});
        }
    } else if(choice) decision=expand(context,*choice);
    if(exhausted) return;
    if(context.goals.empty() && decision.empty()) {finish(context,request);return;}
    if(decision.empty()) {
        for(const auto& [id,goal]:context.goals) {
            (void)id;
            if(goal.kind==Goal::Ambiguity) {
                if(request.proposalsOnly) {++result.stats.contradictions;return;}
                result.outcome=Outcome::Ambiguous;if(result.pending.size()<32) result.pending.push_back(goal.label);return;
            }
        }
        ++result.stats.unfinished;
        // Explicit missing evidence is more useful than the builders waiting
        // on it. Preserve those reasons before filling the bounded summary.
        for(const auto& [id,goal]:context.goals) {
            (void)id;
            if(goal.kind==Goal::Unknown && std::find(result.pending.begin(),result.pending.end(),goal.label)==result.pending.end()) {
                if(result.pending.size()==32) result.pending.pop_back();
                result.pending.insert(result.pending.begin(),goal.label);
            }
        }
        for(const auto& [id,goal]:context.goals) {
            (void)id;if(result.pending.size()<32 && std::find(result.pending.begin(),result.pending.end(),goal.label)==result.pending.end()) result.pending.push_back(goal.label);
            if(goal.kind==Goal::AwaitInput) result.inputs.insert(goal.label);
        }
        return;
    }
    if(options.branches=="reverse") std::reverse(decision.begin(),decision.end());
    if(options.branches=="random") std::shuffle(decision.begin(),decision.end(),random);
    for(auto& alternative:decision) {
        if(exhausted || result.outcome==Outcome::Ambiguous) return;
        if(result.stats.decisions>=options.maxDecisions || (options.budget && options.budget->decisions>=options.budget->maxDecisions)) {exhausted=true;return;}
        ++result.stats.decisions;if(options.budget) ++options.budget->decisions;
        Context trial=context;trial.bindings.apply(alternative.bindings);
        if(choice) {unsubscribe(trial,*choice);trial.goals.erase(*choice);trial.choices.erase(*choice);trial.ready.erase(*choice);}
        append(trial,alternative.goals);trial.assumptions.push_back(std::move(alternative.label));
        search(std::move(trial),request,depth+1);
    }
}
Result Machine::solve(const Request& request) {
    result = {}; exhausted = false; random.seed(options.seed);
    Context initial;for(const auto& goal:request.goals) {Id id=initial.nextGoal++;initial.goals.emplace(id,goal);initial.ready.insert(id);}
    search(std::move(initial),request,0);
    if (result.outcome != Outcome::Ambiguous)
        result.outcome = exhausted ? Outcome::Exhausted : result.stats.unfinished ? Outcome::Unresolved :
            result.selected ? Outcome::Resolved : Outcome::Contradictory;
    return result;
}
} // namespace vx::relational
