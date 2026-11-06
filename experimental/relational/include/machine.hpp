#pragma once

#include <boost/multiprecision/cpp_int.hpp>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace vx::relational {
using Integer = boost::multiprecision::cpp_int;
using Id = std::size_t;
struct ConstructionLimit : std::runtime_error { using std::runtime_error::runtime_error; };

// Terms describe values and interfaces. Constructors have no built-in meaning
// to the machine: adapters contribute that meaning as ordinary relations.
struct Term;
using Ref = std::shared_ptr<const Term>;
struct Term {
    enum Kind { Variable, Number, Constructor } kind;
    bool closed=false;
    Id variable = 0;
    Integer number = 0;
    std::string name;
    std::vector<Ref> arguments;
};
class Terms {
    Id next = 0;
public:
    Ref variable(std::string name = {});
    Ref number(Integer value);
    Ref make(std::string name, std::vector<Ref> arguments = {});
};
std::string show(const Ref& term);
bool identical(const Ref& left, const Ref& right);
bool ground(const Ref& term);

struct Bindings {
    struct Patch {
        std::map<Id,Ref> values;
        std::set<Id> integers;
        std::map<Id,std::optional<std::set<Integer>>> domains;
    };
    class Transaction;
    Transaction* transaction=nullptr;
    Bindings()=default;
    Bindings(const Bindings& other):values(other.values),integers(other.integers),domains(other.domains),touched(other.touched),revision(other.revision) {}
    Bindings& operator=(const Bindings& other) {values=other.values;integers=other.integers;domains=other.domains;touched=other.touched;revision=other.revision;transaction=nullptr;return *this;}
    Bindings(Bindings&&)=default;
    Bindings& operator=(Bindings&&)=default;
    void apply(const Patch& patch);
    void setValue(Id id,Ref value);
    void setDomain(Id id,std::optional<std::set<Integer>> domain);
    void setInteger(Id id);
    std::map<Id, Ref> values;
    std::set<Id> integers;
    std::map<Id,std::set<Integer>> domains;
    std::set<Id> touched;
    std::size_t revision=0;
    bool constrain(Ref term,const std::set<Integer>& domain);
    Ref walk(Ref term) const;
    Ref resolve(const Ref& term) const;
    bool occurs(Id variable, const Ref& term) const;
    bool integer(const Ref& term);
    // A failed unification may have changed this private store. Callers either
    // discard that context or unify in a copy; it is never a published fact.
    bool unify(Ref left, Ref right);
};

class Bindings::Transaction {
    Bindings& bindings;
    Transaction* previous;
    Patch old;
    std::set<Id> touched;
    std::size_t revision;
public:
    explicit Transaction(Bindings& at):bindings(at),previous(at.transaction),touched(at.touched),revision(at.revision) {at.transaction=this;}
    ~Transaction();
    Patch changes() const;
    void value(Id id);
    void domain(Id id);
    void integer(Id id);
};

struct Goal {
    enum Kind { Equal, Finite, Different, Add, Multiply, Modulo, Compute, BooleanOr, LessEqual, Disjoint, Call, Require, Build, Witness, Ambiguity, Unknown, AwaitInput } kind;
    std::vector<Ref> arguments;
    std::string label;
};
Goal equal(Ref a, Ref b);
Goal different(Ref a, Ref b);
Goal finite(Ref value,const std::vector<Ref>& domain);
Goal add(Ref a, Ref b, Ref result);
Goal multiply(Ref a, Ref b, Ref result);
Goal modulo(Ref value, Ref modulus, Ref result);
Goal compute(std::string operation, Ref a, Ref b, Ref result);
// Propagates partial Boolean information without guessing unknown producers.
Goal booleanOr(Ref a, Ref b, Ref result);
Goal lessEqual(Ref a, Ref b);
Goal disjoint(Ref startA, Ref sizeA, Ref startB, Ref sizeB);
Goal call(std::string relation, std::vector<Ref> arguments);
// A bundle shares its variables and is activated once per search context.
// A builder runs only when its declared input prefix is ground. It constructs
// obligations; it cannot read the context or mutate its bindings.
Goal require(std::string bundle);
Goal build(std::string builder, std::vector<Ref> arguments);
// Retain an artifact in this candidate only. Its term must close along with
// the other demanded projections before the candidate may be published.
Goal witness(std::string name, Ref artifact);
Goal ambiguity(std::string reason);
Goal unknown(std::string reason);
// A proof boundary can request named evidence without exposing its bindings.
// Only unfinished contexts contribute outstanding input names to Result.
Goal awaitInput(Ref value,std::string name);

struct Clause {
    std::vector<Ref> head;
    std::vector<Goal> body;
    std::string label;
};
struct Relation {
    std::size_t arity = 0;
    bool closed = true;
    std::vector<Clause> clauses;
};
class Program {
public:
    struct Builder {
        std::size_t inputs, arity;
        std::function<std::vector<Goal>(const std::vector<Ref>&)> construct;
    };
    Terms terms;
    std::map<std::string, Relation> relations;
    std::map<std::string, std::vector<Goal>> bundles;
    std::map<std::string, Builder> builders;
    void define(std::string name, std::size_t arity, std::vector<Clause> clauses,
                bool closed = true);
    void domain(std::string name, const std::vector<Ref>& values);
};

