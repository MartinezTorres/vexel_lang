#pragma once
#include "source_numeric.hpp"

namespace vx::relational {
// One total, straight-line calculation interval with ordered results. The
// caller supplies typed snapshots, owns scratch storage, and evaluates the
// comma expressions in order without intervening operand mutation or effects.
// A single result stays at its original lazy evaluation site. Empty means
// recompute using the ordinary renderer.
struct CalculationInput {
    Ref type;
    std::string expression;
    // Values published by this activation (call results, loop state, loads)
    // still need the target's operand barrier when reused in an expression.
    bool materialized=false;
    // Closed bounds of this physical snapshot, after its transport conversion.
    // A residue carrier must not inherit the unconverted logical interval.
    IntegerBound range;
};
struct CalculationScratch { std::string name,type; std::size_t bytes; };
struct CalculationProjection {
    std::vector<std::string> expressions;
    std::vector<CalculationScratch> scratch;
    std::set<std::string> helpers;
    std::size_t sharedValues=0;
    std::size_t scratchReuses=0;
    std::size_t narrowedValues=0;
    std::size_t narrowedOperations=0;
    std::size_t linearCombinations=0;
    std::size_t sharedDivisions=0;
};
CalculationProjection projectCalculations(const std::vector<Ref>& values,
    const std::function<std::optional<CalculationInput>(const Ref&)>& input,bool reuseScratch=true,unsigned linearTerms=16,bool shareDivisions=false,bool unrollDivisions=true,bool rotationHelpers=false);
std::string calculationRuntimeIncludes(const std::set<std::string>& helpers);

// Realization search allowances, not assumptions about source values.
struct ValueOptions {
    unsigned choiceBranches=2;
    unsigned tableBytes=512;
    unsigned tableSearch=16;
    bool reuseScratch=true;
    unsigned linearTerms=16;
    bool scheduleOutputs=true;
    bool shareDivisions=false;
    bool unrollDivisions=true;
    bool rotationHelpers=false;
};
// An optional C realization of a closed, total integer value graph. Failure
// to certify this alternative leaves the ordinary execution projection intact.
struct ValueProjection {
    std::string header, reason;
    std::size_t sharedValues=0, localBytes=0;
    std::size_t scratchReuses=0;
    std::size_t narrowedValues=0;
    std::size_t narrowedOperations=0,earlyOutputs=0;
    std::size_t linearCombinations=0;
    std::size_t sharedDivisions=0;
    std::string casesHeader;
    std::vector<std::size_t> branchInputs;
    std::vector<std::vector<std::size_t>> caseInputs;
    // Optional fixed-address realization, offered only with an exclusive
    // activation proof. The embedding selects it and budgets its RAM.
    std::string fixedHeader, fixedReason;
    std::size_t fixedBytes=0;
};
ValueProjection projectValue(const std::string& name, const std::vector<Ref>& inputs,
                             const std::vector<Ref>& types, const std::vector<Ref>& values,
                             const std::vector<Ref>& paths, const Ref& execution,
                             bool exclusive=false,ValueOptions options={});
std::map<std::string,std::string> valueRuntimeFiles();

// A named C embedding occurrence. Both storage choices expose the same
// local-declaration macro, input/result fields and run entry.
struct ValueBindingProjection {
    std::string types, headerPrefix, automaticHeader, fixedFunction;
    std::size_t bytes=0;
    bool reuseScratch=true;
    std::size_t scratchReuses=0;
    std::size_t narrowedValues=0;
    std::size_t narrowedOperations=0,earlyOutputs=0;
    bool scheduleOutputs=false;
    std::size_t linearCombinations=0;
    unsigned linearTerms=16;
    bool shareDivisions=false;
    bool unrollDivisions=true;
    bool rotationHelpers=false;
    std::size_t sharedDivisions=0;
    std::size_t wideSelectBranches=0,wideByteCopies=0;
    std::size_t finiteTables=0,finiteTableBytes=0,finiteLookups=0;
    std::size_t finiteConstants=0,finiteGuards=0,finiteArithmetic=0;
    std::size_t sharedSelectBranches=0;
    std::size_t predicateBranches=0;
    std::vector<std::size_t> inputs;
    std::size_t regionStateBytes=0,regionScratchBytes=0,regionLoops=0,regionBoundaries=0;
    std::size_t regionCoalescedBytes=0,regionZeroBytes=0;
    std::size_t regionDirectDecisions=0;
    std::size_t regionPredicateFunctions=0,regionPredicateCalls=0;
    struct BoundarySlot {std::size_t bytes=0;std::vector<std::string> members;};
    std::size_t regionBoundaryBytes=0,regionBoundaryUnsharedBytes=0;
    std::vector<BoundarySlot> regionBoundarySlots;
    std::vector<std::pair<std::string,std::string>> regionBoundaryConflicts;
    std::vector<std::string> regionBoundaryDedicated;
    // Physical alternatives selected for the region's boundary interfaces.
    std::map<std::string,std::string> regionExpansions;
    std::vector<std::string> regionExpansionBarriers;
};
struct RegionBoundary {
    std::string name;
    std::string expansion;
    std::vector<Ref> inputs,outputs;
};
ValueBindingProjection projectRegionBinding(const std::string& name,const std::string& source,
    const std::vector<Ref>& inputs,const std::vector<Ref>& types,const std::vector<Ref>& values,
    const std::vector<Ref>& paths,const Ref& execution,const std::vector<RegionBoundary>& boundaries,
    bool scheduleCopies,bool transferFunctions,unsigned boundaryOverlay,bool predicateFunctions,bool reuseScratch=true,unsigned linearTerms=16);
ValueBindingProjection projectValueBinding(const std::string& name, const std::string& source,
    const std::string& entry, const std::vector<Ref>& inputs, const std::vector<Ref>& types,
    const std::vector<Ref>& values, const std::vector<Ref>& paths, const Ref& execution,
    ValueOptions options={});
}
