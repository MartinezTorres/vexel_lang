#include "machine.hpp"
#include <algorithm>
#include <stdexcept>

namespace vx::relational {
void compactTypes(Program& p) {
    auto& t = p.terms;
    std::vector<Clause> layout;
    for (unsigned bits : {8u,16u,32u})
        layout.push_back({{t.make("UInt",{t.number(bits)}),t.number(bits/8)}, {}, "unsigned " + std::to_string(bits)});
    auto element = t.variable("element"), count = t.variable("count");
    auto elementBytes = t.variable("elementBytes"), bytes = t.variable("bytes");
    layout.push_back({{t.make("Array",{element,count}),bytes},
        {call("compact_size",{element,elementBytes}), lessEqual(t.number(0),count),
         multiply(elementBytes,count,bytes)}, "array"});
    auto first = t.variable("first"), second = t.variable("second");
    auto a = t.variable("firstBytes"), b = t.variable("secondBytes");
    layout.push_back({{t.make("Pair",{first,second}),bytes},
        {call("compact_size",{first,a}),call("compact_size",{second,b}),add(a,b,bytes)}, "pair"});
    p.define("compact_size",2,std::move(layout));
    auto maximum = t.variable("maximum");
    std::vector<Clause> representable;
    for (unsigned bits : {8u,16u,32u})
        representable.push_back({{t.make("UInt",{t.number(bits)}),maximum},
            {lessEqual(t.number(0),maximum),lessEqual(maximum,t.number((Integer(1) << bits)-1))},
            "unsigned range " + std::to_string(bits)});
    p.define("unsigned_range",2,std::move(representable));
    // This describes an explicit byte encoding, not native C struct alignment.
    // Nominal schemas can add their own clauses through a separate relation.
}
Id ActivationGraph::event(std::string name) { events.push_back(std::move(name)); return events.size()-1; }
void ActivationGraph::before(Id first, Id second) {
    if (first >= events.size() || second >= events.size()) throw std::invalid_argument("unknown activation event");
    order.emplace(first,second);
}
void ActivationGraph::live(std::string name, Id begin, Id end, Ref size, Ref offset) {
    before(begin,end); storage.push_back({std::move(name),begin,end,std::move(size),std::move(offset)});
}
bool ActivationGraph::precedes(Id first, Id second) const {
    std::vector<Id> todo{first}; std::set<Id> seen;
    while (!todo.empty()) {
        Id at = todo.back(); todo.pop_back();
        if (!seen.insert(at).second) continue;
        for (auto [from,to] : order) if (from == at) {
            if (to == second) return true;
            todo.push_back(to);
        }
    }
    return false;
}
bool ActivationGraph::valid() const {
    for (Id event = 0; event < events.size(); ++event) if (precedes(event,event)) return false;
    return true;
}
ActivationGraph::Overlap ActivationGraph::overlap(const Lifetime& first, const Lifetime& second) const {
    // A completion may itself be the next lifetime's start: [begin,end)
    // intervals do not overlap at that handoff event.
    if (first.end == second.begin || second.end == first.begin ||
        precedes(first.end,second.begin) || precedes(second.end,first.begin)) return Overlap::Excluded;
    // In a closed partial order, unordered starts/ends admit overlapping
    // executions. In an open contract, further ordering may still exclude them.
    // Cross inequalities already force an intersection even in an open graph.
    if (closed || (precedes(first.begin,second.end) && precedes(second.begin,first.end)))
        return Overlap::Admitted;
    return Overlap::Unknown;
}
void ActivationGraph::constrain(Request& request) const {
    if (!valid()) {
        // Contradictory ordering is a contradiction in the declared contract.
        // Use an ordinary relation-machine equality, not a separate outcome path.
        Terms literals;
        request.goals.push_back(equal(literals.number(0),literals.number(1))); return;
    }
    if (!closed) request.goals.push_back(unknown("activation boundary is not closed"));
    for (const auto& life : storage) {
        if (life.begin >= events.size() || life.end >= events.size() || !precedes(life.begin,life.end))
            throw std::invalid_argument("storage lifetime lacks its start/completion relationship");
    }
    for (std::size_t i = 0; i < storage.size(); ++i)
        for (std::size_t j = i+1; j < storage.size(); ++j)
            if (overlap(storage[i],storage[j]) == Overlap::Admitted)
                request.goals.push_back(disjoint(storage[i].offset,storage[i].size,storage[j].offset,storage[j].size));
            else if (overlap(storage[i],storage[j]) == Overlap::Unknown)
                request.goals.push_back(unknown("unproved overlap: " + storage[i].name + " / " + storage[j].name));
}
Demonstration demonstration(Program& p, const std::string& mode, unsigned budget) {
    if (mode != "serial" && mode != "overlap" && mode != "retained" && mode != "recursive" && mode != "open")
        throw std::invalid_argument("unknown activation demonstration");
    if (budget > 64) throw std::invalid_argument("demonstration payload budget exceeds 64 bytes");
    compactTypes(p); auto& t = p.terms;
    Demonstration d;
    d.element = t.variable("element"); d.count = t.variable("count"); d.width = t.variable("width");
    d.bytes = t.variable("bytes"); d.firstOffset = t.variable("firstOffset");
    d.secondOffset = t.variable("secondOffset"); d.extent = t.variable("extent");
    auto preference = t.variable("preference"), endA = t.variable("endA"), endB = t.variable("endB");
    p.define("element_profile",3,{
        {{t.make("UInt",{t.number(16)}),t.number(16),t.number(0)}, {}, "prefer 16-bit representation"},
        {{t.make("UInt",{t.number(8)}),t.number(8),t.number(1)}, {}, "8-bit representation"}
    });
    std::vector<Ref> offsets;
    for (unsigned at = 0; at <= budget; ++at) offsets.push_back(t.number(at));
    p.domain("offset",offsets);
    auto x = t.variable("x"), y = t.variable("y");
    p.define("maximum",3,{
        {{x,y,y},{lessEqual(x,y)},"second"},
        {{x,y,x},{lessEqual(y,x),different(x,y)},"first"}
    });
    auto buffer = t.make("Array",{d.element,d.count});
    d.request.goals = {
        call("element_profile",{d.element,d.width,preference}),
        call("unsigned_range",{d.element,t.number(252)}),
        equal(d.count,t.number(3)), call("compact_size",{buffer,d.bytes}),
        call("offset",{d.firstOffset}), call("offset",{d.secondOffset}),
        add(d.firstOffset,d.bytes,endA), add(d.secondOffset,d.bytes,endB),
        lessEqual(endA,t.number(budget)), lessEqual(endB,t.number(budget)),
        call("maximum",{endA,endB,d.extent})
    };
    // Source observation is a logical interface and computation contract.
    // Buffer encoding and offsets are physical choices, never source ambiguity.
    d.request.observation = {t.make("sum_two_triplets",{t.make("UInt",{t.number(16)})})};
    d.request.realization = {buffer,d.width,d.bytes,d.firstOffset,d.secondOffset,d.extent};
    d.request.minimize = {preference,d.extent,d.firstOffset,d.secondOffset};
    auto beginA = d.activation.event("source A begins");
    auto returnA = d.activation.event("source A returns");
    auto beginB = d.activation.event("source B begins");
    auto returnB = d.activation.event("source B returns");
    Id releaseA = returnA;
    if (mode == "retained") {
        releaseA = d.activation.event("sink releases A's payload");
        d.activation.before(returnA,releaseA);
        d.activation.before(returnA,beginB);
    } else if (mode == "serial") d.activation.before(returnA,beginB);
    else {
        d.activation.before(beginA,beginB); d.activation.before(returnB,returnA);
    }
    d.activation.before(beginA,returnA);
    d.activation.before(beginB,returnB);
    d.activation.live("A payload",beginA,releaseA,d.bytes,d.firstOffset);
    d.activation.live("B payload",beginB,returnB,d.bytes,d.secondOffset);
    if (mode == "open") d.activation.closed = false;
    d.activation.constrain(d.request);
    return d;
}
} // namespace vx::relational