struct Request {
    std::vector<Goal> goals;
    // Proposal discovery can ignore an explicitly ambiguous branch: it has
    // no complete model to contribute. This does not suppress ambiguity
    // between completed observations, or unfinished alternatives. The owning
    // semantic request must still check the original obligations normally.
    bool proposalsOnly = false;
    // Isolated proof requests can replace selected obligation bundles with
    // explicit assumptions. Overrides belong to this request, never Program.
    // A caller must validate any resulting proposal against the original
    // bundles before treating it as a source fact.
    std::map<std::string,std::vector<Goal>> overrides;
    // The same proof boundary can cover bundles discovered by later builders.
    // This factory sees a bundle name, never a context or a binding store.
    std::function<std::optional<std::vector<Goal>>(const std::string&)> overrideBundle;
    std::vector<Ref> observation;
    // These candidate-local artifacts, including their absence, participate
    // in observable uniqueness after every obligation has closed. Absence in
    // an unfinished candidate never proves that an artifact is unnecessary.
    std::set<std::string> observedWitnesses;
    // Close indirect observations through this candidate's already-ground
    // artifacts. The adapter may canonicalize local identities, but receives
    // no binding store and cannot discharge obligations. nullopt means that
    // the semantic projection is unfinished. The displayed observation stays
    // intact; uniqueness compares the returned key when this hook is present.
    std::function<std::optional<std::vector<Ref>>(const std::vector<Ref>&,
                                                const std::map<std::string,Ref>&)> observationClosure;
    std::vector<Ref> realization;
    // Explicit lexicographic policy over physical candidates. It never selects
    // between disagreeing source observations.
    std::vector<Ref> minimize;
};
struct SearchBudget {
    std::size_t maxWork=1000000,maxDecisions=100000,work=0,decisions=0;
};
struct Options {
    std::string schedule = "fifo", branches = "forward";
    std::size_t maxWork = 1000000, maxDecisions = 100000, maxDepth = 128;
    unsigned seed = 731;
    // Optional common fuel for nested or cooperating proof requests. Each
    // Machine still has its local limits and isolated bindings.
    std::shared_ptr<SearchBudget> budget;
    bool operator==(const Options&) const = default;
};
enum class Outcome { Resolved, Contradictory, Ambiguous, Unresolved, Exhausted };
std::string outcomeName(Outcome outcome);
struct Statistics {
    std::size_t work = 0, decisions = 0, contradictions = 0, models = 0;
    std::size_t unfinished = 0, maxDepth = 0;
};
struct Model {
    std::vector<Ref> observation, realization;
    std::vector<Ref> observationKey;
    std::vector<Integer> cost;
    std::vector<std::string> assumptions;
    std::map<std::string,Ref> witnesses;
};
struct Result {
    Outcome outcome = Outcome::Unresolved;
    std::optional<Model> selected, alternative;
    Statistics stats;
    std::vector<std::string> pending;
    std::set<std::string> inputs;
};

class Machine {
    Program& program;
    Options options;
    std::mt19937 random;
    struct Context {
        Bindings bindings;
        std::map<Id,Goal> goals;
        std::set<Id> ready;
        std::map<Id,std::set<Id>> watchers;
        std::map<Id,std::set<Id>> waiting;
        std::map<Id,std::size_t> choices;
        Id nextGoal=0;
        std::vector<std::string> assumptions;
        std::set<std::string> activated;
        std::map<std::string,Ref> witnesses;
    };
    struct Expansion { Bindings::Patch bindings; std::vector<Goal> goals; std::string label; };
    Result result;
    bool exhausted = false;
    bool tick();
    Ref fresh(const Ref& term, std::map<Id, Ref>& variables);
    std::vector<Expansion> expand(Context& context, std::size_t index);
    enum class Progress { Done, Pending, Conflict };
    Progress propagate(const Goal& goal, Bindings& bindings);
    Progress linear(const std::vector<Goal>& goals, Bindings& bindings);
    void search(Context context, const Request& request, std::size_t depth);
    void finish(const Context& context, const Request& request);
public:
    explicit Machine(Program& program, Options options = {});
    Result solve(const Request& request);
};

// Target and activation adapters populate the same relation machine. No type
// kind or reentrancy flag is special-cased by its search implementation.
void compactTypes(Program& program);
struct Lifetime { std::string name; Id begin = 0, end = 0; Ref size, offset; };
class ActivationGraph {
    std::vector<std::string> events;
    std::set<std::pair<Id, Id>> order;
public:
    enum class Overlap { Excluded, Admitted, Unknown };
    bool closed = true;
    std::vector<Lifetime> storage;
    Id event(std::string name);
    void before(Id first, Id second);
    void live(std::string name, Id begin, Id end, Ref size, Ref offset);
    bool precedes(Id first, Id second) const;
    bool valid() const;
    Overlap overlap(const Lifetime& first, const Lifetime& second) const;
    void constrain(Request& request) const;
};
struct Demonstration {
    Request request;
    ActivationGraph activation;
    Ref element, count, width, bytes, firstOffset, secondOffset, extent;
};
Demonstration demonstration(Program& program, const std::string& mode, unsigned budget);
} // namespace vx::relational
