#include "machine.hpp"
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace vx::relational;
namespace {
std::size_t cases = 0;
void check(bool truth, const std::string& message) {
    ++cases;
    if (!truth) throw std::runtime_error(message);
}
void status(const Result& result, Outcome expected) {
    check(result.outcome == expected,"expected " + outcomeName(expected) + ", got " + outcomeName(result.outcome));
}
void value(const Result& result, std::size_t index, const std::string& expected) {
    status(result,Outcome::Resolved);
    check(show(result.selected->observation.at(index)) == expected,"wrong observation: " + show(result.selected->observation.at(index)));
}
std::vector<Options> orders() {
    std::vector<Options> result;
    for (auto schedule : {"fifo","lifo","random"}) for (auto branches : {"forward","reverse"}) {
        Options o; o.schedule = schedule; o.branches = branches; result.push_back(o);
    }
    return result;
}
void structural_terms() {
    {
        Terms terms;auto x=terms.variable("x");
        check(show(terms.make("F",{terms.make(""),x,terms.number(-10),terms.make("A(1)")}))==
              "F(,?x:0,-10,A(1))","term text preserves empty names, punctuation, variables and numbers");
        auto chain=terms.number(7);std::string expected;
        for(unsigned i=0;i<2048;++i) {chain=terms.make("N",{chain});expected+="N(";}
        expected+="7";expected.append(2048,')');
        check(show(chain)==expected,"deep complete term text uses an explicit traversal");
    }
    for (auto options : orders()) {
        Program p; auto& t = p.terms;
        auto a = t.variable("element"), n = t.variable("length"), schema = t.variable("schema");
        Request r;
        r.goals = {equal(t.make("Record",{schema,t.make("Array",{a,n})}),
                         t.make("Record",{t.make("Packet"),t.make("Array",{t.make("UInt",{t.number(16)}),t.number(7)})}))};
        r.observation = {a,n,schema};
        auto result = Machine(p,options).solve(r);
        value(result,0,"UInt(16)"); value(result,1,"7"); value(result,2,"Packet");
        r.goals.push_back(equal(a,t.make("UInt",{t.number(8)})));
        status(Machine(p,options).solve(r),Outcome::Contradictory);
        r.goals = {equal(a,t.make("Array",{a,t.number(2)}))};
        status(Machine(p,options).solve(r),Outcome::Contradictory);
    }
}
void fresh_polymorphism() {
    Program p; auto& t = p.terms;
    auto element = t.variable("element");
    p.define("identity",1,{{{t.make("Function",{element,element})},{},"generic identity"}});
    auto resultA = t.variable("resultA"), resultB = t.variable("resultB");
    Request r;
    r.goals = {call("identity",{t.make("Function",{t.make("UInt",{t.number(8)}),resultA})}),
               call("identity",{t.make("Function",{t.make("Array",{t.make("UInt",{t.number(16)}),t.number(4)}),resultB})})};
    r.observation = {resultA,resultB};
    for (auto options : orders()) {
        auto result = Machine(p,options).solve(r);
        value(result,0,"UInt(8)"); value(result,1,"Array(UInt(16),4)");
    }
}
void shared_closed_terms() {
    Program p; auto& t=p.terms;
    auto tree=t.number(7);
    for(unsigned depth=0;depth<60;++depth) tree=t.make("Fork",{tree,tree});
    auto x=t.variable("x"),y=t.variable("y");Bindings bindings;
    check(!bindings.occurs(x->variable,tree),"closed DAG has no variable occurrences");
    {
        Bindings::Transaction transaction(bindings);
        check(bindings.unify(x,tree),"bind a shared closed DAG without expanding its paths");
        check(bindings.resolve(x)==tree,"resolution preserves closed graph sharing");
    }
    check(bindings.walk(x)==x,"closed graph binding rolls back");
    check(bindings.unify(x,t.make("Open",{y,tree})),"bind an open graph with a closed subtree");
    check(bindings.occurs(y->variable,x),"open variable remains visible through bindings");
    check(!bindings.unify(y,x),"closed subtree shortcut does not admit a cycle");
    p.define("shared",1,{{{tree},{},"closed graph"}});
    Request request;request.goals={call("shared",{x})};request.observation={x};
    auto result=Machine(p).solve(request);status(result,Outcome::Resolved);
    check(result.selected->observation[0]==tree,"clause freshening preserves immutable ground structure");
}
void shared_open_terms() {
    Program p;auto& t=p.terms;
    auto fork=[&](Ref leaf) {
        for(unsigned depth=0;depth<60;++depth) leaf=t.make("Fork",{leaf,leaf});
        return leaf;
    };
    auto x=t.variable("leaf"),open=fork(x),seven=fork(t.number(7)),sevenCopy=fork(t.number(7));
    check(identical(seven,sevenCopy),"independent equal DAGs compare by their nodes, not paths");
    check(!identical(seven,fork(t.number(8))),"independent DAG comparison sees a different leaf");
    Bindings bindings;
    check(bindings.resolve(open)==open,"an unresolved rewrite retains untouched shared structure");
    check(!bindings.occurs(t.variable("absent")->variable,open),"an absent variable does not expand DAG paths");
    {
        Bindings::Transaction transaction(bindings);
        check(bindings.unify(open,sevenCopy),"unify repeated open subgraphs once");
        auto resolved=bindings.resolve(open);
        check(ground(resolved) && identical(resolved,seven),"substitution closes every shared occurrence");
        for(unsigned depth=0;depth<60;++depth) {
            if(resolved->arguments[0]!=resolved->arguments[1]) throw std::runtime_error("substitution lost graph sharing");
            resolved=resolved->arguments[0];
        }
        check(resolved->number==7,"shared rewrite reaches the expected leaf");
    }
    check(bindings.resolve(open)==open,"resolved DAG memo cannot survive rollback");
    auto y=t.variable("second leaf");
    {
        Bindings::Transaction transaction(bindings);
        check(!bindings.unify(t.make("Pair",{x,x}),t.make("Pair",{t.number(1),t.number(2)})),
              "later narrowing still rejects conflicting repeated variables");
    }
    p.define("replicate",2,{{{open,x},{},"shared generic graph"}});
    auto a=t.variable("first graph"),b=t.variable("second graph");
    p.builders.emplace("leaf",Program::Builder{1,2,[&](const std::vector<Ref>& args) {
        auto leaf=args[0];for(unsigned depth=0;depth<60;++depth) leaf=leaf->arguments[0];
        return std::vector<Goal>{equal(leaf,args[1])};
    }});
    for(auto options:orders()) {
        Request r;r.goals={call("replicate",{a,t.number(7)}),call("replicate",{b,t.number(9)})};
        r.observation={a,b};auto result=Machine(p,options).solve(r);status(result,Outcome::Resolved);
        check(identical(result.selected->observation[0],seven) && identical(result.selected->observation[1],fork(t.number(9))),
              "fresh clauses preserve sharing and separate their variable environments");
        r.goals={build("leaf",{open,y}),equal(x,t.number(7))};r.observation={y};
        value(Machine(p,options).solve(r),0,"7");
        r.goals={call("replicate",{sevenCopy,t.number(7)})};r.observation={t.number(1)};
        value(Machine(p,options).solve(r),0,"1");
    }
}
void shared_realization_order() {
    Program p;auto& t=p.terms;
    auto x=t.variable("realization"),y=t.variable("tail");
    // Small independent text oracle: names can contain the same delimiters as
    // show(), so structural tuple ordering would change historical choices.
    std::vector<Ref> samples={t.number(-2),t.number(-10),t.number(2),t.number(10),
        t.make(""),t.make("A"),t.make("A",{t.number(1)}),t.make("A(1)"),
        t.make("A,",{t.make("")}),t.make("A\nB"),t.make("A",{t.number(1),t.number(2)})};
    unsigned pair=0;
    for(const auto& a:samples) for(const auto& b:samples) {
        auto name="realizations_"+std::to_string(pair++);
        p.define(name,2,{{{a,b},{},"first"},{{b,a},{},"second"}});
        Request request;request.goals={call(name,{x,y})};
        request.observation={t.number(42)};request.realization={x,y};
        auto expected=std::min(show(a)+'\n'+show(b)+'\n',show(b)+'\n'+show(a)+'\n');
        for(bool reverse:{false,true}) {
            Options options;options.branches=reverse ? "reverse" : "forward";
            auto result=Machine(p,options).solve(request);status(result,Outcome::Resolved);
            const auto& chosen=result.selected->realization;
            check(show(chosen[0])+'\n'+show(chosen[1])+'\n'==expected,"lazy tie order matches exact text ordering");
        }
    }
    auto fork=[&] {
        auto result=t.number(7);
        for(unsigned depth=0;depth<60;++depth) result=t.make("Fork",{result,result});
        return result;
    };
    auto a=t.make("Plan",{fork(),t.number(10)}),b=t.make("Plan",{fork(),t.number(2)});
    p.domain("large_realizations",{a,b});
    const auto& labels=p.relations.at("large_realizations").clauses;
    check(labels[0].label.size()==259 && labels[0].label.ends_with("..."),"domain diagnostics stop before expanding a shared graph");
    check(labels[0].label==labels[1].label,"truncated equal labels do not replace exact alternative identity");
    Request request;request.goals={call("large_realizations",{x})};
    request.observation={t.number(42)};request.realization={x};
    for(auto options:orders()) {
        auto result=Machine(p,options).solve(request);status(result,Outcome::Resolved);
        check(identical(result.selected->realization[0],a),"equal DAG prefix is skipped and lexical 10 precedes 2");
    }
}
void partial_schema() {
    for (auto options : orders()) {
        Program p; auto& t = p.terms;
        auto x = t.variable("x"), y = t.variable("y"), capture = t.variable("capture");
        p.define("x_property",2,{{{t.make("Record",{t.make("Packet"),x,y,capture}),x},{},"project x"}});
        auto missing = t.variable("unused field"), environment = t.variable("pending schema"), selected = t.variable("selected");
        auto record = t.make("Record",{t.make("Packet"),t.make("UInt",{t.number(16)}),missing,environment});
        Request r; r.goals = {call("x_property",{record,selected})}; r.observation = {selected};
        value(Machine(p,options).solve(r),0,"UInt(16)");
        r.observation = {record}; status(Machine(p,options).solve(r),Outcome::Unresolved);
        r.goals.push_back(equal(missing,t.make("UInt",{t.number(8)})));
        r.goals.push_back(equal(environment,t.make("Capture",{t.number(3)})));
        value(Machine(p,options).solve(r),0,"Record(Packet,UInt(16),UInt(8),Capture(3))");
        r.goals.push_back(equal(record,t.make("Record",{t.make("Other"),x,y,capture})));
        status(Machine(p,options).solve(r),Outcome::Contradictory);
    }
}
void relational_types() {
    for (auto options : orders()) {
        Program p; compactTypes(p); auto& t = p.terms;
        auto u8 = t.make("UInt",{t.number(8)}), u16 = t.make("UInt",{t.number(16)});
        auto a = t.variable("a"), b = t.variable("b"), n = t.variable("n");
        p.define("flip",2,{
            {{u8,u16},{},"8 to 16"}, {{u16,u8},{},"16 to 8"},
            {{t.make("Array",{a,n}),t.make("Array",{b,n})},{call("flip",{a,b})},"array"}
        });
        auto x = t.variable("x"), y = t.variable("y");
        Request r;
        // These equations reverse widths. A widening-only iteration cannot
        // express their alternatives; neither owner has an initializer seed.
        p.domain("scalar",{u8,u16});
        r.goals = {call("scalar",{x}),call("flip",{x,y}),call("flip",{y,x}),call("compact_size",{x,t.number(1)})};
        r.observation = {x,y};
        auto result = Machine(p,options).solve(r);
        value(result,0,"UInt(8)"); value(result,1,"UInt(16)");
        r.goals.pop_back(); status(Machine(p,options).solve(r),Outcome::Ambiguous);
        auto input = t.variable("input");
        r.goals = {call("flip",{input,t.make("Array",{t.make("Array",{u16,t.number(3)}),t.number(2)})})};
        r.observation = {input};
        value(Machine(p,options).solve(r),0,"Array(Array(UInt(8),3),2)");
    }
}
void dependent_shapes() {
    for (auto options : orders()) {
        Program p; compactTypes(p); auto& t = p.terms;
        auto cols = t.variable("columns"), fields = t.variable("fields"), header = t.variable("header");
        auto matrix = t.make("Array",{t.make("Array",{t.make("UInt",{t.number(16)}),cols}),t.number(2)});
        Request r;
        r.goals = {call("compact_size",{matrix,t.number(12)}),add(header,t.number(1),cols),
                   equal(fields,t.make("Packet",{header,matrix}))};
        r.observation = {cols,header,fields};
        auto result = Machine(p,options).solve(r);
        value(result,0,"3"); value(result,1,"2"); value(result,2,"Packet(2,Array(Array(UInt(16),3),2))");
        // New constraints start a fresh context; no previous candidate leaks.
        r.goals.push_back(equal(header,t.number(3)));
        status(Machine(p,options).solve(r),Outcome::Contradictory);
    }
}
void arithmetic_relations() {
    Program p; auto& t = p.terms; auto x = t.variable("x");
    Integer big = Integer(1) << 200;
    Request r; r.goals = {add(x,t.number(3),t.number(big))}; r.observation = {x};
    value(Machine(p).solve(r),0,Integer(big-3).str());
    r.goals = {multiply(x,t.number(-3),t.number(21))}; value(Machine(p).solve(r),0,"-7");
    r.goals = {multiply(x,t.number(3),t.number(5))}; status(Machine(p).solve(r),Outcome::Contradictory);
    r.goals = {multiply(x,t.number(0),t.number(0))}; status(Machine(p).solve(r),Outcome::Unresolved);
    r.observation = {t.number(7)}; value(Machine(p).solve(r),0,"7");
    r.goals = {different(t.make("Array",{x}),t.make("Record",{x}))}; value(Machine(p).solve(r),0,"7");
}
void joint_linear_dimensions() {
    for (auto options : orders()) {
        Program p; compactTypes(p); auto& t = p.terms;
        auto a = t.variable("first length"), b = t.variable("second length"), bytes = t.variable("bytes");
        auto first = t.make("Array",{t.make("UInt",{t.number(8)}),a});
        auto second = t.make("Array",{t.make("UInt",{t.number(16)}),b});
        Request r; r.goals = {add(b,t.number(1),a),add(a,b,t.number(7)),
            call("compact_size",{t.make("Pair",{first,second}),bytes})};
        r.observation = {first,second,bytes};
        auto result = Machine(p,options).solve(r);
        value(result,0,"Array(UInt(8),4)"); value(result,1,"Array(UInt(16),3)"); value(result,2,"10");
        r.goals = {add(a,t.number(1),a)}; status(Machine(p,options).solve(r),Outcome::Contradictory);
        r.goals = {add(a,b,t.number(1)),add(b,t.number(0),a)};
        status(Machine(p,options).solve(r),Outcome::Contradictory);
        // Two coupled equations, with no initializer, seed or finite domain.
        auto twice = t.variable("twice");
        r.goals = {multiply(a,t.number(2),twice),add(twice,b,t.number(13)),add(a,b,t.number(8))};
        r.observation = {a,b}; result = Machine(p,options).solve(r);
        value(result,0,"5"); value(result,1,"3");
    }
}
void numeric_obligations_survive_simplification() {
    for (auto options : orders()) {
        Program p; auto& t = p.terms; auto x = t.variable("x"), y = t.variable("y");
        Request r; r.observation = {t.number(7)};
        // 0*x=0 can discharge arithmetic, but cannot erase x's integer domain.
        r.goals = {multiply(x,t.number(0),t.number(0)),equal(x,t.make("Record"))};
        status(Machine(p,options).solve(r),Outcome::Contradictory);
        r.goals = {multiply(x,t.number(0),t.number(0)),equal(x,y),equal(y,t.make("Record"))};
        status(Machine(p,options).solve(r),Outcome::Contradictory);
        p.domain("mixed",{t.make("Record"),t.number(5)});
        r.goals = {multiply(x,t.number(0),t.number(0)),call("mixed",{x})}; r.observation = {x};
        value(Machine(p,options).solve(r),0,"5");
    }
}
void linear_oracle() {
    std::size_t verified = 0;
    for (int a = -2; a <= 2; ++a) for (int b = -2; b <= 2; ++b)
        for (int c = -2; c <= 2; ++c) for (int d = -2; d <= 2; ++d) {
            int determinant = a*d-b*c;
            if (!determinant) continue;
            for (bool integral : {false,true}) {
                int e = integral ? a*(-2)+b*3 : 1;
                int f = integral ? c*(-2)+d*3 : 2;
                int numeratorX = e*d-b*f, numeratorY = a*f-e*c;
                bool admissible = numeratorX % determinant == 0 && numeratorY % determinant == 0;
                for (auto options : orders()) {
                    Program p; auto& t = p.terms;
                    auto x = t.variable("x"), y = t.variable("y");
                    auto ax = t.variable("ax"), by = t.variable("by"), cx = t.variable("cx"), dy = t.variable("dy");
                    Request r; r.goals = {
                        multiply(t.number(a),x,ax),multiply(t.number(b),y,by),add(ax,by,t.number(e)),
                        multiply(t.number(c),x,cx),multiply(t.number(d),y,dy),add(cx,dy,t.number(f))};
                    r.observation = {x,y};
                    auto result = Machine(p,options).solve(r);
                    if (admissible) {
                        value(result,0,std::to_string(numeratorX/determinant));
                        value(result,1,std::to_string(numeratorY/determinant));
                    } else status(result,Outcome::Contradictory);
                    ++verified;
                }
            }
        }
    std::cout << verified << " independent linear-system oracle cases passed\n";
}
void closure_and_policy() {
    for (auto options : orders()) {
        Program p; auto& t = p.terms; auto x = t.variable("x");
        p.domain("choice",{t.number(1),t.number(2)});
        Request r; r.goals = {call("choice",{x})}; r.observation = {x}; r.minimize = {x};
        status(Machine(p,options).solve(r),Outcome::Ambiguous);
        r.observation = {t.number(7)}; r.realization = {x};
        auto result = Machine(p,options).solve(r);
        value(result,0,"7"); check(show(result.selected->realization[0]) == "1","wrong physical preference");
        p.define("open",1,{{{t.number(1)},{},"known"}},false);
        r.goals = {call("open",{x})}; status(Machine(p,options).solve(r),Outcome::Unresolved);
        r.goals.push_back(lessEqual(t.number(2),t.number(1)));
        status(Machine(p,options).solve(r),Outcome::Contradictory);
        p.define("empty",1,{}); r.goals = {call("empty",{x})};
        status(Machine(p,options).solve(r),Outcome::Contradictory);
        r.goals = {call("missing",{x})}; status(Machine(p,options).solve(r),Outcome::Unresolved);
        auto v = t.variable("v"); p.define("spin",1,{{{v},{call("spin",{v})},"recur"}});
        r.goals = {call("spin",{x})}; options.maxWork = 100;
        status(Machine(p,options).solve(r),Outcome::Exhausted);
        r.goals = {call("choice",{x})}; options.maxWork = 10000; options.maxDecisions = 0;
        status(Machine(p,options).solve(r),Outcome::Exhausted);
    }
}
void outcome_oracle() {
    std::size_t verified = 0;
    for (int first = -1; first <= 1; ++first) for (int second = -1; second <= 1; ++second)
        for (int third = -1; third <= 1; ++third) for (unsigned mask = 0; mask < 8; ++mask) {
            const int mapping[] = {first,second,third};
            std::set<int> known; bool unfinished = false;
            for (unsigned i = 0; i < 3; ++i) if (mask & (1u << i)) {
                if (mapping[i] == -1) unfinished = true; else known.insert(mapping[i]);
            }
            auto expected = known.size() > 1 ? Outcome::Ambiguous : unfinished ? Outcome::Unresolved :
                !known.empty() ? Outcome::Resolved : Outcome::Contradictory;
            for (auto options : orders()) {
                Program p; auto& t = p.terms; auto x = t.variable("x");
                std::vector<Clause> alternatives;
                for (unsigned i = 0; i < 3; ++i) {
                    std::vector<Goal> body{lessEqual(t.number(1),t.number((mask & (1u << i)) ? 1 : 0))};
                    if (mapping[i] == -1) body.push_back(unknown("unfinished proof"));
                    alternatives.push_back({{mapping[i] == -1 ? x : t.number(mapping[i])},body,std::to_string(i)});
                }
                p.define("branch",1,std::move(alternatives));
                Request r; r.goals = {call("branch",{x})}; r.observation = {x};
                auto result = Machine(p,options).solve(r); status(result,expected);
                if (expected == Outcome::Resolved) value(result,0,std::to_string(*known.begin()));
                ++verified;
            }
        }
    check(verified == 1296,"oracle coverage");
    std::cout << verified << " independent outcome oracle cases passed\n";
}
void activation_contracts() {
    Program p; auto& t = p.terms;
    ActivationGraph graph;
    auto a = graph.event("begin A"), b = graph.event("end A");
    auto c = graph.event("begin B"), d = graph.event("end B");
    graph.live("A",a,b,t.number(3),t.number(0)); graph.live("B",c,d,t.number(3),t.number(0));
    check(graph.overlap(graph.storage[0],graph.storage[1]) == ActivationGraph::Overlap::Admitted,"independent sources can overlap");
    Request r; r.observation = {t.number(7)}; graph.constrain(r);
    status(Machine(p).solve(r),Outcome::Contradictory);
    graph.before(b,c);
    check(graph.overlap(graph.storage[0],graph.storage[1]) == ActivationGraph::Overlap::Excluded,"serial activations can reuse storage");
    r.goals.clear(); graph.constrain(r); value(Machine(p).solve(r),0,"7");
    graph.before(d,a); r.goals.clear(); graph.constrain(r);
    status(Machine(p).solve(r),Outcome::Contradictory);
    ActivationGraph handoff;
    a = handoff.event("begin A"); b = handoff.event("A releases; B begins"); d = handoff.event("end B");
    handoff.live("A",a,b,t.number(3),t.number(0)); handoff.live("B",b,d,t.number(3),t.number(0));
    check(handoff.overlap(handoff.storage[0],handoff.storage[1]) == ActivationGraph::Overlap::Excluded,
          "a shared completion/start event permits storage reuse");
    check(handoff.overlap(handoff.storage[1],handoff.storage[0]) == ActivationGraph::Overlap::Excluded,
          "handoff exclusion is symmetric");
    r.goals.clear(); handoff.constrain(r); value(Machine(p).solve(r),0,"7");
    ActivationGraph open;
    a = open.event("start A"); b = open.event("end A"); c = open.event("start B"); d = open.event("end B");
    open.live("A",a,b,t.number(3),t.number(0)); open.live("B",c,d,t.number(3),t.number(0));
    open.closed = false; r.goals.clear(); open.constrain(r);
    check(open.overlap(open.storage[0],open.storage[1]) == ActivationGraph::Overlap::Unknown,"open overlap must stay unknown");
    status(Machine(p).solve(r),Outcome::Unresolved);
    // Established nesting can refute sharing even while other contract facts
    // remain open. A conservative guess of overlap cannot do that.
    open.before(a,c); open.before(d,b); r.goals.clear(); open.constrain(r);
    check(open.overlap(open.storage[0],open.storage[1]) == ActivationGraph::Overlap::Admitted,"nesting proves overlap");
    status(Machine(p).solve(r),Outcome::Contradictory);
    for (const auto& mode : {"overlap","retained","recursive"}) {
        Program model; auto demo = demonstration(model,mode,6);
        auto result = Machine(model).solve(demo.request); status(result,Outcome::Resolved);
        check(show(result.selected->realization[1]) == "8","overlap must revise element representation");
        check(show(result.selected->realization[3]) != show(result.selected->realization[4]),"overlap must prevent sharing");
    }
    Program model; auto demo = demonstration(model,"open",6);
    status(Machine(model).solve(demo.request),Outcome::Unresolved);
}
void modular_arithmetic() {
    for(auto options:orders()) {
        for(int input:{-257,-256,-1,0,1,255,256,257}) {
            Program p;auto& t=p.terms;auto result=t.variable("remainder");Request r;
            r.goals={modulo(t.number(input),t.number(256),result)};r.observation={result};
            value(Machine(p,options).solve(r),0,std::to_string((input%256+256)%256));
        }
        Program p;auto& t=p.terms;auto input=t.variable("input");Request r;
        r.goals={modulo(input,t.number(256),t.number(0))};r.observation={input};
        status(Machine(p,options).solve(r),Outcome::Unresolved);
        r.goals.push_back(equal(input,t.make("Record")));
        status(Machine(p,options).solve(r),Outcome::Contradictory);
        r.goals={modulo(t.number(1),t.number(0),input)};
        status(Machine(p,options).solve(r),Outcome::Contradictory);
    }
}
void delayed_obligations() {
    for(auto options:orders()) {
        Program p;auto& t=p.terms;
        auto input=t.variable("input"),output=t.variable("output"),shared=t.variable("shared");
        p.bundles["shared"]={equal(shared,t.number(7)),require("cycle")};
        p.bundles["cycle"]={require("shared")};
        unsigned builds=0;
        p.builders.emplace("delayed",Program::Builder{1,2,[&](const std::vector<Ref>& args) {
            ++builds;return std::vector<Goal>{require("shared"),add(args[0],shared,args[1])};
        }});
        Request r;r.goals={build("delayed",{input,output}),equal(output,t.number(8))};r.observation={input,output};
        status(Machine(p,options).solve(r),Outcome::Unresolved);
        check(builds==0,"a consumer must not run a builder with missing inputs");
        p.domain("inputs",{t.number(1),t.number(2)});r.goals.push_back(call("inputs",{input}));
        auto result=Machine(p,options).solve(r);value(result,0,"1");value(result,1,"8");
        r.goals={call("inputs",{input}),build("delayed",{input,output})};
        status(Machine(p,options).solve(r),Outcome::Ambiguous);
        // A once-built shared bundle still has to be required independently
        // in every alternative and every new solve.
        r.goals.push_back(equal(shared,t.number(9)));
        status(Machine(p,options).solve(r),Outcome::Contradictory);
        r.goals={ambiguity("equal semantic candidates"),equal(t.number(0),t.number(1))};
        status(Machine(p,options).solve(r),Outcome::Contradictory);
        r.goals={ambiguity("equal semantic candidates")};
        status(Machine(p,options).solve(r),Outcome::Ambiguous);
    }
}
void candidate_artifacts() {
    for(auto options:orders()) {
        Program p;auto& t=p.terms;auto input=t.variable("input"),open=t.variable("open artifact");
        p.define("candidate",1,{
            {{t.number(1)},{witness("implementation",t.make("First"))},"first"},
            {{t.number(2)},{witness("implementation",t.make("Second"))},"second"}
        });
        Request r;r.goals={call("candidate",{input}),equal(input,t.number(2))};r.observation={t.number(7)};
        auto result=Machine(p,options).solve(r);status(result,Outcome::Resolved);
        check(result.selected->witnesses.size()==1 && show(result.selected->witnesses.at("implementation"))=="Second","rejected artifact leaked");
        r.goals.push_back(witness("incomplete",open));status(Machine(p,options).solve(r),Outcome::Unresolved);
        r.goals.back()=witness("implementation",t.make("First"));status(Machine(p,options).solve(r),Outcome::Contradictory);
        r.goals={witness("implementation",t.make("First"))};
        result=Machine(p,options).solve(r);status(result,Outcome::Resolved);
        check(show(result.selected->witnesses.at("implementation"))=="First","artifact survived a revision");
    }
}
void proposal_discovery() {
    for(auto options:orders()) {
        Program p;auto& t=p.terms;auto x=t.variable("proposed result");
        p.define("proposals",1,{
            {{t.number(1)},{ambiguity("provisional overload tie")},"ambiguous"},
            {{t.number(2)},{},"complete"}
        });
        Request r;r.goals={call("proposals",{x})};r.observation={x};
        status(Machine(p,options).solve(r),Outcome::Ambiguous);
        r.proposalsOnly=true;value(Machine(p,options).solve(r),0,"2");
        p.relations.at("proposals").clauses.push_back({{t.number(3)},{unknown("unfinished proposal")},"open"});
        auto result=Machine(p,options).solve(r);status(result,Outcome::Unresolved);
        check(result.selected && show(result.selected->observation[0])=="2","unfinished proposal lost a completed sibling");
        p.relations.at("proposals").clauses.back().body.clear();
        status(Machine(p,options).solve(r),Outcome::Ambiguous);
        // Proposal mode belongs to a request, never the program or machine.
        r.proposalsOnly=false;status(Machine(p,options).solve(r),Outcome::Ambiguous);
    }
}
void observed_artifacts() {
    for(auto options:orders()) {
        Program p;auto& t=p.terms;auto branch=t.variable("branch");
        p.define("alternatives",1,{
            {{t.number(0)},{},"absent"},
            {{t.number(1)},{witness("needed",t.number(7))},"present"}
        });
        Request r;r.goals={call("alternatives",{branch})};r.observation={t.number(42)};
        value(Machine(p,options).solve(r),0,"42");
        r.observedWitnesses={"needed"};status(Machine(p,options).solve(r),Outcome::Ambiguous);
        r.goals.push_back(equal(branch,t.number(0)));value(Machine(p,options).solve(r),1,"Absent");
        r.goals.back()=equal(branch,t.number(1));value(Machine(p,options).solve(r),1,"Present(7)");
        r.goals.pop_back();
        p.relations.at("alternatives").clauses[0].body={unknown("incomplete absence")};
        status(Machine(p,options).solve(r),Outcome::Unresolved);
        p.relations.at("alternatives").clauses[0].body={witness("needed",t.number(7)),witness("irrelevant",t.number(9))};
        value(Machine(p,options).solve(r),1,"Present(7)");
        p.relations.at("alternatives").clauses[0].body={witness("needed",t.number(8))};
        status(Machine(p,options).solve(r),Outcome::Ambiguous);
        r.observedWitnesses.clear();value(Machine(p,options).solve(r),0,"42");
        r.observedWitnesses={"never"};value(Machine(p,options).solve(r),1,"Absent");
    }
}
void closed_observations() {
    for(auto options:orders()) {
        Program p;auto& t=p.terms;auto branch=t.variable("branch");
        p.define("alternatives",1,{
            {{t.number(0)},{witness("left",t.number(7))},"left"},
            {{t.number(1)},{witness("right",t.number(7))},"right"}
        });
        Request r;r.goals={call("alternatives",{branch})};r.observation={branch};
        r.observationClosure=[](const auto& roots,const auto& artifacts)->std::optional<std::vector<Ref>> {
            auto name=roots[0]->number==0 ? "left" : "right";
            auto found=artifacts.find(name);if(found==artifacts.end()) return {};
            return std::vector<Ref>{found->second};
        };
        auto resolved=Machine(p,options).solve(r);status(resolved,Outcome::Resolved);
        check(resolved.selected->observation.size()==1,"closure changed displayed projections");
        check(show(resolved.selected->observationKey[0])=="7","closure did not dereference artifact");
        p.relations.at("alternatives").clauses[1].body={witness("right",t.number(8))};
        status(Machine(p,options).solve(r),Outcome::Ambiguous);
        p.relations.at("alternatives").clauses[1].body.clear();
        status(Machine(p,options).solve(r),Outcome::Unresolved);
        p.relations.at("alternatives").clauses[1].body={witness("right",t.number(7)),unknown("unfinished definition")};
        status(Machine(p,options).solve(r),Outcome::Unresolved);
        r.observationClosure={};status(Machine(p,options).solve(r),Outcome::Unresolved);
    }
}
void finite_domains() {
    for(auto options:orders()) {
        Program p;auto& t=p.terms;auto x=t.variable("x"),y=t.variable("y"),z=t.variable("z");
        auto one=t.number(1),two=t.number(2),three=t.number(3);
        Request r;r.observation={x,y};
        r.goals={finite(x,{one,two}),finite(y,{two,three}),equal(x,y)};
        auto result=Machine(p,options).solve(r);value(result,0,"2");value(result,1,"2");
        r.goals={finite(x,{one,two,three}),different(x,one),lessEqual(x,two)};
        r.observation={x};value(Machine(p,options).solve(r),0,"2");
        r.goals={finite(x,{one,two}),finite(y,{one,two}),different(x,y)};
        status(Machine(p,options).solve(r),Outcome::Ambiguous);
        r.goals.push_back(equal(x,y));status(Machine(p,options).solve(r),Outcome::Contradictory);
        r.goals={finite(x,{} )};status(Machine(p,options).solve(r),Outcome::Contradictory);
        r.goals={finite(x,{one,two}),equal(x,t.make("Record"))};status(Machine(p,options).solve(r),Outcome::Contradictory);
        // Cross-check bounds, aliasing and rollback against a tiny complete oracle.
        for(int total=0;total<=9;++total) for(int bound=0;bound<=3;++bound) {
            std::set<int> observed;
            for(int a=1;a<=3;++a) for(int b=1;b<=3;++b) if(a!=b && a+b==total && a<=bound) observed.insert(a);
            r.goals={finite(x,{one,two,three}),finite(y,{one,two,three}),different(x,y),add(x,y,t.number(total)),lessEqual(x,t.number(bound))};
            result=Machine(p,options).solve(r);
            status(result,observed.empty() ? Outcome::Contradictory : observed.size()==1 ? Outcome::Resolved : Outcome::Ambiguous);
            if(observed.size()==1) value(result,0,std::to_string(*observed.begin()));
        }
        for(int a=-3;a<=3;++a) for(int b=-3;b<=3;++b) for(std::string op:{"/","%"}) {
            r.goals={compute(op,t.number(a),t.number(b),z)};r.observation={z};result=Machine(p,options).solve(r);
            if(b==0) status(result,Outcome::Contradictory);else value(result,0,std::to_string(op=="/" ? a/b : a%b));
        }
    }
}
void partial_boolean_information() {
    for(auto options:orders()) {
        Program p;auto& t=p.terms;auto a=t.variable("a"),b=t.variable("b"),out=t.variable("out");
        for(int x=0;x<2;++x) for(int y=0;y<2;++y) {
            Request r;r.goals={booleanOr(t.number(x),t.number(y),out)};r.observation={out};
            value(Machine(p,options).solve(r),0,std::to_string(x||y));
        }
        // The result wakes the producer of the as-yet unknown operand.
        p.builders.emplace("complete",Program::Builder{1,2,[&](const auto& args){return std::vector<Goal>{equal(args[1],t.number(0))};}});
        Request r;r.goals={booleanOr(a,t.number(1),out),build("complete",{out,a})};r.observation={a,out};
        auto result=Machine(p,options).solve(r);value(result,0,"0");value(result,1,"1");
        r.goals={booleanOr(a,b,t.number(0))};r.observation={a,b};result=Machine(p,options).solve(r);value(result,0,"0");value(result,1,"0");
        // An unknown or invalid producer is never discarded by true OR x.
        r.goals={booleanOr(a,t.number(1),out)};r.observation={out};status(Machine(p,options).solve(r),Outcome::Unresolved);
        for(auto invalid:{t.number(2),t.make("NotBoolean")}) {r.goals.push_back(equal(a,invalid));status(Machine(p,options).solve(r),Outcome::Contradictory);r.goals.pop_back();}
        r.goals={booleanOr(a,b,out)};status(Machine(p,options).solve(r),Outcome::Unresolved);
    }
}
void isolated_bundle_assumptions() {
    for(auto options:orders()) {
        Program p;auto& t=p.terms;auto x=t.variable("interface");
        p.bundles["definition"]={equal(x,t.number(1))};
        Request original;original.goals={require("definition")};original.observation={x};
        Request proof=original;proof.overrides["definition"]={equal(x,t.make("Opaque"))};
        value(Machine(p,options).solve(proof),0,"Opaque");
        value(Machine(p,options).solve(original),0,"1");
        auto late=t.variable("late interface");
        p.builders.emplace("discover",Program::Builder{0,0,[&](const auto&){
            p.bundles["late definition"]={equal(late,t.number(9))};
            return std::vector<Goal>{require("late definition")};
        }});
        Request dynamic;dynamic.goals={build("discover",{})};dynamic.observation={late};
        dynamic.overrideBundle=[&](const std::string& name)->std::optional<std::vector<Goal>> {
            if(name=="late definition") return std::vector<Goal>{equal(late,t.make("UnknownProperty"))};
            return {};
        };
        auto hypothetical=Machine(p,options).solve(dynamic);value(hypothetical,0,"UnknownProperty");
        check(std::find(hypothetical.selected->assumptions.begin(),hypothetical.selected->assumptions.end(),"assume:late definition")!=hypothetical.selected->assumptions.end(),"dynamic assumption was not recorded");
        dynamic.overrideBundle={};value(Machine(p,options).solve(dynamic),0,"9");
        auto budget=std::make_shared<SearchBudget>(SearchBudget{4,100,0,0});options.budget=budget;
        Request small;small.goals={equal(x,t.number(7))};small.observation={x};
        while(budget->work<budget->maxWork) value(Machine(p,options).solve(small),0,"7");
        status(Machine(p,options).solve(small),Outcome::Exhausted);
        check(budget->work==4,"shared work fuel exceeded its bound");
        options.budget.reset();
        proof.goals.push_back(equal(x,t.number(2)));status(Machine(p,options).solve(proof),Outcome::Contradictory);
        value(Machine(p,options).solve(original),0,"1");
    }
}
void streamed_term_text() {
    Terms t;std::vector<Ref> terms{t.number(-12),t.number(2),t.number(10),t.make(""),t.make("("),
        t.make("A"),t.make("A(x)"),t.make("A",{t.make("x")}),t.make("A",{t.number(1),t.number(2)})};
    for(const auto& left:terms) for(const auto& right:terms) for(const auto& prefix:{"","bit:03:"}) {
        auto expected=(std::string(prefix)+show(left)).compare(show(right));
        auto actual=compareTermText(left,prefix,right,"");
        check((actual<0)==(expected<0) && (actual>0)==(expected>0),"streamed ordering differs from exact display");
    }
    auto shared=t.make("Leaf");
    for(unsigned i=0;i<60;++i) shared=t.make("Pair",{shared,shared});
    auto left=t.make("Edge",{shared,t.number(1)}),right=t.make("Edge",{shared,t.number(2)});
    check(compareTermText(left,"",right,"")<0,"shared display prefix did not compare compactly");
    check(show(shared,32).size()==35,"bounded display did not truncate shared structure");
    check(show(t.make("Leaf"),4)=="Leaf" && show(t.make("Leaf"),3)=="Lea...","display truncation boundary changed");
}
void bounded_retry_effort() {
    RetryEffort effort;std::size_t total=0;
    const std::vector<std::size_t> expected{10,20,10,40,10,10,10,80,10,10,10,10,10,10,10,80};
    for(std::size_t i=0;i<expected.size();++i) {
        auto work=effort.next(10,80);total+=work;
        check(work==expected[i],"retry fuel must stay cheap between geometric boosts");
        check(total*2<=(i+1)*10*5,"bounded retries spent more than 2.5 times ordinary fuel");
    }
    RetryEffort limited;
    check(limited.next(20000,7)==7 && limited.next(20000,0)==0,"retry fuel exceeded the caller's limit");
    RetryEffort huge;auto maximum=std::numeric_limits<std::size_t>::max();
    check(huge.next(maximum/2+1,maximum)==maximum/2+1 && huge.next(maximum/2+1,maximum)==maximum,
          "retry fuel multiplication overflowed");
    check(huge.next(0,maximum)==0,"zero ordinary fuel acquired work");
}
void named_proof_inputs() {
    for(auto options:orders()) {
        Program p;auto& t=p.terms;auto x=t.variable("enclosing evidence"),y=t.variable("local answer");
        Request proof;proof.goals={awaitInput(x,"parent/type"),equal(y,x)};proof.observation={y};
        auto blocked=Machine(p,options).solve(proof);status(blocked,Outcome::Unresolved);
        check(blocked.inputs==std::set<std::string>{"parent/type"},"missing proof input was not reported");
        proof.goals.push_back(equal(x,t.make("UInt",{t.number(16)})));
        auto closed=Machine(p,options).solve(proof);value(closed,0,"UInt(16)");check(closed.inputs.empty(),"closed evidence was reported as missing");
        proof.goals.push_back(equal(y,t.make("Other")));
        auto rejected=Machine(p,options).solve(proof);status(rejected,Outcome::Contradictory);check(rejected.inputs.empty(),"discarded proof requested evidence");
    }
}
void explanatory_pending_evidence() {
    for(auto options:orders()) {
        Program p;auto& t=p.terms;Request request;
        for(unsigned i=0;i<64;++i) request.goals.push_back(add(t.variable("left"),t.variable("right"),t.variable("sum")));
        request.goals.push_back(unknown("missing activation contract"));
        auto answer=Machine(p,options).solve(request);status(answer,Outcome::Unresolved);
        check(std::find(answer.pending.begin(),answer.pending.end(),"missing activation contract")!=answer.pending.end(),"explicit evidence was hidden behind mechanical dependencies");
        check(std::count(answer.pending.begin(),answer.pending.end(),"sum")<=1,"pending summary repeated equivalent dependencies");
    }
}

}
int main() {
    try {
        for (auto [name,test] : std::vector<std::pair<const char*,std::function<void()>>>{
            {"structural terms",structural_terms}, {"fresh polymorphism",fresh_polymorphism}, {"shared closed terms",shared_closed_terms}, {"shared open terms",shared_open_terms}, {"shared realization ordering",shared_realization_order}, {"partial schemas",partial_schema}, {"pending evidence",explanatory_pending_evidence},
            {"retry effort",bounded_retry_effort}, {"streamed term text",streamed_term_text}, {"relational types",relational_types}, {"dependent shapes",dependent_shapes}, {"observed artifacts",observed_artifacts}, {"closed observations",closed_observations},
            {"exact arithmetic",arithmetic_relations}, {"joint linear dimensions",joint_linear_dimensions},
            {"numeric obligations",numeric_obligations_survive_simplification},
            {"linear oracle",linear_oracle}, {"closure and policy",closure_and_policy},
            {"outcome oracle",outcome_oracle}, {"activation contracts",activation_contracts},
            {"modular arithmetic",modular_arithmetic}, {"delayed obligations",delayed_obligations}, {"candidate artifacts",candidate_artifacts}, {"proposal discovery",proposal_discovery}, {"finite domains",finite_domains}, {"partial Boolean information",partial_boolean_information}, {"isolated bundle assumptions",isolated_bundle_assumptions}, {"named proof inputs",named_proof_inputs}}) {
            test(); std::cout << name << ": passed\n";
        }
        std::cout << cases << " assertions passed\n";
    } catch (const std::exception& error) { std::cerr << "FAILED: " << error.what() << '\n'; return 1; }
}
