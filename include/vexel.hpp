#pragma once

#include <boost/multiprecision/cpp_int.hpp>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace vx {
using Big = boost::multiprecision::cpp_int;
using Id = std::size_t;
inline constexpr Id none = static_cast<Id>(-1);

struct Span { Id file = none; std::size_t begin = 0, end = 0; };
enum class Diagnostic { Invalid, UndefinedName, UndefinedFunction, UndefinedType, NoOverload, MissingMember, NotIterable, ImportResolution, ValueTrap, Exhausted, SearchAmbiguous, InvalidReplay, Unresolved };
struct Error : std::runtime_error {
    Span span;
    Diagnostic category;
    Error(Span s, const std::string& message, Diagnostic c = Diagnostic::Invalid) : std::runtime_error(message), span(s), category(c) {}
};
struct ChoiceRequired {
    Id context;
    std::string name;
    std::vector<Big> domain;
};
struct RecursionWidening { Id function; std::size_t parameter; bool capture = false; Id target = none; };
struct RecursionEffectRequired { Id function; Id target; };
struct SignatureRevision { Id target; std::string key; };
struct TypePropertiesRequired {
    std::vector<Id> types, schemas;
};

struct Type {
    enum Kind { Unknown, Empty, Exact, Integer, Fixed, Float, Boolean, String, Array, Record } kind = Unknown;
    unsigned bits = 0;
    unsigned fraction = 0;
    bool sign = false;
    std::size_t size = 0;
    std::shared_ptr<const Type> element;
    Id declaration = none;
    Id origin = none; // Record schema environment; not part of nominal identity.
    Id obligation = none; // A still-open property; excluded from nominal identity.
    Id schemaObligation = none; // Captured schema identity, independent of field layout.
    std::string name;
    std::string schema; // Frozen lexical values used by dependent record invariants.
    std::vector<std::string> fieldNames;
    std::vector<Type> fields;
    static Type integer(unsigned bits, bool sign);
    static Type fixed(unsigned integerBits, unsigned fractionalBits, bool sign);
    static Type floating(unsigned bits) { Type t; t.kind = Float; t.bits = bits; return t; }
    static Type array(Type element, std::size_t size);
    static Type record(Id declaration, std::string name, std::vector<std::string> names, std::vector<Type> fields);
    static Type simple(Kind k) { Type t; t.kind = k; return t; }
    bool concrete() const;
    bool settled() const;
    bool operator==(const Type& other) const;
    std::string str() const;
};
struct RecursionTypeRequired { Id target; std::string key; Type type; };
struct TypeEquationsRequired { std::map<Id,Type> witnesses; };

struct Value {
    enum Kind { Empty, Number, Bytes, Elements, Runtime, Symbolic } kind = Empty;
    Big number = 0;
    std::string bytes;
    std::vector<Value> elements;
    Id context = none, variable = none;
    static Value integer(Big n);
    static Value string(std::string s);
    static Value array(std::vector<Value> a);
    static Value runtime();
    static Value symbolic(Id context, Id variable);
    bool symbolic() const;
    bool known() const;
    bool operator==(const Value& other) const;
    std::string str() const;
};

// Runtime control must survive even when the eventual result is a constant:
// a residual loop can fail to terminate without performing external IO.
enum class Effect { Pure, Local, Control, External };
struct Fact {
    Type type;
    Value value;
    Effect effect = Effect::Pure;
};
std::string formatValue(const Type& type, const Value& value);

// Structural sites and resolution obligations share the same arena. There is
// no AST owned by a later phase, and no evaluator outside the worklist rules.
enum class Syntax {
    None, File, Lexeme, Trivia, Function, Parameter, Global, Type, Import,
    Block, Declare, Assign, Return, Name, Number, String, Character, Unary,
    Binary, Call, Select, Array, Index, Cast, Resource, Iterate, Group, Loop, Repeat,
    Solve, Choice, Require, RecordDecl, Construct, Member, Tuple, Probe, MethodProbe, Destructure
};
enum class Rule {
    None, Constant, Input, Type, Global, GlobalRead, Bind, Unary, Binary, ShortCircuit,
    Select, Array, Index, Cast, Invoke, Finish, Entry, Slot, Store, Load,
    Step, StateOf, Merge, Iterate, Resource, Adapt, Update,
    Loop, LoopHeader, LoopExit, LoopValue, LoopItem, Repeat, Sort, Solve, Choice, Require,
    Construct, FieldIndex, Tuple, Probe, Deferred, Alias, CallProbe, ShapeCheck,
    Sequence, Exit, FunctionResult, Resume, ExitJoin, Unpack, Evaluate, CallState,
    CustomIteration, IterationBody, TypeJoin, PropertyLoad, PropertyValue, PropertyCall, Math
};

