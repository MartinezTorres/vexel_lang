#pragma once
#include "source_numeric.hpp"
#include "term_index.hpp"
#include <iosfwd>
#include <tuple>

namespace vx::relational {
// A dependency closure over logical interfaces and loop state, before choosing
// their physical transport. One selected projection owns all these facts.
class InterfaceDemand {
public:
    using Range=IntegerRange;
    using Bound=std::optional<Range>;
    struct Function {
        Ref trace;
        std::vector<Ref> results,resultTypes,argumentTypes,captureTypes;
        std::set<std::size_t> arguments,captures;
        std::map<std::size_t,std::size_t> slots;
        // Constants or immutable formal aliases, never substituted computations.
        std::map<std::size_t,Ref> knownResults;
        // Equal leaves at the same invocation's return, with identical types.
        std::map<std::size_t,std::size_t> resultAliases;
        std::vector<Bound> argumentRanges,captureRanges,resultRanges;
        // Private transport can be narrower than the semantic scalar type.
        std::vector<Ref> argumentStorageTypes,captureStorageTypes,resultStorageTypes;
        // Required low bits, joined across every consumer. Zero means no value
        // demand; effects and completion have independent obligations.
        std::vector<unsigned> argumentBits,captureBits,resultBits;
        // Derived from all executions and all logical results, including
        // untransported leaves. Recursion and loops need separate proofs.
        bool discardableExecution=false;
        // Completion certificates in this function's closed caller envelope.
        // Demand analysis and every C projection consume the same certificate.
        std::vector<bool> discardableResults;
    };
    struct Loop {
        struct Slot {Id root;std::size_t count;std::map<std::size_t,std::size_t> leaves;bool ready=false,alwaysReady=false;std::size_t invariantLeaves=0;};
        Ref trace;
        std::optional<Id> owner;
        std::vector<Slot> slots;
        std::map<std::pair<Id,std::size_t>,std::pair<Id,std::size_t>> aliases;
        std::vector<Ref> storageTypes;
        std::vector<unsigned> bits;
        bool index=false;
    };
    explicit InterfaceDemand(Terms& terms):terms_(terms) {}
    void function(Id id,Function function);
    void root(Ref trace,std::vector<Ref> observations);
    void close();
    const Function& at(Id id) const {return functions_.at(id);}
    const std::map<Id,Loop>& loops() const {return loops_;}
    std::size_t nodes=0,edges=0,required=0;
    std::size_t discardedCalls() const {return discardedCalls_.size();}
    // A separate physical view. The selected semantic terms stay unchanged.
    Ref rewrite(const Ref& value,std::optional<Id> owner={});
    Ref transportedResult(Id function,std::size_t leaf);
    // Read-only facts for the ordinary projection's physical leaf numbering.
    // Uses the closed caller/backedge envelopes and applies the actual carrier
    // conversion; does not create another range-analysis environment.
    Bound transportRange(const Ref& value,std::optional<Id> owner={}) const;
    // Shared value facts, without changing logical call/loop numbering. Other
    // C interfaces consume this view before selecting their own transport.
    Ref facts(const Ref& value,std::optional<Id> owner={});
    void reportRanges(std::ostream& out) const;
    static bool discardable(const Ref& value);
private:
    Terms& terms_;
    std::map<Id,Function> functions_;
    std::map<Id,Loop> loops_;
    std::vector<std::pair<Ref,std::vector<Ref>>> roots_;
    using Owner=std::optional<Id>;
    using Guards=std::vector<std::pair<Ref,bool>>;
    struct Call {
        struct Occurrence {Ref trace;Guards guards;};
        Id callee;Owner owner;Ref trace;
        std::vector<Occurrence> occurrences;
    };
    std::map<Id,Call> calls_;
    std::set<Id> discardedCalls_;
    using Leaf=std::tuple<Id,Id,std::size_t>;
    std::map<Leaf,Ref> invariants_;
    std::map<Leaf,Leaf> loopAliases_;
    std::set<std::pair<Id,Id>> ready_;
    using Cache=std::map<Owner,std::map<Ref,Ref,std::owner_less<Ref>>>;
    Cache normalized_;
    std::map<Leaf,Bound> loopRanges_;
    // Analysis-local structural identities. They preserve constructor kinds,
    // owners and occurrence IDs; printed spelling is never a proof key.
    TermIndex expressionIdentities_;
    // These reductions depend only on exact terms and explicit word widths,
    // never on an owner's current range/induction hypotheses. A failed bounded
    // algebra attempt means only that this reduction supplied no certificate.
    std::map<Id,std::optional<Ref>> algebraCache_;
    std::map<Owner,std::map<Id,Bound>> rangeCache_;
    std::set<std::pair<Id,Id>> rangeReady_;
    struct RangeContext {
        std::vector<Bound> arguments,captures;
        std::map<Id,Bound> assumptions;
        std::optional<Id> environment;
    };
    // Immutable abstract environments share answers across result leaves and
    // branch visits. A new range round discards every environment and answer.
    std::map<std::vector<Integer>,Id> rangeEnvironments_;
    std::map<Id,std::map<Id,Bound>> rangeQueries_;
    // A context interprets the existing value graph; it never clones code or
    // substitutes a computation for an invocation's immutable result snapshot.
    std::map<std::vector<Integer>,RangeContext> rangeContexts_;
    std::map<Id,std::vector<Bound>> callRanges_;
    struct CallInputs {bool possible=false;std::vector<Bound> arguments,captures;};
    std::map<Id,CallInputs> callInputs_;
    std::set<Id> activeRangeFunctions_,activeRangeCalls_;
    static constexpr unsigned maxRangeContexts=256,maxContextNodes=16384,maxContextDepth=32;
    unsigned contextNodes_=0,peakRangeContexts_=0;
    unsigned peakContextNodes_=0,peakRangeEnvironments_=0;
    std::size_t rangeQueryHits_=0,rangeQueryMisses_=0;
    unsigned branchDepth_=0,peakBranchDepth_=0;
    bool contextBudgetHit_=false;
    unsigned rangeRounds_=0;
    bool rangesConverged_=false,rangesReady_=false;
    Id rangeExpression(const Ref& value);
    Id rangeEnvironment(Owner owner,RangeContext& context);
    Bound range(const Ref& value,Owner owner);
    Bound range(const Ref& value,Owner owner,RangeContext* context);
    Bound callRange(Id site,std::size_t leaf,Owner owner,RangeContext* context);
    CallInputs incomingRanges(const Call& call);
    std::pair<bool,Bound> branchRange(const Ref& value,const Ref& condition,bool truth,Owner owner,RangeContext* context,const Leaf* preserved=nullptr);
    std::pair<bool,Bound> changingRange(Ref value,const Leaf& preserved,Owner owner,RangeContext* context);
    bool assumeRange(const Ref& condition,bool truth,Owner owner,RangeContext& context,unsigned depth=0);
    bool refineRange(Ref value,Bound bound,Owner owner,RangeContext& context);
    void resetRangeContexts();
    void resolveRanges();
    void planStorage();
    Ref narrowTransport(Ref value,const Ref& logical,const Ref& storage);
    Ref carriedStorage(Id loop,Id root,std::size_t leaf) const;
    void resolveFacts(bool publish=true);
    void resolveExecutions();
    bool discardableIn(const Ref& value,Owner owner,const Guards& guards={});
    static bool stableAcrossLoop(const Ref& value);
    bool resolveEqualities();
    Ref normalize(const Ref& value,Owner owner);
    std::optional<std::pair<Integer,Integer>> bounds(const Ref& value,Owner owner) const;
    // Hold source nodes too: some projection roots are temporary Sequences.
    // A raw-address cache would outlive them and could reuse a stale rewrite.
    Cache rewritten_;
    Cache storageFacts_;
    bool closed_=false;
};
}