// Completion is independent of a normal expression value. Exits retain their
// payload and state; joining control paths must not read an exited path again.
struct Completion {
    bool normal = true;
    std::vector<Id> exits;
    void include(const Completion& other) {
        for (Id exit : other.exits) {
            bool found = false; for (Id old : exits) if (old == exit) { found = true; break; }
            if (!found) exits.push_back(exit);
        }
    }
};

struct Metadata {
    std::string name, text;
    std::vector<Id> arguments;
    Span span;
};
struct Node {
    Syntax syntax = Syntax::None;
    Rule rule = Rule::None;
    Span span;
    std::string text, identity;
    std::vector<std::string> metadata;
    std::vector<Metadata> attributes;
    std::vector<Id> children;
    Id annotation = none, body = none;
    bool exported = false, external = false, noinline = false, terminated = false;
    bool expression = false;
    std::size_t receiverCount = 0;
    std::vector<Id> receivers;

    // Instantiated graph edges and rule-local expansion bookkeeping.
    Id site = none, scope = none, previous = none, slot = none;
    Id expansion = none, endState = none;
    Id lexicalScope = none;
    std::vector<Id> inputs, branches, ends, candidates;
    // Closed runtime regions keep entry seeds separate from cyclic runtime
    // state. These edges describe carried storage, not unfinished constants.
    Id region = none;
    Id runtimeBackedge = none; // Runtime state flow, never a fact-demand dependency.
    Id signature = none, signatureOrigin = none; // Independent, non-executing call projection.
    bool valueDependentSignature = false;
    std::vector<Id> carried, seeds;
    std::vector<Id> environment;
    unsigned phase = 0;
    std::vector<Id> dependencies, users;
    std::vector<Id> typeDependencies, typeUsers, valueTypeUsers;
    std::optional<Fact> fact;
    std::optional<Type> inferredType;
    std::optional<Error> typeError;
    bool typeExpanded = false;
    bool typeDemanded = false, typeQueued = false;
    bool demanded = false, queued = false, expanded = false, readonly = false;
    std::string key;
    Completion completion;
    Completion typeCompletion;
    bool mayExit = false;
};

struct File { std::filesystem::path path; std::string bytes; bool source = true; Id node = none; };
struct Scope {
    struct Callable { Id declaration, scope; };
    struct Expression { Id site, scope, annotation; bool iteration = false; };
    Id node = none, parent = none, caller = none, puzzle = none;
    Id function = none, loop = none;
    Value guard = Value::integer(1);
    bool runtimeControl = false;
    bool typeValues = false;
    std::string modulePath;
    std::map<std::string, Id> slots;
    std::map<std::string, Id> constants;
    std::map<std::string, Id> types;
    std::map<std::string, std::vector<Callable>> functions;
    std::map<std::string, Expression> expressions;
};
struct AdviceRecord { std::string request,response,status; };
struct SearchBudget {
    std::size_t decisions = 0, checks = 0;
    // Work already spent survives semantic rollback. Keep its bounded evidence
    // too, including advice obtained in a rejected structural revision.
    std::map<std::string,std::vector<AdviceRecord>> advice;
};
struct Options {
    std::size_t maxNodes = 200000, maxSteps = 2000000, maxElements = 16384;
    unsigned maxBits = 65536;
    unsigned strictness = 0;
    bool allowProcesses = false;
    std::size_t processTimeoutMs = 1000;
    std::size_t maxFileBytes = 16 * 1024 * 1024;
    std::string schedule = "fifo";
    bool trace = false;
    std::size_t maxDecisions = 100000, maxConstraintChecks = 100000000;
    std::string branchOrder = "forward";
    std::string heuristicProgram, heuristicReplay;
    std::size_t maxProposals = 32, heuristicTimeoutMs = 1000;
    std::string compiledSearch;
    bool verifySearchKernel = false;
    std::map<std::string,Big> structuralAssumptions;
    std::shared_ptr<SearchBudget> searchBudget;
};

struct ConstraintProblem;
struct Graph {
    Options options;
    std::deque<Node> nodes;
    std::vector<File> files;
    std::vector<Scope> scopes;
    std::map<std::string, std::vector<Id>> symbols;
    std::map<std::string, Id> types;
    std::map<std::string, Id> moduleBindings;
    // Target inputs are ordinary graph facts, available before source resolution.
    std::map<std::string, Id> targetFacts;
    std::map<Id, std::shared_ptr<ConstraintProblem>> puzzles;
    std::map<std::string, Id> loaded;
    std::shared_ptr<std::map<std::string,std::string>> inputSnapshots = std::make_shared<std::map<std::string,std::string>>();
    std::shared_ptr<std::map<std::string,std::vector<std::string>>> directorySnapshots = std::make_shared<std::map<std::string,std::vector<std::string>>>();
    std::shared_ptr<std::map<std::string,std::pair<std::string,std::string>>> processSnapshots = std::make_shared<std::map<std::string,std::pair<std::string,std::string>>>();
    std::set<std::string> dependencies;
    std::filesystem::path project;
    explicit Graph(Options o = {}) : options(std::move(o)) {}
    Id add(Node n);
    Id semantic(Rule rule, Id site, std::vector<Id> inputs = {}, Id scope = none);
    Id scope(Id parent, Id site, const std::string& identity);
    Id read(const std::filesystem::path& path, bool source, Span requester = {});
    std::filesystem::path resolve(const std::string& path, Span from);
    std::string location(Span span) const;
    std::string identity(Id site) const;
};

void loadProgram(Graph& graph, const std::filesystem::path& path);
std::vector<Id> moduleDeclarations(const Graph& graph, Id block);
bool dependentField(const Graph& graph, Id declaration, Id annotation);
std::string searchReport(const Graph& graph, const Error* error = nullptr);
std::string quote(const std::string& s);
std::string ruleName(Rule r);
std::string effectName(Effect e);
int compareValues(const Value& a, const Value& b);
std::optional<int> compareValues(const Type& type, const Value& a, const Value& b);
bool representationCast(const Type& from,const Type& to);
std::optional<std::size_t> representationBytes(const Type& type);

struct Built { Id value = none, state = none; };
struct Root { std::string name; Id node; bool abi = false, constant = false; };

struct MsxOptions {
    std::filesystem::path directory;
    std::filesystem::path assets;
    std::string entry;
    unsigned stackBytes = 1024;
    unsigned romSegments = 256;
    std::set<std::string> exclusiveEntries;
};
void installMsxFacts(Graph& graph, const MsxOptions& options);
void exportMsx(Graph& graph, const std::vector<Root>& roots, const MsxOptions& options);

class Solver {
public:
    Graph& g;
    std::vector<Root> roots;
    std::size_t steps = 0, invocations = 0, cacheHits = 0;
    explicit Solver(Graph& graph) : g(graph) {
        if (!g.options.searchBudget) g.options.searchBudget = std::make_shared<SearchBudget>();
    }
    void exportRoots();
    void root(const std::string& name);
    void call(const std::string& name, const std::vector<std::string>& args);
    void solve();
    std::string report(bool json) const;
    std::string dot() const;
    std::string depfile(const std::string& target) const;
private:
    std::vector<Id> queue;
    std::size_t fifoHead = 0;
    std::mt19937 rng{0x56455845};
    std::map<Id, Id> globals;
    std::map<std::string, Id> pureCalls;
    std::map<Id, Id> annotations;
    std::map<std::tuple<Id, Id, Id>, Id> scopedAnnotations;
    std::map<std::tuple<Id, Id, Id>, Id> parameterAnnotations;
    std::map<std::pair<Id, Id>, Id> loads;
    std::map<std::pair<Id, Id>, Id> propertyLoads;
    std::map<Id,Id> propertyValues;
    std::map<Id,Type> typeEquations;
    std::map<Id,Id> equationChecks;
    std::map<Id,Id> equationWitnesses;
    struct TypeAliasProof { Id target, join, requirement; };
    std::map<Id,TypeAliasProof> typeAliasProofs;
    using PropertyKey = std::pair<Id,std::vector<std::size_t>>;
    struct PropertyNodes { Id witness, check; };
    struct PropertyProof { Id check; Type candidate; };
    std::map<PropertyKey,PropertyNodes> propertyNodes;
    std::map<PropertyKey,PropertyProof> propertyProofs;
    std::set<PropertyKey> propertyAssumptions;
    bool propertyTrial = false;
    bool typeEquationTrial = false;
    struct Checkpoint;
    std::map<Id,std::shared_ptr<Checkpoint>> checkpoints;
    std::set<Id> ownedPuzzles;
    std::shared_ptr<std::size_t> structuralTrials = std::make_shared<std::size_t>(0);
    void copyContext(Solver& destination) const;
    void checkpoint(Id context);
    void branch(const ChoiceRequired& request);
    [[noreturn]] void requestChoice(const Value& value, Id at) const;
    mutable bool evaluatingRelation = false;
    bool processingType = false;
    Id unit(Id site);
    Id literal(Id site, Type t, Value v);
    Id type(Id site, Id scope = none, Id state = none);
    Id argumentType(Id site, Id scope, Id state, Id argument);
    Id parameterType(Id call, Id function, std::size_t index, Id lexical);
    Id global(Id decl);
    Id constant(const std::string& name, Id scope) const;
    Id binding(const std::string& name, Id scope);
    Id slot(const std::string& name, Id scope) const;
    Id namedType(const std::string& name, Id scope) const;
    std::vector<Scope::Callable> functions(const std::string& name, Id scope) const;
    Id snapshot(Id scope, Id site);
    Built build(Id site, Id scope, Id state);
    Built buildNow(Id site, Id scope, Id state);
    Built callExpression(Id site, Id scope, Id state);
    struct Place { Id slot, value, state; std::vector<Id> offsets; };
    std::optional<Place> place(Id site, Id scope, Id state);
    Built block(Id site, Id scope, Id state, bool functionBody = false);
    bool hasCompletion(Id site, Id scope = none) const;
    void completionRule(Id id, bool typesOnly = false);
    void sequenceStatement(Id id, std::size_t index);
    Id resume(Id state, Id target, const std::string& mode, Id site, Id scope);
    Id resumedLoad(Id variable, Id state, Id site);
    Built finishExpression(Id value, Id state);
    Id adapt(Id value, Id target, Id site, bool cast = false);
    Id load(Id slot, Id state, Id site);
    Id propertyLoad(Id slot, Id state, Id site);
    Id propertyValue(Id value);
    bool mayWrite(Id site, Id scope, Id variable) const;
    void prepareGlobal(Id id);
    void globalTypeAliases(Id id);
    Id typeEquationWitness(Id owner,Id supplied);
    bool inferTypeEquations();
    bool inferContextTypeEquations();
    bool inferTypeAliases();
    bool inferTypeProperties();
    bool verifyTypeProperties();
    bool verifyTypeEquations();
    static bool stableEquationType(const Type& type);
    void reviseTypeEquations(const TypeEquationsRequired& request, const Graph& base, const Solver& context);
    void demand(Id id);
    void enqueue(Id id);
    const Fact* need(Id owner, Id dependency);
    const Type* needType(Id owner, Id dependency);
    void enqueueType(Id id);
    void publishType(Id id, Type type);
    void typeRule(Id id);
    void selectTypeRule(Id id,bool typesOnly);
    bool resolveCall(Id id, bool typesOnly);
    void publish(Id id, Fact fact);
    void rule(Id id);
    void loopRule(Id id);
    void loopTypeRule(Id id);
    void sortRule(Id id);
    bool logicalRule(Id id, bool typesOnly);
    void puzzleRule(Id id);
    void recordRule(Id id, bool typesOnly = false);
    bool recordConversion(Id id, const Fact& value);
    bool recordSchema(Id id, Type& type);
    void broadcastRule(Id id, bool typesOnly = false);
    void mathRule(Id id, bool typesOnly = false);
    Type mathType(const std::string& name, const std::vector<Type>& inputs, Id at) const;
    Fact mathValue(const std::string& name, const std::vector<Fact>& inputs, Id at) const;
    void resourceRule(Id id,bool typesOnly = false);
    Fact relation(const std::vector<Fact>& inputs, Id at,
                  const std::function<Fact(const std::vector<Fact>&)>& operation,
                  Type expected = {}) const;
    bool guarded(Id at) const;
    Value guardedBy(Id scope, const Fact& condition, bool positive, Id at);
    Fact chooseValue(const Fact& condition, const Fact& yes, const Fact& no, Id at) const;
    Fact symbolicIndex(const Fact& array, const Fact& index, Id at) const;
    [[noreturn]] void fail(Id id, const std::string& message, Diagnostic category = Diagnostic::Invalid) const;
    Fact convert(const Fact& fact, const Type& target, bool cast, Id at) const;
    Type conversionType(const Type& source, const Type& target, bool cast, Id at) const;
    Type resolvedType(const Type& type) const;
    void requireTypeProperties(const Type& type) const;
    Fact representation(const Fact& fact,const Type& target,Id at) const;
    Type common(const Type& a, const Type& b, Id at) const;
    Fact binary(const std::string& op, const Fact& a, const Fact& b, Id at) const;
    Fact unary(const std::string& op, const Fact& value, Id at) const;
    void instantiate(Id id, Id function);
    void solveWorklist();
    void drainWorklist();
    bool inferRecursiveInterfaces();
    std::set<std::pair<Id,std::size_t>> runtimeParameters;
    std::set<std::pair<Id,Id>> runtimeCaptures;
    std::set<Id> externalRecursion;
    std::set<std::string> recursiveRuntimeInstances;
    std::map<std::string,Type> recursiveInterfaces;
    std::set<std::string> specializedSignatures;
    bool signatureSpecialization(Id scope) const;
    std::string explanation(Id id) const;
};
} // namespace vx
