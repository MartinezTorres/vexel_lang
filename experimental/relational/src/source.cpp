#include "source.hpp"
#include "source_numeric.hpp"
#include "source_observation.hpp"
#include "source_control_proof.hpp"
#include "source_bank_chunks.hpp"
#include "floating.hpp"
#include "vexel.hpp"
#include "process.hpp"
#include <algorithm>
#include <deque>
#include <iomanip>
#include <sstream>

namespace vx::relational {
void sourceRules(Program&,const std::set<unsigned>&);
namespace {
enum class Facet { Type, Value, Length, Element, Schema, Target, Execution, Storage, Identity, Category, Shape };
using Property=std::pair<Id,Facet>;
struct PropertyScope {
    std::optional<Property>& current;
    std::optional<Property> previous;
    PropertyScope(std::optional<Property>& slot,Property key):current(slot),previous(slot){current=key;}
    ~PropertyScope(){current=previous;}
};
struct SupportScope {
    std::vector<Ref>*& current;
    std::vector<Ref>* previous;
    SupportScope(std::vector<Ref>*& slot,std::vector<Ref>* next):current(slot),previous(slot){current=next;}
    ~SupportScope(){current=previous;}
};
struct Definition { Id site=none, scope=none; };
// Membership is published once per lexical scope and input revision. Only
// values and type relationships are solver promises; discovery order cannot
// extend an already observed overload set.
struct DeclarationSet {
    std::map<std::string,Definition> records;
    std::map<std::string,std::vector<Definition>> functions;
};
struct Environment {
    Id parent=none;
    // Property queries retain the immutable execution snapshot that supplied
    // their reaching values, including control and initialization premises.
    Id queryState=0;
    std::string modulePath;
    std::map<std::string,Id> values;
    std::shared_ptr<const DeclarationSet> declarations;
    std::vector<Id> calls;
    std::vector<std::string> evaluations;
    bool puzzle=false;
    std::set<Id> parameters;
};
struct Cell {
    enum Kind { SyntaxCell, Alias, Bound, Record, Input, Known, Dispatch, Invocation, Store, Arithmetic, Math, Thunk, Projection, Element, Receiver, SchemaSymbol, LoopSlot, CapturePrototype, Missing } kind=SyntaxCell;
    Id site=none, scope=none, target=none, annotation=none, recordScope=none;
    Definition definition;
    bool thunkActual=false, readonly=false, expanded=false, expanding=false, field=false, flowPure=false, flowWrapper=false;
    std::string problem, inputName;
    Ref knownType,knownValue;
    std::map<Facet,Ref> knownProperties;
    Id location=none,previous=none,returnTarget=none;
    Id iterationTarget=none,useScope=none;
    bool sortedIteration=false,inputRange=false;
    bool temporaryReceiver=false;
    bool abstractNative=false;
    bool syntheticCall=false,builtin=false,allowBuiltin=false;
    Diagnostic absence=Diagnostic::Invalid;
    Id absenceSite=none;
    std::vector<Ref> receiverPath;
    std::vector<Id> loopTargets;
    Ref inputMin,inputMax;
    std::vector<Id> items, prefix, parameters, locals;
    std::vector<std::string> names;
    std::map<Facet,Ref> properties;
};
struct Query { std::string name; Ref answer; bool type=false; };
struct Output {
    std::string name,source;
    Id cell=none;
    std::vector<Id> parameters;
    Ref type,value,bits,bytes,allocationBytes,offset,preference,types,values,paths;
};
struct Flow {Id value=none;Ref state,code,effect={},runtime={};};
struct FlowExit {std::string kind;Ref condition;Id value=none,target=none;};
struct CapturePlan {std::vector<Id> slots;std::vector<bool> inputs,outputs;bool conservative=false;};
struct RuntimeInterface {
    std::vector<Ref> types;
    std::vector<std::vector<Ref>> arguments;
    std::map<Id,Ref> captures;
};
// Sufficient initialization proofs are not executable computations: their
// Boolean normal forms may reorder source operations with guarded evaluation.
struct InitializationFacts {
    Ref proof,runtime;
    InitializationFacts()=default;
    InitializationFacts(Ref value):proof(value),runtime(std::move(value)) {}
    InitializationFacts(Ref evidence,Ref physical):proof(std::move(evidence)),runtime(std::move(physical)) {}
};
struct FlowState {
    bool schema=false;
    bool controlShadow=false;
    // Scoped purity obligations protect logical locations, including aliases.
    // Callee scratch locations and private receiver copies remain writable.
    std::set<Id> immutableRoots;
    std::map<Id,Id> values;
    // A location may have a supplied type without an initialized value.
    std::map<Id,Ref> locationTypes;
    std::vector<std::pair<Id,std::string>> calls;
    Id frame=none,returnTarget=none,initializationLoop=none;
    std::vector<Id> loopTargets;
    Ref live;
    std::vector<FlowExit> exits;
    std::map<Id,InitializationFacts> initialized;
    std::vector<Ref> conditions;
    std::set<std::pair<Id,Id>> captureProofs;
    // Runtime recursion reuses interface promises; it never enumerates
    // omitted/read/write capture combinations to manufacture an interface.
    std::map<std::pair<Id,Id>,RuntimeInterface> runtimeInterfaces;
    Id captureProofIdentity=none;
};
struct NativeReceiver {Id location=none;std::vector<Ref> path;};
struct NativeControlCall {
    Definition definition;
    std::vector<Ref> types,values;
    std::vector<Id> captures;
    std::vector<Ref> captureTypes,captureValues;
    std::vector<bool> captureInputs,captureOutputs;
    std::map<std::size_t,NativeReceiver> receivers;
    Id site=none,scope=none,before=none;
};
struct Frame {Id id=none,firstAutomatic=none;Ref resultType,resultPattern,capturePatterns,effect;std::vector<Id> captures;std::vector<bool> captureInputs,captureOutputs;std::string bundle;};
struct Leaf {Ref type,value;std::string path;};
struct LoopSlotInfo {Id proxy=none,initial=none;bool existing=false;};
struct LoopInitialization {
    std::map<Id,LoopSlotInfo> slots;
    std::vector<std::pair<Id,Ref>> reads;
};
bool aggregate(const Ref& type) {return type->name=="Array" || type->name=="Record";}
bool tupleType(const Ref& type) {return type->name=="Record" && type->arguments[0]->name=="Tuple";}
Ref tupleType(Terms& terms,const std::vector<Ref>& items) {
    std::vector<Ref> fields;
    for(std::size_t i=0;i<items.size();++i) fields.push_back(terms.make("__"+std::to_string(i),{items[i]}));
    return terms.make("Record",{terms.make("Tuple",{terms.number(items.size())}),terms.make("Fields",fields),terms.make("Captures")});
}
std::size_t arrayCount(const Ref& type) {
    if(type->name!="Array" || type->arguments.size()!=2 || type->arguments[1]->kind!=Term::Number ||
       type->arguments[1]->number<0 || type->arguments[1]->number>10000)
        throw ConstructionLimit("aggregate shape exceeds construction budget");
    return type->arguments[1]->number.convert_to<std::size_t>();
}
// Structural traversals construct interfaces; they do not evaluate source arithmetic.
std::vector<Leaf> leaves(const Ref& type,const Ref& value={},const std::string& path={}) {
    std::vector<Leaf> result;
    if(type->name=="NoValue") return result;
    if(type->name=="Array") {
        auto count=arrayCount(type);
        if(value && (value->name!="Values" || value->arguments.size()!=count)) throw std::logic_error("array interface/value mismatch");
        for(std::size_t i=0;i<count;++i) {
            auto part=leaves(type->arguments[0],value ? value->arguments[i] : Ref{},path+"["+std::to_string(i)+"]");
            result.insert(result.end(),part.begin(),part.end());
        }
    } else if(type->name=="Record") {
        const auto& fields=type->arguments[1]->arguments;
        if(value && (value->name!="RecordValue" || value->arguments[1]->arguments.size()!=fields.size())) throw std::logic_error("record interface/value mismatch");
        for(std::size_t i=0;i<fields.size();++i) {
            auto part=leaves(fields[i]->arguments[0],value ? value->arguments[1]->arguments[i] : Ref{},path+"."+fields[i]->name);
            result.insert(result.end(),part.begin(),part.end());
        }
    } else result.push_back({type,value,path});
    if(result.size()>10000) throw ConstructionLimit("aggregate interface exceeds construction budget");
    return result;
}
bool runtimeValue(const Ref& value) {
    if(value->name=="Runtime") return true;
    return std::any_of(value->arguments.begin(),value->arguments.end(),runtimeValue);
}
std::size_t nativeLeafCount(const Ref& type) {
    auto flat=leaves(type);
    return static_cast<std::size_t>(std::count_if(flat.begin(),flat.end(),[](const Leaf& leaf){return nativeScalar(leaf.type);}));
}
unsigned nativeBytes(const Ref& scalar) {
    if(!nativeScalar(scalar)) throw std::logic_error("native storage requires a concrete scalar of at most 32 bits");
    unsigned bits=scalarBits(scalar);
    return bits<=8 ? 1u : bits<=16 ? 2u : 4u;
}
std::optional<unsigned> unsignedWidth(const std::string& name) {
    if(name.size()<2 || name[0]!='u' || name.find_first_not_of("0123456789",1)!=std::string::npos) return {};
    unsigned width=0;
    for(std::size_t i=1;i<name.size();++i) {
        width=width*10+static_cast<unsigned>(name[i]-'0');
        if(width>65536) return {};
    }
    if(width==0) return {};
    return width;
}
std::optional<unsigned> signedWidth(const std::string& name) {
    return name.starts_with("i") ? unsignedWidth("u"+name.substr(1)) : std::optional<unsigned>{};
}
std::string typeText(const Ref& type) {
    if(type->kind!=Term::Constructor) return show(type);
    if(type->name=="UInt") return "#u"+show(type->arguments.at(0));
    if(type->name=="SInt") return "#i"+show(type->arguments.at(0));
    if(type->name=="Float") return "#f"+show(type->arguments.at(0));
    if(type->name=="Fixed") return std::string(scalarSigned(type) ? "#i" : "#u")+Integer(type->arguments[0]->number-type->arguments[2]->number).str()+"."+show(type->arguments[2]);
    if(type->name=="Exact") return "#integer";
    if(type->name=="Bool") return "#b";
    if(type->name=="String") return "#s";
    if(type->name=="Array") return typeText(type->arguments.at(0))+"["+show(type->arguments.at(1))+"]";
    if(type->name=="Record") {
        if(tupleType(type)) {
            std::string out="(";
            for(const auto& field:type->arguments[1]->arguments) {if(out.size()>1) out+=",";out+=typeText(field->arguments[0]);}
            return out+")";
        }
        const auto& name=type->arguments.at(0)->arguments.at(0)->name;
        std::string out="#"+name+"(";
        const auto& fields=type->arguments.at(1)->arguments;
        for(std::size_t i=0;i<fields.size();++i) {
            if(i) out+=",";
            out+=fields[i]->name+":"+typeText(fields[i]->arguments.at(0));
        }
        out+=")";
        if(!type->arguments.at(2)->arguments.empty()) out+="{"+show(type->arguments.at(2))+"}";
        return out;
    }
    return show(type);
}

// This adapter constructs graph equations. It never inspects a solver binding
// to decide a source type, selects a physical type, or evaluates arithmetic.
// A memoized property is a promise, not an answer. Cycles become ordinary
// simultaneous obligations in Request; Machine is the only answer engine.
class Session {
    Graph& graph;
    SourceOptions options;
    std::map<std::string,Ref> configuration;
    std::map<std::string,std::vector<std::string>> configurationOrigins;
    std::map<std::string,Ref> streamCapacities;
    std::map<Id,Ref> mapperSizes;
    std::map<Id,unsigned> mapperOffsets;
    Ref mapperExtent;
    Program program;
    Request request;
    std::vector<std::string> observableArtifacts;
    std::deque<Environment> environments;
    std::deque<Cell> cells;
    std::map<std::pair<Id,Id>,Id> expressions;
    std::map<std::string,Id> roots;
    std::map<std::string,Id> invocations;
    std::map<std::pair<Id,Id>,Id> selectedCalls;
    std::map<std::string,Id> captureScopes;
    std::map<std::string,Id> moduleBindings,moduleScopes;
    std::map<std::pair<Id,Id>,Id> recordPrototypes;
    std::set<unsigned> widths{8,16,32};
    std::vector<Ref> numericFormats;
    std::vector<Query> queries;
    std::vector<Output> outputs;
    std::vector<std::string> boundaryEventNames;
    std::set<std::pair<Id,Id>> boundaryEventOrder;
    bool boundaryClosed=false;
    bool executionExclusive=false;
    std::set<std::pair<std::string,std::string>> overlappingExecutions;
    std::optional<Property> activeProperty;
    std::map<Property,std::set<Property>> dependencies;
    std::map<Property,Ref> supports;
    std::vector<Ref>* supportInputs=nullptr;
    std::vector<Ref>* effectInputs=nullptr;
    std::vector<Ref>* runtimeInputs=nullptr;
    std::map<Id,Flow> initializers;
    unsigned nesting=0;
    std::map<Id,Ref> functionTypes;
    std::vector<std::string> captureProofReports;
    std::deque<FlowState> states{FlowState{}};
    std::map<Id,LoopInitialization> loopInitializations;
    std::map<std::pair<Id,Id>,Flow> flows;
    std::map<std::string,Frame> frames;
    std::map<Id,Ref> frameCodes;
    std::map<Id,NativeControlCall> nativeControlCalls;
    std::map<Id,std::vector<Ref>> nativeControlResults;
    std::set<Id> preparedAliasGroups;
    std::map<Id,std::string> aliasBundles;
    std::map<Id,std::vector<Id>> aliasComponents;
    std::set<Id> openAliasComponents;
    std::map<Id,Ref> aliasReadiness;
    std::map<Id,std::vector<Id>> equationGroups;
    std::map<std::pair<Id,std::vector<std::string>>,std::string> equationBundles;
    std::map<std::pair<Id,std::vector<std::string>>,Id> projections;
    std::map<Id,Id> operatorCalls;
    std::map<std::string,Definition> schemaDefinitions;
    std::map<Id,Ref> recordValidations;
    std::shared_ptr<SearchBudget> searchBudget;
    std::size_t startingWork=0,startingDecisions=0;
    Id nextFrame=0,nextTemporary=0,nextLoop=0,nextCaptureProof=0;
    Ref extent;
    Terms& terms() {return program.terms;}
    Ref number(Integer n) {return terms().number(std::move(n));}
    Ref fresh(std::string name) {return terms().variable(std::move(name));}
    void closeEffects(const std::vector<Ref>& inputs,Ref result) {
        Ref joined=number(0);
        for(const auto& input:inputs) {auto next=fresh("joined effect");request.goals.push_back(booleanOr(joined,input,next));joined=next;}
        equation(result,joined);
    }
    void boundaryEffect() {if(effectInputs) effectInputs->push_back(number(1));}
    void runtimeCode() {if(runtimeInputs) runtimeInputs->push_back(number(1));}
    void captureUse(Id slot,Id at,bool written=false) {
        if(states[at].captureProofs.empty()) return;
        request.goals.push_back(witness("capture_use_"+std::to_string(slot),number(slot)));
        if(written) request.goals.push_back(witness("capture_write_"+std::to_string(slot),number(slot)));
    }
    std::vector<Goal> constructed(const std::function<void()>& action) {
        auto outer=std::move(request.goals);request.goals.clear();
        try {action();} catch(...) {request.goals=std::move(outer);throw;}
        auto goals=std::move(request.goals);request.goals=std::move(outer);return goals;
    }
    void later(std::vector<Ref> inputs,const std::function<void(const std::vector<Ref>&)>& action) {
        auto name="source_builder_"+std::to_string(program.builders.size());
        Ref support,effect,runtime;
        if(supportInputs) {support=fresh("delayed dependency support");supportInputs->push_back(support);}
        if(effectInputs) {effect=fresh("delayed effect");effectInputs->push_back(effect);}
        if(runtimeInputs) {runtime=fresh("delayed runtime requirement");runtimeInputs->push_back(runtime);}
        auto cache=std::make_shared<std::map<std::string,std::vector<Goal>>>();
        program.builders.emplace(name,Program::Builder{inputs.size(),inputs.size(),[this,action,cache,support,effect,runtime](const auto& args){
            std::string key;for(const auto& arg:args) key+=show(arg)+"\n";
            if(auto found=cache->find(key);found!=cache->end()) return found->second;
            auto goals=constructed([&]{
                std::vector<Ref> edges;SupportScope collecting(supportInputs,&edges);
                std::vector<Ref> effects;SupportScope collectingEffects(effectInputs,&effects);
                std::vector<Ref> runtimes;SupportScope collectingRuntime(runtimeInputs,&runtimes);
                action(args);if(support) equation(support,terms().make("Edges",edges));
                if(effect) closeEffects(effects,effect);
                if(runtime) closeEffects(runtimes,runtime);
            });cache->emplace(key,goals);return goals;
        }});
        request.goals.push_back(build(name,std::move(inputs)));
    }
    void equation(Ref a,Ref b) {request.goals.push_back(equal(std::move(a),std::move(b)));}
    void pending(const std::string& reason,Id site=none) {
        request.goals.push_back(unknown((site==none ? "" : graph.location(graph.nodes[site].span)+": ")+reason));
    }
    void conflict() {equation(number(0),number(1));}
    Id environment(Id parent) {Environment env;env.parent=parent;if(parent!=none) {env.queryState=environments[parent].queryState;env.puzzle=environments[parent].puzzle;env.calls=environments[parent].calls;env.evaluations=environments[parent].evaluations;env.modulePath=environments[parent].modulePath;}environments.push_back(std::move(env));return environments.size()-1;}
    Id addCell(Cell c) {
        if(cells.size()>=options.maxNodes) throw ConstructionLimit("source relationship budget exhausted");
        cells.push_back(std::move(c));return cells.size()-1;
    }
    Id expression(Id site,Id scope) {
        auto key=std::make_pair(site,scope);
        if(auto found=expressions.find(key);found!=expressions.end()) return found->second;
        Cell c;c.site=site;c.scope=scope;Id id=addCell(std::move(c));expressions.emplace(key,id);return id;
    }
    Id bound(Id site,Id scope,Id annotation,Id initial,bool field=false) {
        Cell c;c.kind=Cell::Bound;c.site=site;c.scope=scope;c.annotation=annotation;c.target=initial;c.field=field;c.expanded=true;
        return addCell(std::move(c));
    }
    Id lookup(Id scope,const std::string& name) const {
        for(Id env=scope;env!=none;env=environments[env].parent) {
            auto found=environments[env].values.find(name);
            if(found!=environments[env].values.end()) return found->second;
        }
        return none;
    }
    Definition record(Id scope,const std::string& name) const {
        for(Id env=scope;env!=none;env=environments[env].parent) {
            const auto& declarations=environments[env].declarations;
            if(!declarations) continue;
            auto found=declarations->records.find(name);
            if(found!=declarations->records.end()) return found->second;
        }
        return {};
    }
    std::vector<Definition> functions(Id scope,const std::string& name) const {
        std::vector<Definition> result;std::set<std::pair<Id,Id>> seen;
        for(Id env=scope;env!=none;env=environments[env].parent) {
            const auto& declarations=environments[env].declarations;
            if(!declarations) continue;
            auto found=declarations->functions.find(name);
            if(found!=declarations->functions.end()) for(auto def:found->second)
                if(seen.emplace(def.site,def.scope).second) result.push_back(def);
        }
        return result;
    }
    Id queryState(Id scope) const {return scope==none ? 0 : environments[scope].queryState;}
    Id reachingScope(Id lexical,Id caller) {
        std::map<Id,Id> reaching;
        for(Id env=caller;env!=none;env=environments[env].parent)
            for(const auto& [name,value]:environments[env].values) {
                (void)name;Id slot=cells[value].kind==Cell::Store ? cells[value].location : cells[value].readonly && cells[value].previous!=none ? cells[value].previous : value;
                reaching.try_emplace(slot,value);
            }
        std::map<std::string,Id> replacements;std::set<std::string> visible;
        for(Id env=lexical;env!=none;env=environments[env].parent)
            for(const auto& [name,value]:environments[env].values) if(visible.insert(name).second) {
                Id slot=cells[value].kind==Cell::Store ? cells[value].location : cells[value].readonly && cells[value].previous!=none ? cells[value].previous : value;
                if(auto found=reaching.find(slot);found!=reaching.end() && found->second!=value) replacements.emplace(name,found->second);
            }
        Id context=queryState(caller);
        if(replacements.empty() && context==queryState(lexical)) return lexical;
        std::string key=std::to_string(lexical)+":query-state:"+std::to_string(context);
        for(const auto& [name,value]:replacements) key+="|"+name+":"+std::to_string(value);
        if(auto found=captureScopes.find(key);found!=captureScopes.end()) return found->second;
        Id scope=environment(lexical);environments[scope].queryState=context;environments[scope].values=std::move(replacements);captureScopes.emplace(key,scope);return scope;
    }
    void declarations(const std::vector<Id>& sites,Id scope,bool globals) {
        if(environments[scope].declarations) throw std::logic_error("declaration membership already closed");
        auto closed=std::make_shared<DeclarationSet>();
        for(Id site:sites) {
            const auto& n=graph.nodes[site];
            if(n.syntax==Syntax::RecordDecl) {
                if(record(environments[scope].parent,n.text).site!=none) throw Error(n.span,"shadowing or duplicate type: "+n.text);
                if(!closed->records.emplace(n.text,Definition{site,scope}).second)
                    throw Error(n.span,"duplicate record in source adapter: "+n.text);
            } else if(n.syntax==Syntax::Function) {
                if(lookup(scope,n.text)!=none) throw Error(n.span,"shadowing or duplicate symbol: "+n.text);
                auto previous=functions(scope,n.text);
                if(auto found=closed->functions.find(n.text);found!=closed->functions.end())
                    previous.insert(previous.end(),found->second.begin(),found->second.end());
                for(auto other:previous) if(n.external || n.exported || graph.nodes[other.site].external || graph.nodes[other.site].exported)
                    throw Error(n.span,"duplicate ABI function: "+n.text);
                closed->functions[n.text].push_back({site,scope});
            }
            else if(globals && n.syntax==Syntax::Global) {
                if(lookup(scope,n.text)!=none || closed->functions.contains(n.text) || !functions(scope,n.text).empty()) throw Error(n.span,"shadowing or duplicate symbol: "+n.text);
                const auto key=environments[scope].modulePath+":"+std::to_string(site);
                auto found=moduleBindings.find(key);Id value;
                if(found!=moduleBindings.end()) value=found->second;
                else {
                    Id initial=n.children.empty() ? none : expression(n.children[0],scope);
                    value=bound(site,scope,n.annotation,initial);moduleBindings.emplace(key,value);
                }
                environments[scope].values.emplace(n.text,value);
            }
        }
        environments[scope].declarations=std::move(closed);
    }
    Id moduleEnvironment(Id lexical) {
        const auto key=environments[lexical].modulePath;
        if(auto found=moduleScopes.find(key);found!=moduleScopes.end()) return found->second;
        std::map<Id,Id> copied;
        std::function<Id(Id)> copy=[&](Id original)->Id {
            if(original==none || original==0) return original;
            if(auto found=copied.find(original);found!=copied.end()) return found->second;
            Id scope=environment(none);copied.emplace(original,scope);
            environments[scope]=environments[original];environments[scope].parent=copy(environments[original].parent);
            environments[scope].values.clear();environments[scope].parameters.clear();
            for(const auto& [name,value]:environments[original].values) if(global(value)) {
                environments[scope].values.emplace(name,value);
                if(cells[value].scope==original) {
                    cells[value].scope=scope;const auto& node=graph.nodes[cells[value].site];
                    cells[value].target=node.children.empty() ? none : expression(node.children[0],scope);
                }
            }
            if(environments[scope].declarations) {
                auto rebound=std::make_shared<DeclarationSet>(*environments[scope].declarations);
                for(auto& [name,definitions]:rebound->functions) {
                    (void)name;for(auto& def:definitions) def.scope=copy(def.scope);
                }
                for(auto& [name,def]:rebound->records) {(void)name;def.scope=copy(def.scope);}
                environments[scope].declarations=std::move(rebound);
            }
            return scope;
        };
        Id scope=copy(lexical);moduleScopes.emplace(key,scope);return scope;
    }
    Id instantiate(Definition definition,const std::vector<Id>& arguments,Id site,Id callerScope,bool expandBody=false,const std::string& evaluation={},bool stateInterface=false) {
        if(!stateInterface) definition.scope=reachingScope(definition.scope,callerScope);
        const auto& function=graph.nodes[definition.site];
        if(function.children.size()!=arguments.size()) {
            Cell bad;bad.kind=Cell::Missing;bad.site=site;bad.expanded=true;
            bad.problem="no matching function arity: "+function.text;bad.absence=Diagnostic::NoOverload;
            return addCell(std::move(bad));
        }
        if(function.key=="bundled-math") {
            Cell math;math.kind=Cell::Math;math.expanded=true;math.site=site;math.scope=callerScope;
            math.inputName=function.text;math.items=arguments;return addCell(std::move(math));
        }
        if(function.external || (function.noinline && !expandBody)) {
            Cell invocation;invocation.kind=Cell::Invocation;invocation.expanded=true;invocation.site=site;
            invocation.scope=callerScope;invocation.definition=definition;invocation.items=arguments;
            return addCell(std::move(invocation));
        }
        // Recursive source expansion is not guessed to be a completed proof.
        const auto& calls=environments[callerScope].calls;
        if(!expandBody && std::find(calls.begin(),calls.end(),definition.site)!=calls.end()) {
            Cell bad;bad.kind=Cell::Missing;bad.site=site;bad.expanded=true;bad.problem="recursive source specialization remains open";
            return addCell(std::move(bad));
        }
        Id scope=environment(definition.scope);
        environments[scope].modulePath+="/"+graph.identity(definition.site);
        environments[scope].puzzle=environments[callerScope].puzzle || environments[scope].puzzle;
        environments[scope].calls=calls;environments[scope].calls.push_back(definition.site);
        environments[scope].evaluations=environments[callerScope].evaluations;
        if(!evaluation.empty()) environments[scope].evaluations.push_back(evaluation);
        std::vector<Id> parameters;
        for(std::size_t i=0;i<arguments.size();++i) {
            const auto& param=graph.nodes[function.children[i]];
            Id value;
            if(i<function.receiverCount && cells[arguments[i]].kind==Cell::Receiver) {
                value=arguments[i];
            } else if(param.expression) {
                Cell thunk;thunk.kind=Cell::Thunk;thunk.expanded=true;thunk.site=function.children[i];thunk.scope=scope;thunk.target=arguments[i];thunk.annotation=param.annotation;
                value=addCell(std::move(thunk));
            } else {
                value=bound(function.children[i],scope,param.annotation,arguments[i]);
                environments[scope].parameters.insert(value);parameters.push_back(value);
            }
            environments[scope].values.emplace(param.text,value);
        }
        Id body=expression(function.body,scope);
        expand(body);
        if(function.annotation!=none) body=bound(site,scope,function.annotation,body);
        Cell completion;completion.kind=Cell::Alias;completion.expanded=true;completion.site=site;completion.scope=scope;
        completion.flowWrapper=true;
        completion.definition=definition;
        completion.target=body;completion.prefix=std::move(parameters);
        return addCell(std::move(completion));
    }
    Id importAbsence(Id block) const {
        std::set<Id> visited;
        std::function<Id(Id)> visit=[&](Id body)->Id {
            if(!visited.insert(body).second) return none;
            for(Id child:graph.nodes[body].children) if(graph.nodes[child].syntax==Syntax::Import) {
                const auto& node=graph.nodes[child];
                if(node.typeError && node.typeError->category==Diagnostic::ImportResolution) return child;
                if(node.body!=none) if(Id absent=visit(node.body);absent!=none) return absent;
            }
            return none;
        };
        return visit(block);
    }
    void expand(Id id) {
        auto& cell=cells[id];
        if(cell.expanded) return;
        if(cell.expanding) {cell.kind=Cell::Missing;cell.problem="cyclic source structure remains open";return;}
        cell.expanding=true;
        if(++nesting>256) throw ConstructionLimit("source expansion depth exhausted");
        const auto& n=graph.nodes[cell.site];
        auto child=[&](Id site){return expression(site,cell.scope);};
        if(n.syntax==Syntax::Name) {
            cell.target=lookup(cell.scope,n.text);
            cell.kind=cell.target==none ? Cell::Missing : Cell::Alias;
            cell.problem="undefined source name: "+n.text;
            if(cell.target==none) cell.absence=Diagnostic::UndefinedName;
        } else if(n.syntax==Syntax::Group || n.syntax==Syntax::Solve) {
            cell.kind=Cell::Alias;
            if(n.syntax==Syntax::Solve) {Id scope=environment(cell.scope);environments[scope].puzzle=true;cell.target=expression(n.children.at(0),scope);}
            else cell.target=child(n.children.at(0));
        } else if(n.syntax==Syntax::Block) {
            if(Id absent=importAbsence(cell.site);absent!=none) {
                cell.kind=Cell::Missing;cell.absence=Diagnostic::ImportResolution;cell.absenceSite=absent;
                cell.problem=graph.nodes[absent].typeError->what();cell.expanded=true;cell.expanding=false;--nesting;return;
            }
            Id lexical=environment(cell.scope);environments[lexical].modulePath+="/"+graph.identity(cell.site);
            auto members=moduleDeclarations(graph,cell.site);declarations(members,lexical,true);
            std::set<Id> direct(n.children.begin(),n.children.end());
            if(std::any_of(members.begin(),members.end(),[&](Id site){return !direct.contains(site) || graph.nodes[site].syntax==Syntax::Global;})) {
                Id module=moduleEnvironment(lexical);
                auto rebound=std::make_shared<DeclarationSet>(*environments[lexical].declarations);
                for(auto& [name,definitions]:rebound->functions) {
                    (void)name;for(auto& def:definitions) if(!direct.contains(def.site)) def.scope=module;
                }
                for(auto& [name,def]:rebound->records) {(void)name;if(!direct.contains(def.site)) def.scope=module;}
                environments[lexical].declarations=std::move(rebound);
            }
            Id scope=environment(lexical);std::set<Id> owned=environments[cell.scope].parameters;
            cell.kind=Cell::Alias;
            for(Id statement:n.children) {
                const auto& s=graph.nodes[statement];
                if(s.syntax==Syntax::RecordDecl || s.syntax==Syntax::Function || s.syntax==Syntax::Import || s.syntax==Syntax::Global) {cell.target=none;continue;}
                Id value=none;
                if(s.syntax==Syntax::Declare) {
                    Id init=s.children.empty() ? none : expression(s.children[0],scope);
                    value=bound(statement,scope,s.annotation,init);
                    if(lookup(scope,s.text)!=none || !functions(scope,s.text).empty()) throw Error(s.span,"shadowing or duplicate local declaration: "+s.text);
                    environments[lexical].values[s.text]=value;owned.insert(value);cell.locals.push_back(value);
                    if(init!=none) cell.prefix.push_back(value);
                } else if(s.syntax==Syntax::Assign && graph.nodes[s.children[0]].syntax==Syntax::Name) {
                    const auto& name=graph.nodes[s.children[0]].text;
                    Id previous=lookup(scope,name),rhs=expression(s.children[1],scope);
                    if(previous==none && !functions(scope,name).empty()) throw Error(s.span,"assignment collides with function declaration: "+name);
                    if(previous==none && s.text=="=") {
                        value=bound(statement,scope,none,rhs);environments[lexical].values[name]=value;owned.insert(value);cell.locals.push_back(value);
                    } else if(previous!=none && (cells[location(previous)].kind==Cell::Receiver || s.text.starts_with('.') || (s.text!="=" && !functions(scope,"."+s.text.substr(0,s.text.size()-1)).empty()))) {
                        value=expression(statement,scope);
                    } else if(previous!=none && (s.text=="=" || s.text=="+=" || s.text=="-=" || s.text=="*=" || s.text=="/=" || s.text=="%=" || s.text=="&=" || s.text=="|=" || s.text=="^=" || s.text=="<<=" || s.text==">>=")) {
                        Id location=cells[previous].kind==Cell::Store ? cells[previous].location : previous;
                        if(s.text!="=") {
                            Cell operation;operation.kind=Cell::Arithmetic;operation.expanded=true;operation.site=statement;
                            operation.inputName=s.text.substr(0,s.text.size()-1);operation.items={previous,rhs};rhs=addCell(std::move(operation));
                        }
                        Cell store;store.kind=Cell::Store;store.expanded=true;store.site=statement;store.scope=scope;
                        store.location=location;store.previous=previous;store.target=rhs;
                        if(!owned.contains(location)) store.problem="write to enclosing storage needs an explicit state interface";
                        value=addCell(std::move(store));
                        // Earlier loads retain their scope and reaching store.
                        // Every update creates a new state version.
                        scope=environment(scope);environments[scope].values[name]=value;
                    } else if(previous!=none && (s.text=="&&=" || s.text=="||=")) {
                        value=expression(statement,scope);
                    } else {
                        Cell bad;bad.kind=Cell::Missing;bad.expanded=true;bad.site=statement;
                        bad.problem="unsupported local update";value=addCell(std::move(bad));
                    }
                    cell.prefix.push_back(value);
                } else if(s.syntax==Syntax::Destructure) {
                    Id rhs=expression(s.children.back(),scope);
                    Cell assignment;assignment.site=statement;assignment.scope=scope;assignment.expanded=true;
                    assignment.items.push_back(rhs);
                    for(std::size_t i=0;i+1<s.children.size();++i) {
                        const auto& name=graph.nodes[s.children[i]].text;Id previous=lookup(scope,name);
                        Id field=projection(rhs,{"__"+std::to_string(i)}),destination;
                        if(previous==none) {
                            if(!functions(scope,name).empty()) throw Error(s.span,"assignment collides with function declaration: "+name);
                            destination=bound(s.children[i],scope,none,field);
                            environments[lexical].values[name]=destination;owned.insert(destination);cell.locals.push_back(destination);
                        } else {
                            Cell store;store.kind=Cell::Store;store.expanded=true;store.site=s.children[i];store.scope=scope;
                            store.location=location(previous);store.previous=previous;store.target=field;
                            destination=addCell(std::move(store));
                            scope=environment(scope);environments[scope].values[name]=destination;
                        }
                        assignment.items.push_back(destination);
                    }
                    value=addCell(std::move(assignment));cell.prefix.push_back(value);
                } else {value=expression(statement,scope);cell.prefix.push_back(value);}
                cell.target=s.terminated || s.syntax==Syntax::Declare || s.syntax==Syntax::Assign || s.syntax==Syntax::Destructure ? none : value;
            }
            if(cell.target==none) {cell.kind=Cell::Missing;cell.problem="block has no result in relational source slice";}
        } else if(n.syntax==Syntax::Call || n.syntax==Syntax::MethodProbe) {
            auto definitions=functions(cell.scope,n.text);
            for(Id argument:n.children) cell.prefix.push_back(child(argument));
            if(n.syntax==Syntax::MethodProbe || definitions.size()>1 || (n.receiverCount && !definitions.empty())) {cell.kind=Cell::Dispatch;cell.items=cell.prefix;}
            else if(definitions.empty()) {cell.kind=Cell::Missing;cell.problem="missing call remains open: "+n.text;cell.absence=Diagnostic::UndefinedFunction;}
            else {cell.kind=Cell::Alias;cell.target=instantiate(definitions[0],cell.prefix,cell.site,cell.scope);}
        } else if(n.syntax==Syntax::Construct) {
            cell.definition=record(cell.scope,n.text);
            if(cell.definition.site!=none) cell.definition.scope=reachingScope(cell.definition.scope,cell.scope);
            if(cell.definition.site==none) {
                cell.kind=Cell::Missing;cell.problem="undefined record: "+n.text;cell.absence=Diagnostic::UndefinedType;
                for(Id argument:n.children) cell.prefix.push_back(child(argument));
            }
            else {
                const auto& schema=graph.nodes[cell.definition.site];
                if(schema.children.size()!=n.children.size()) {cell.kind=Cell::Missing;cell.problem="record argument arity mismatch: "+n.text;}
                else {
                    cell.kind=Cell::Record;cell.recordScope=environment(cell.definition.scope);
                    for(std::size_t i=0;i<n.children.size();++i) {
                        const auto& field=graph.nodes[schema.children[i]];
                        Id value=bound(schema.children[i],cell.recordScope,field.annotation,child(n.children[i]),true);
                        environments[cell.recordScope].values[field.text]=value;
                        cell.items.push_back(value);cell.names.push_back(field.text);
                    }
                }
            }
        } else if(n.syntax==Syntax::Member) {
            cell.kind=Cell::Projection;cell.target=child(n.children[0]);cell.names={n.text};
        } else {
            for(Id site:n.children) cell.items.push_back(child(site));
        }
        cell.expanded=true;cell.expanding=false;--nesting;
    }
    Id peel(Id id) {
        std::set<Id> seen;
        while(seen.insert(id).second) {
            expand(id);const auto& c=cells[id];
            if((c.kind==Cell::Alias || c.kind==Cell::Bound) && c.target!=none) id=c.target;
            else return id;
        }
        return id;
    }
    Ref nominal(const Cell& c) {
        const auto& n=graph.nodes[c.definition.site];
        return terms().make("Nominal",{terms().make(n.text),terms().make(n.identity)});
    }
    Id recordPrototype(Definition definition,Id site) {
        auto key=std::make_pair(definition.site,definition.scope);
        if(auto found=recordPrototypes.find(key);found!=recordPrototypes.end()) return found->second;
        Cell recordCell;recordCell.kind=Cell::Record;recordCell.expanded=true;recordCell.site=site;
        recordCell.definition=definition;recordCell.recordScope=environment(definition.scope);
        Id prototype=addCell(std::move(recordCell));recordPrototypes.emplace(key,prototype);
        for(Id field:graph.nodes[definition.site].children) {
            const auto& f=graph.nodes[field];Id value=bound(field,cells[prototype].recordScope,f.annotation,none,true);
            environments[cells[prototype].recordScope].values[f.text]=value;
            cells[prototype].items.push_back(value);cells[prototype].names.push_back(f.text);
        }
        return prototype;
    }
    Ref annotationBase(Id site,Id scope) {
        if(site==none) return fresh("unannotated");
        const auto& n=graph.nodes[site];Ref result;
        if(n.body!=none) result=demand(expression(n.body,scope),Facet::Type);
        else if(n.text=="(tuple)") {
            std::vector<Ref> fields;for(Id field:n.children) fields.push_back(annotation(field,scope));
            result=tupleType(terms(),fields);
        }
        else if(auto bits=unsignedWidth(n.text)) result=terms().make("UInt",{number(*bits)});
        else if(auto signedBits=signedWidth(n.text)) result=terms().make("SInt",{number(*signedBits)});
        else if(auto numeric=extraNumericType(terms(),n.text)) result=numeric;
        else if(n.text=="b") result=terms().make("Bool");
        else if(n.text=="s") result=terms().make("String");
        else if(auto definition=record(scope,n.text);definition.site!=none) {
            result=demand(recordPrototype(definition,site),Facet::Type);
        }
        else {
            if(n.text.size()>1 && (n.text[0]=='u' || n.text[0]=='i') && n.text.find_first_not_of("0123456789",1)==std::string::npos) {
                if(n.text.find_first_not_of('0',1)!=std::string::npos) throw ConstructionLimit("integer width exceeds construction budget");
                conflict();
            } else if(n.text.size()>1 && (n.text[0]=='u' || n.text[0]=='i') && n.text.find('.')!=std::string::npos && n.text.find_first_not_of("0123456789.",1)==std::string::npos) conflict();
            else pending("unsupported or uninstantiated type annotation: #"+n.text,site);
            result=fresh("open type");
        }
        return result;
    }
    Ref annotation(Id site,Id scope) {
        if(site==none) return fresh("unannotated");
        auto result=annotationBase(site,scope);
        if(graph.nodes[site].text=="(tuple)") return result;
        for(Id dimension:graph.nodes[site].children) {
            Ref length=demand(expression(dimension,scope),Facet::Value);
            request.goals.push_back(lessEqual(number(0),length));
            result=terms().make("Array",{result,length});
        }
        return result;
    }
    Ref annotatedElement(Id site,Id scope) {
        const auto& n=graph.nodes[site];
        if(n.children.empty()) {pending("element requested from non-array annotation",site);return fresh("element");}
        Ref result=annotationBase(site,scope);
        for(std::size_t i=0;i+1<n.children.size();++i) {
            auto length=demand(expression(n.children[i],scope),Facet::Value);
            request.goals.push_back(lessEqual(number(0),length));result=terms().make("Array",{result,length});
        }
        return result;
    }
    Ref appliedAnnotation(Id site,Id scope,Ref supplied) {
        const auto& spec=graph.nodes[site];
        if(spec.text=="(tuple)") {
            std::vector<Ref> fields,targets;
            for(Id child:spec.children) {
                auto field=fresh("supplied tuple field");fields.push_back(field);
                targets.push_back(appliedAnnotation(child,scope,field));
            }
            equation(supplied,tupleType(terms(),fields));return tupleType(terms(),targets);
        }
        Ref base=supplied;std::vector<Ref> lengths;
        for(auto dimension=spec.children.rbegin();dimension!=spec.children.rend();++dimension) {
            auto element=fresh("supplied array element"),length=fresh("supplied extent");
            equation(base,terms().make("Array",{element,length}));base=element;
            equation(length,demand(expression(*dimension,scope),Facet::Value));request.goals.push_back(lessEqual(number(0),length));
            lengths.push_back(length);
        }
        Ref target;
        if(spec.body==none && record(scope,spec.text).site!=none) {
            target=fresh("applied nominal interface");
            later({base,terms().make("Dimensions",lengths)},[this,site,scope,target](const auto& facts){
                bool empty=std::any_of(facts[1]->arguments.begin(),facts[1]->arguments.end(),[](const Ref& length){return length->kind==Term::Number && length->number==0;});
                // An untyped empty literal supplies shape, but no record
                // instance from which to obtain dependent field interfaces.
                if(empty && facts[0]->name=="Exact") {equation(target,annotationBase(site,scope));return;}
                Cell expected;expected.definition=record(scope,graph.nodes[site].text);
                equation(facts[0],terms().make("Record",{nominal(expected),fresh("supplied record fields"),fresh("supplied record captures")}));
                equation(target,facts[0]);
            });
        } else target=annotationBase(site,scope);
        for(auto length=lengths.rbegin();length!=lengths.rend();++length) target=terms().make("Array",{target,*length});
        return target;
    }
    Ref recordType(Id id) {
        auto& c=cells[id];std::vector<Ref> fields,schemaEdges;Ref checks=terms().make("Done");
        schemaDefinitions.try_emplace(show(nominal(c)),c.definition);
        Id schemaScope=environment(c.definition.scope);std::map<Id,std::string> lexical;std::set<std::string> names;std::set<Id> owned;
        for(Id env=c.definition.scope;env!=none;env=environments[env].parent)
            for(const auto& [name,value]:environments[env].values) if(names.insert(name).second) {
                Id proxy=schemaAlias(value,name,c.definition.scope);environments[schemaScope].values[name]=proxy;lexical.emplace(proxy,name);
            }
        for(std::size_t i=0;i<c.items.size();++i) {
            Id proxy=schemaAlias(c.items[i],c.names[i],c.recordScope);environments[schemaScope].values[c.names[i]]=proxy;owned.insert(proxy);
        }
        Id symbolicScope=environment(schemaScope);
        for(std::size_t i=0;i<c.items.size();++i) {
            Cell symbol;symbol.kind=Cell::SchemaSymbol;symbol.expanded=true;symbol.flowPure=true;symbol.readonly=true;
            symbol.target=c.items[i];symbol.site=cells[c.items[i]].site;symbol.scope=symbolicScope;symbol.inputName=c.names[i];
            Id proxy=addCell(std::move(symbol));environments[symbolicScope].values[c.names[i]]=proxy;owned.insert(proxy);
        }
        for(std::size_t i=0;i<c.items.size();++i) {
            Id field=c.items[i];const auto& f=cells[field];
            Ref type=demand(field,Facet::Type);
            fields.push_back(terms().make(c.names[i],{type}));
            if(f.annotation!=none) {
                PropertyScope schema(activeProperty,{id,Facet::Schema});
                const auto& spec=graph.nodes[f.annotation];
                if(spec.body!=none || !spec.children.empty() || record(c.recordScope,spec.text).site==none) {
                    // Capture closure covers every field valuation that a later
                    // store could expose. Checks execute with the actual value.
                    {SupportScope detached(supportInputs,nullptr);checks=sequence(checks,recordRequirement(type,f.annotation,schemaScope));}
                    SupportScope collecting(supportInputs,&schemaEdges);
                    (void)recordRequirement(type,f.annotation,schemaScope,symbolicScope);
                    if(spec.body!=none && schemaBranches(spec.body,schemaScope)) captureSchemaType(expression(spec.body,symbolicScope),{});
                }
            }
        }
        // The support term includes delayed construction. Its closure is
        // candidate-local, so a selected overload/branch can reveal captures
        // without publishing an incomplete schema or inspecting bindings.
        auto captures=fresh("closed schema captures");
        owned.insert(c.items.begin(),c.items.end());
        captureSupport(terms().make("Edges",schemaEdges),lexical,owned,{},captures);
        later({captures},[this](const auto& args){if(runtimeValue(args[0])) conflict();});
        recordValidations[id]=checks;
        return terms().make("Record",{nominal(c),terms().make("Fields",fields),captures});
    }
    void conversion(Id initial,Ref targetType,Ref input,Ref result) {
        Id base=peel(initial);auto& c=cells[base];
        if(c.kind==Cell::Known) {
            later({demand(initial,Facet::Type)},[this,targetType,input,result](const auto& types){
                auto sourceType=types[0];
                if(sourceType->name=="Record") {
                    std::vector<Ref> fields,sourceValues,values;
                    for(const auto& field:sourceType->arguments[1]->arguments) {
                        auto type=fresh("converted record field type"),item=fresh("record conversion input"),value=fresh("converted record field");
                        fields.push_back(terms().make(field->name,{type}));sourceValues.push_back(item);values.push_back(value);
                        conversion(known(field->arguments[0],item),type,item,value);
                    }
                    equation(targetType,terms().make("Record",{sourceType->arguments[0],terms().make("Fields",fields),sourceType->arguments[2]}));
                    equation(input,terms().make("RecordValue",{sourceType->arguments[0],terms().make("Values",sourceValues)}));
                    equation(result,terms().make("RecordValue",{sourceType->arguments[0],terms().make("Values",values)}));return;
                }
                if(sourceType->name!="Array") {request.goals.push_back(call("implicit",{sourceType,targetType,input,result}));return;}
                auto element=fresh("converted element");equation(targetType,terms().make("Array",{element,sourceType->arguments[1]}));
                std::vector<Ref> sourceValues,values;
                for(std::size_t i=0;i<arrayCount(sourceType);++i) {
                    auto item=fresh("array conversion input"),value=fresh("converted array item");sourceValues.push_back(item);
                    conversion(known(sourceType->arguments[0],item),element,item,value);values.push_back(value);
                }
                equation(input,terms().make("Values",sourceValues));equation(result,terms().make("Values",values));
            });return;
        }
        if(c.kind==Cell::Record || (c.kind==Cell::SyntaxCell && graph.nodes[c.site].syntax==Syntax::Tuple)) {conversion(known(demand(initial,Facet::Type),input),targetType,input,result);return;}
        if(c.kind==Cell::SyntaxCell && graph.nodes[c.site].syntax==Syntax::Array) {
            auto element=fresh("converted element"),length=demand(initial,Facet::Length);
            equation(targetType,terms().make("Array",{element,length}));
            std::vector<Ref> converted;
            for(Id child:c.items) {
                Ref value=fresh("converted item");conversion(child,element,demand(child,Facet::Value),value);converted.push_back(value);
            }
            equation(result,terms().make("Values",converted));return;
        }
        // A closed logical interface, including a boundary value, has the
        // same structural conversion rules as an already-specialized value.
        // The scalar relation alone cannot copy an aggregate.
        conversion(known(demand(initial,Facet::Type),input),targetType,input,result);
    }
    void dispatch(Id id,Facet facet,Ref result) {
        bool probe=graph.nodes[cells[id].site].syntax==Syntax::MethodProbe;
        if(probe && facet!=Facet::Value) {
            if(facet==Facet::Type) equation(result,terms().make("Bool"));
            else if(facet==Facet::Category) equation(result,terms().make("Scalar"));
            else conflict();
            return;
        }
        later({},[this,id,facet,result,probe](const auto&) {
            PropertyScope property(activeProperty,{id,facet});
            const auto& cell=cells[id];auto definitions=functions(cell.scope,callName(id));
            std::vector<Ref> scores;
            for(const auto& definition:definitions) {
                const auto& fn=graph.nodes[definition.site];
                if(fn.receiverCount!=callReceivers(id) || fn.children.size()!=cell.items.size()) {scores.push_back(number(-1));continue;}
                auto score=fresh("candidate specificity");
                scoreArguments(definition,cell.items,0,environment(definition.scope),number(0),score,probe);
                scores.push_back(score);
            }
            later(scores,[this,id,facet,result,definitions,probe](const auto& resolved) {
                PropertyScope selectedProperty(activeProperty,{id,facet});
                Integer best=-1;std::vector<std::size_t> winners;
                for(std::size_t i=0;i<resolved.size();++i) {
                    if(resolved[i]->kind!=Term::Number) throw std::logic_error("non-numeric specificity");
                    if(resolved[i]->number>best) {best=resolved[i]->number;winners.clear();}
                    if(best>=0 && resolved[i]->number==best) winners.push_back(i);
                }
                if(winners.empty()) {if(probe) equation(result,number(0));else if(cells[id].allowBuiltin && facet==Facet::Target) equation(result,terms().make("Builtin"));else conflict();return;}
                if(winners.size()!=1) {request.goals.push_back(ambiguity("equally specific overloads: "+callName(id)));return;}
                if(probe) {equation(result,number(1));return;}
                const auto& definition=definitions[winners[0]];
                if(facet==Facet::Target) {equation(result,terms().make("Target",{number(definition.site),number(definition.scope)}));return;}
                auto key=std::make_pair(id,definition.site);Id target;
                if(auto found=selectedCalls.find(key);found!=selectedCalls.end()) target=found->second;
                else {target=instantiate(definition,cells[id].items,cells[id].site,cells[id].scope);selectedCalls.emplace(key,target);}
                if(facet==Facet::Value) for(Id argument:cells[id].items) (void)demand(argument,Facet::Value);
                equation(result,demand(target,facet));
            });
        });
    }
    void invocation(Id id,Facet facet,Ref result,bool inferInterface=true) {
        const auto& cell=cells[id];const auto& fn=graph.nodes[cell.definition.site];
        if(fn.external && facet==Facet::Type && fn.annotation==none) {equation(result,terms().make("NoValue"));return;}
        if(facet==Facet::Length || facet==Facet::Element) {
            if(fn.annotation!=none) {
                Id scope=environment(cell.definition.scope);
                for(std::size_t i=0;i<cell.items.size();++i) {
                    const auto& param=graph.nodes[fn.children[i]];
                    environments[scope].values[param.text]=bound(fn.children[i],scope,param.annotation,cell.items[i]);
                }
                equation(result,demand(bound(cell.site,scope,fn.annotation,none),facet));return;
            }
            auto element=fresh("call element"),length=fresh("call length");
            equation(demand(id,Facet::Type),terms().make("Array",{element,length}));
            equation(result,facet==Facet::Length ? length : element);return;
        }
        if(facet==Facet::Type && fn.annotation!=none) {
            Id scope=environment(cell.definition.scope);
            for(std::size_t i=0;i<cell.items.size();++i) {
                const auto& param=graph.nodes[fn.children[i]];
                environments[scope].values[param.text]=bound(fn.children[i],scope,param.annotation,cell.items[i]);
            }
            equation(result,annotation(fn.annotation,scope));return;
        }
        if(facet!=Facet::Type && facet!=Facet::Value) {pending("recursive aggregate property remains open",cell.site);return;}
        if(facet==Facet::Type && inferInterface) {equation(result,functionType(id));return;}
        std::vector<Ref> inputs;
        for(Id argument:cell.items) {inputs.push_back(demand(argument,Facet::Type));inputs.push_back(demand(argument,Facet::Value));}
        later(inputs,[this,id,facet,result](const auto& args) {
            PropertyScope property(activeProperty,{id,facet});
            const auto& invocationCell=cells[id];
            std::string key=std::to_string(invocationCell.definition.site)+":"+std::to_string(invocationCell.definition.scope)+":"+std::to_string(static_cast<unsigned>(facet));
            for(std::size_t i=0;i<args.size();i+=2) {
                bool runtime=args[i+1]->kind==Term::Constructor && args[i+1]->name=="Runtime";
                if(runtime && facet==Facet::Value) {pending("runtime recursive value requires a materialized invocation",invocationCell.site);return;}
                key+="|"+show(args[i])+":"+show(args[i+1]);
            }
            Id body;
            const auto& chain=environments[invocationCell.scope].evaluations;
            if(facet==Facet::Value && std::find(chain.begin(),chain.end(),key)!=chain.end()) {
                pending("recursive value revisits an unfinished invocation",invocationCell.site);return;
            }
            if(auto found=invocations.find(key);found!=invocations.end()) body=found->second;
            else {
                std::vector<Id> arguments;
                for(std::size_t i=0;i<args.size();i+=2) {
                    Cell parameter;parameter.kind=Cell::Known;parameter.expanded=true;parameter.site=invocationCell.site;
                    parameter.knownType=args[i];parameter.knownValue=args[i+1];
                    // A failed universal interface proof falls back to this
                    // actual invocation. Its runtime expression and caller
                    // premises remain facts; replacing them with an unrelated
                    // SignatureInput could discard a valid guarded branch.
                    arguments.push_back(addCell(std::move(parameter)));
                }
                body=instantiate(invocationCell.definition,arguments,invocationCell.site,invocationCell.scope,true,facet==Facet::Value ? key : "");
                invocations.emplace(key,body);
            }
            equation(result,demand(body,facet));
        });
    }
    Ref demand(Id id,Facet facet) {
        Property key{id,facet};
        if(supportInputs && (!activeProperty || *activeProperty!=key))
            supportInputs->push_back(terms().make("Property",{number(id),number(static_cast<unsigned>(facet))}));
        if(activeProperty && *activeProperty!=key) dependencies[*activeProperty].insert(key);
        PropertyScope property(activeProperty,key);
        auto& c=cells[id];
        auto name="source_property_"+std::to_string(id)+"_"+std::to_string(static_cast<unsigned>(facet));
        request.goals.push_back(require(name));
        if(auto found=c.properties.find(facet);found!=c.properties.end()) return found->second;
        Ref result=fresh("source property");c.properties.emplace(facet,result);
        auto support=fresh("property dependency support");supports.emplace(key,support);
        auto body=constructed([&]{
            std::vector<Ref> edges;SupportScope collecting(supportInputs,&edges);
            // Interface observations have no activation effect. An isolated
            // proof may replace their bundles with assumptions; execution
            // promises must not escape that boundary and then wait for the
            // implementation which the proof deliberately replaced.
            bool interface=facet==Facet::Type || facet==Facet::Category || facet==Facet::Identity || facet==Facet::Element || facet==Facet::Shape;
            SupportScope detachedEffects(effectInputs,interface ? nullptr : effectInputs),detachedRuntime(runtimeInputs,interface ? nullptr : runtimeInputs);
            (void)propertyBody(id,facet,result);equation(support,terms().make("Edges",edges));
        });
        program.bundles.emplace(name,std::move(body));
        return result;
    }
    std::optional<std::vector<Goal>> abstractProofBarrier(const std::string& name) {
        // Replacing native values with Runtime prototypes is a sufficient
        // interface abstraction, not universal quantification over source
        // choices. A rejected abstract alternative may be valid for a concrete
        // argument. Such a proof must defer joint source-choice closure to the
        // original request instead of publishing the surviving alternative.
        if(name.starts_with("type_equations_"))
            return std::vector<Goal>{unknown("abstract interface proof requires joint type-choice closure")};
        const std::string prefix="source_property_";if(!name.starts_with(prefix)) return {};
        auto split=name.find('_',prefix.size());if(split==std::string::npos) return {};
        Id id=std::stoul(name.substr(prefix.size(),split-prefix.size()));
        auto facet=static_cast<Facet>(std::stoul(name.substr(split+1)));
        const auto& cell=cells[id];
        if((facet==Facet::Value || facet==Facet::Execution) && cell.kind==Cell::SyntaxCell && cell.site!=none &&
           graph.nodes[cell.site].syntax==Syntax::Choice)
            return std::vector<Goal>{unknown("abstract interface proof requires joint finite-choice closure")};
        return {};
    }
    Ref propertyBody(Id id,Facet facet,Ref result) {
        auto& c=cells[id];
        expand(id);
        if(c.kind==Cell::Known && c.location!=none)
            request.goals.push_back(witness("capture_use_"+std::to_string(c.location),number(c.location)));
        if(c.kind==Cell::CapturePrototype) {
            if(c.location!=none) request.goals.push_back(witness("capture_use_"+std::to_string(c.location),number(c.location)));
            if(c.location!=none && facet!=Facet::Type && facet!=Facet::Category && facet!=Facet::Identity && facet!=Facet::Element && facet!=Facet::Shape)
                request.goals.push_back(witness("capture_input_"+std::to_string(c.location),number(c.location)));
            if(facet==Facet::Value) capturePrototypeValue(id,result);
            else equation(result,demand(c.target,facet));
            return result;
        }
        if(c.kind==Cell::LoopSlot) {
            if(facet==Facet::Value) runtime(demand(c.target,Facet::Type),terms().make("LoopSlotValue",{number(c.location),number(c.previous),number(0)}),result);
            else equation(result,demand(c.target,facet));
            return result;
        }
        if(c.kind==Cell::Known) if(auto fact=c.knownProperties.find(facet);fact!=c.knownProperties.end()) {
            equation(result,fact->second);return result;
        }
        if(facet==Facet::Storage) {storage(id,result);return result;}
        if(facet==Facet::Identity) {identity(id,result);return result;}
        if(facet==Facet::Category) {category(id,result);return result;}
        if(facet==Facet::Shape) {shapeProperty(id,result);return result;}
        if(c.kind==Cell::SchemaSymbol) {
            if(facet==Facet::Value) runtime(demand(c.target,Facet::Type),terms().make("SchemaField",{terms().make(c.inputName),number(0)}),result);
            else equation(result,demand(c.target,facet));
            return result;
        }
        if(facet==Facet::Execution) {
            auto execution=flow(id,queryState(c.scope));
            later({demand(execution.value,Facet::Type),demand(execution.value,Facet::Value),execution.code},[this,id,result](const auto& args){
                equation(result,after(args[0],args[1],args[2]));
                request.goals.push_back(witness("execution_"+std::to_string(id),args[2]));
            });
            return result;
        }
        if(c.kind==Cell::Receiver) {
            equation(result,facet==Facet::Value ? demand(id,Facet::Execution) : demand(c.target,facet));return result;
        }
        if(c.kind==Cell::Element) {
            if(facet==Facet::Type) equation(result,demand(c.target,Facet::Element));
            else if(facet==Facet::Value) indexValue(demand(c.target,Facet::Type),observedValue(c.target),c.knownValue,result);
            else typeProperty(demand(id,Facet::Type),facet,result);
            return result;
        }
        if(c.kind==Cell::Projection) {
            if(facet==Facet::Length) {
                later({demand(id,Facet::Category)},[this,id,result](const auto& categories){
                    if(categories[0]->name=="String") equation(result,demand(flow(id,queryState(cells[id].scope)).value,Facet::Length));
                    else projectedProperty(cells[id].target,cells[id].names,result,Facet::Length);
                });
            } else if(facet==Facet::Type || facet==Facet::Element) projectedProperty(c.target,c.names,result,facet);
            else if(facet==Facet::Value) equation(result,demand(id,Facet::Execution));
            else pending("undetermined member projection",c.site);
            return result;
        }
        if(!c.problem.empty() && c.kind==Cell::Bound) {pending(c.problem,c.site);return result;}
        if(c.kind==Cell::Missing) {pending(c.problem,c.site);return result;}
        if(c.kind==Cell::Dispatch) {dispatch(id,facet,result);return result;}
        if(c.kind==Cell::Invocation) {invocation(id,facet,result);return result;}
        if(c.kind==Cell::Math) {mathProperty(id,facet,result);return result;}
        if(c.flowWrapper && facet==Facet::Type && graph.nodes[c.definition.site].annotation==none) {
            equation(result,functionType(id));return result;
        }
        if(c.kind==Cell::Thunk) {
            if(facet==Facet::Type && c.annotation!=none) equation(result,annotation(c.annotation,c.scope));
            else if(facet==Facet::Value && c.annotation!=none) conversion(c.target,demand(id,Facet::Type),demand(c.target,Facet::Value),result);
            else equation(result,demand(c.target,facet));
            return result;
        }
        if(c.kind==Cell::Known) {
            if(facet==Facet::Type) equation(result,c.knownType);
            else if(facet==Facet::Value) {
                if(c.knownValue) equation(result,c.knownValue);
                else pending("specialized interface has no supplied value",c.site);
            }
            else if(facet==Facet::Length || facet==Facet::Element) {
                if(facet==Facet::Element) request.goals.push_back(call("element_type",{c.knownType,result}));
                else request.goals.push_back(call("extent_value",{c.knownType,c.knownValue ? c.knownValue : terms().make("NoValue"),result}));
            } else pending("specialized aggregate shape remains open",c.site);
            return result;
        }
        if(c.kind==Cell::Store) {
            if(facet==Facet::Type) equation(result,demand(c.location,Facet::Type));
            else if(!c.problem.empty()) {pending(c.problem,c.site);return result;}
            else if(facet==Facet::Value) conversion(c.target,demand(c.location,Facet::Type),demand(c.target,Facet::Value),result);
            else pending("aggregate store property remains open",c.site);
            return result;
        }
        if(Id call=operatorCall(id);call!=none) {operatorProperty(id,call,facet,result);return result;}
        if(auto op=broadcastName(id);!op.empty()) {broadcastProperty(id,op,facet,result);return result;}
        if(c.kind==Cell::Arithmetic) {
            binaryProperty(id,c.inputName,facet,result);
            return result;
        }
        if(c.kind==Cell::Alias) {
            if(facet==Facet::Value) for(Id effect:c.prefix) (void)demand(effect,Facet::Value);
            equation(result,demand(c.target,facet));return result;
        }
        if(c.kind==Cell::Input) {
            if(facet==Facet::Type) equation(result,annotation(c.annotation,c.scope));
            else if(facet==Facet::Value) sourceInput(id,result);
            else typeProperty(demand(id,Facet::Type),facet,result);
            return result;
        }
        if(c.kind==Cell::Bound) {
            if(facet==Facet::Length && (c.annotation==none || graph.nodes[c.annotation].children.empty())) {
                later({demand(id,Facet::Category)},[this,id,result](const auto& categories){
                    const auto& binding=cells[id];
                    if(categories[0]->name=="String") {
                        request.goals.push_back(call("extent_value",{terms().make("String"),observedValue(id),result}));return;
                    }
                    if(binding.annotation!=none && graph.nodes[binding.annotation].body!=none) {
                        if(global(id) || binding.field) aliasProperty(id,Facet::Length,result);
                        else equation(result,demand(expression(graph.nodes[binding.annotation].body,binding.scope),Facet::Length));
                    } else if(binding.target!=none) equation(result,demand(binding.target,Facet::Length));
                    else pending("undetermined length",binding.site);
                });return result;
            }
            if(c.annotation!=none && graph.nodes[c.annotation].body!=none && (global(id) || c.field) && facet==Facet::Type) aliasProperty(id,facet,result);
            if(c.annotation==none) {
                if(c.target==none) {if(facet!=Facet::Type) pending("uninitialized source value",c.site);}
                else if(global(id) && facet==Facet::Type) equation(result,observedType(c.target));
                else equation(result,demand(c.target,facet));
                return result;
            }
            const auto& spec=graph.nodes[c.annotation];
            bool array=spec.text!="(tuple)" && !spec.children.empty();
            if(!array && spec.body!=none && (facet==Facet::Element || facet==Facet::Length)) {
                if(global(id) || c.field) aliasProperty(id,facet,result);
                else equation(result,demand(expression(spec.body,c.scope),facet));
                return result;
            }
            if(facet==Facet::Type) {
                if(array) equation(result,terms().make("Array",{demand(id,Facet::Element),demand(id,Facet::Length)}));
                else if(spec.text=="(tuple)" && c.target!=none) equation(result,appliedAnnotation(c.annotation,c.scope,demand(c.target,Facet::Type)));
                else if(spec.body==none && record(c.scope,spec.text).site!=none) {
                    if(c.target==none) equation(result,annotation(c.annotation,c.scope));
                    else {
                        auto supplied=demand(c.target,Facet::Type);auto expected=record(c.scope,spec.text);
                        later({supplied},[this,expected,result](const auto& args){
                            Cell identity;identity.definition=expected;
                            if(args[0]->name!="Record" || !identical(args[0]->arguments[0],nominal(identity))) {conflict();return;}
                            equation(result,args[0]);
                        });
                    }
                } else equation(result,annotation(c.annotation,c.scope));
            } else if(facet==Facet::Element && array) {
                if(c.target!=none && spec.body==none && record(c.scope,spec.text).site!=none)
                    equation(appliedAnnotation(c.annotation,c.scope,demand(c.target,Facet::Type)),terms().make("Array",{result,fresh("supplied outer extent")}));
                else equation(result,annotatedElement(c.annotation,c.scope));
            }
            else if(facet==Facet::Element && spec.body==none && spec.text=="s") equation(result,terms().make("UInt",{number(8)}));
            else if(facet==Facet::Length && array) {
                if(c.field && c.target!=none) equation(result,demand(c.target,Facet::Length));
                else {
                    equation(result,demand(expression(spec.children.back(),c.scope),Facet::Value));
                    request.goals.push_back(lessEqual(number(0),result));
                    if(c.target!=none) equation(result,demand(c.target,Facet::Length));
                }
            } else if(facet==Facet::Value) {
                if(c.target==none) pending("uninitialized source value: "+graph.nodes[c.site].text,c.site);
                else conversion(c.target,demand(id,Facet::Type),demand(c.target,Facet::Value),result);
            } else if(c.target!=none) equation(result,demand(c.target,facet));
            else pending("undetermined annotated property",c.site);
            return result;
        }
        if(c.kind==Cell::Record) {
            if(facet==Facet::Type) equation(result,recordType(id));
            else if(facet==Facet::Value) {
                (void)demand(id,Facet::Type);std::vector<Ref> values;
                for(Id field:c.items) values.push_back(demand(field,Facet::Value));
                equation(result,terms().make("RecordValue",{nominal(c),terms().make("Values",values)}));
            } else pending("record is not an array",c.site);
            return result;
        }
        const auto& n=graph.nodes[c.site];
        if(n.syntax==Syntax::Assign) {
            if(facet==Facet::Type) equation(result,demand(c.items[0],Facet::Type));
            else if(facet==Facet::Value) equation(result,demand(id,Facet::Execution));
            else typeProperty(demand(id,Facet::Type),facet,result);
        } else if(n.syntax==Syntax::Probe) {
            if(facet==Facet::Type) equation(result,terms().make("Bool"));
            else if(facet==Facet::Value) memberProbe(c.items[0],n.text,result);
            else conflict();
        } else if(n.syntax==Syntax::Choice) {
            if(!environments[c.scope].puzzle) {conflict();return result;}
            if(facet==Facet::Type) equation(result,annotation(n.annotation,c.scope));
            else if(facet==Facet::Value) finiteChoice(id,result);
            else if(facet==Facet::Length || facet==Facet::Element) {
                auto element=fresh("choice element"),length=fresh("choice length");equation(demand(id,Facet::Type),terms().make("Array",{element,length}));equation(result,facet==Facet::Length ? length : element);
            } else pending("choice property is undetermined",c.site);
        } else if(n.syntax==Syntax::Binary && n.text=="..") {
            rangeProperty(id,facet,result);
        } else if(n.syntax==Syntax::Resource) {
            resourceProperty(id,facet,result);
        } else if(n.syntax==Syntax::String) {
            if(facet==Facet::Type) equation(result,terms().make("String"));
            else if(facet==Facet::Length) equation(result,number(n.text.size()));
            else if(facet==Facet::Element) equation(result,terms().make("UInt",{number(8)}));
            else if(facet==Facet::Value) {
                std::vector<Ref> bytes;for(unsigned char byte:n.text) bytes.push_back(number(byte));
                equation(result,terms().make("Bytes",bytes));
            } else pending("string property remains open",c.site);
        } else if(n.syntax==Syntax::Character) {
            if(facet==Facet::Type) equation(result,terms().make("UInt",{number(8)}));
            else if(facet==Facet::Value) equation(result,number(static_cast<unsigned char>(n.text.at(0))));
            else conflict();
        } else if(n.syntax==Syntax::Number) {
            if(n.text.find_first_of(".eE")!=std::string::npos && !n.text.starts_with("0x") && !n.text.starts_with("0X")) {
                if(facet==Facet::Type) equation(result,terms().make("Float",{number(64)}));
                else if(facet==Facet::Value) equation(result,number(floatLiteral(n.text,65536,n.span)));
                else pending("floating literal has no shape",c.site);
            }
            else if(facet==Facet::Type) equation(result,terms().make("Exact"));
            else if(facet==Facet::Value) {
                Integer value=0;unsigned base=n.text.starts_with("0x") || n.text.starts_with("0X") ? 16u : 10u;
                for(std::size_t at=base==16 ? 2u : 0u;at<n.text.size();++at) {
                    char digit=n.text[at];unsigned v=digit>='0'&&digit<='9' ? static_cast<unsigned>(digit-'0') : static_cast<unsigned>((digit>='a'?digit-'a':digit-'A')+10);
                    value=value*base+v;
                }
                equation(result,number(value));
            } else pending("scalar literal has no shape",c.site);
        } else if(n.syntax==Syntax::Tuple) {
            std::vector<Ref> parts;
            if(facet==Facet::Type) {
                for(Id child:c.items) parts.push_back(demand(child,Facet::Type));
                equation(result,tupleType(terms(),parts));
            } else if(facet==Facet::Value) {
                for(Id child:c.items) parts.push_back(demand(child,Facet::Value));
                equation(result,terms().make("RecordValue",{terms().make("Tuple",{number(c.items.size())}),terms().make("Values",parts)}));
            } else conflict();
        } else if(n.syntax==Syntax::Destructure) {
            equation(result,terms().make("NoValue"));
        } else if(n.syntax==Syntax::Array) {
            if(facet==Facet::Length) equation(result,number(c.items.size()));
            else if(facet==Facet::Element) {
                Ref element=c.items.empty() ? terms().make("Exact") : demand(c.items[0],Facet::Type);
                for(std::size_t i=1;i<c.items.size();++i) {
                    Id child=c.items[i];
                    Ref next=fresh("common element");commonType(element,demand(child,Facet::Type),next);element=next;
                }
                equation(result,element);
            } else if(facet==Facet::Type) equation(result,terms().make("Array",{demand(id,Facet::Element),demand(id,Facet::Length)}));
            else {
                std::vector<Ref> values;Ref element=demand(id,Facet::Element);
                for(Id child:c.items) {auto value=fresh("array item");conversion(child,element,demand(child,Facet::Value),value);values.push_back(value);}
                equation(result,terms().make("Values",values));
            }
        } else if(n.syntax==Syntax::Index) {
            if(facet==Facet::Type) equation(result,demand(c.items[0],Facet::Element));
            else if(facet==Facet::Value) indexValue(demand(c.items[0],Facet::Type),demand(c.items[0],Facet::Value),demand(c.items[1],Facet::Value),result);
            else if(facet==Facet::Length || facet==Facet::Element) {
                auto element=fresh("indexed element"),length=fresh("indexed length");
                equation(demand(id,Facet::Type),terms().make("Array",{element,length}));equation(result,facet==Facet::Length ? length : element);
            } else pending("index property has no schema",c.site);
        } else if(n.syntax==Syntax::Member) {
            auto memberType=facet==Facet::Type ? result : fresh("member type");
            memberValue(demand(c.items[0],Facet::Type),facet==Facet::Value ? demand(c.items[0],Facet::Value) : Ref{},n.text,memberType,result);
            if(facet==Facet::Length || facet==Facet::Element) {
                auto element=fresh("member element"),length=fresh("member length");
                equation(memberType,terms().make("Array",{element,length}));equation(result,facet==Facet::Length ? length : element);
            }
        } else if(n.syntax==Syntax::Cast) {
            Ref target=annotation(n.children[0],c.scope);
            if(facet==Facet::Type) equation(result,target);
            else if(facet==Facet::Value) {
                castValue(demand(c.items[1],Facet::Type),target,demand(c.items[1],Facet::Value),result);
            } else if(facet==Facet::Length || facet==Facet::Element) {
                later({target},[this,id,facet,result](const auto& types){
                    if(facet==Facet::Length && types[0]->name=="String") request.goals.push_back(call("extent_value",{types[0],demand(id,Facet::Value),result}));
                    else typeProperty(types[0],facet,result);
                });
            }
            else pending("cast has no requested property",c.site);
        } else if(n.syntax==Syntax::Select) {
            if(c.items.size()!=3) {pending("statement conditional state merge remains open",c.site);return result;}
            Ref condition=fresh("boolean condition");
            request.goals.push_back(call("implicit",{demand(c.items[0],Facet::Type),terms().make("Bool"),demand(c.items[0],Facet::Value),condition}));
            later({conditionedValue(terms().make("Bool"),condition,states[queryState(c.scope)].conditions,!states[queryState(c.scope)].controlShadow)},[this,id,facet,result](const auto& args) {
                PropertyScope property(activeProperty,{id,facet});
                const auto& selection=cells[id];const auto& cond=args[0];
                if(cond->kind==Term::Number) {
                    if(cond->number!=0 && cond->number!=1) {conflict();return;}
                    equation(result,demand(selection.items[cond->number==0 ? 2u : 1u],facet));
                } else if(cond->kind==Term::Constructor && cond->name=="Runtime") {
                    auto left=demand(selection.items[1],Facet::Type),right=demand(selection.items[2],Facet::Type);
                    equation(left,right);
                    if(facet==Facet::Type) equation(result,left);
                    else if(facet==Facet::Value) {
                        auto a=demand(selection.items[1],Facet::Value),b=demand(selection.items[2],Facet::Value);
                        selectValue(left,cond,a,b,result);
                    } else equation(result,demand(selection.items[1],facet)),equation(result,demand(selection.items[2],facet));
                } else conflict();
            });
        } else if(n.syntax==Syntax::Unary && n.text=="|") {
            if(facet!=Facet::Type && facet!=Facet::Value) pending("length/absolute result has no shape",c.site);
            else later({demand(c.items[0],Facet::Category)},[this,id,facet,result](const auto& categories){
                Id operand=cells[id].items[0];
                if(categories[0]->name=="Array" || categories[0]->name=="String") {equation(result,facet==Facet::Type ? terms().make("Exact") : demand(operand,Facet::Length));return;}
                if(categories[0]->name!="Scalar") {conflict();return;}
                auto type=demand(operand,Facet::Type);
                request.goals.push_back(call("arithmetic_operand",{type}));
                if(facet==Facet::Type) {equation(result,type);return;}
                later({type},[this,operand,result](const auto& types){
                    auto value=demand(operand,Facet::Value);
                    if(types[0]->name=="UInt") equation(result,value);
                    else if(types[0]->name=="SInt") request.goals.push_back(call("signed_absolute",{types[0],value,result}));
                    else if(types[0]->name=="Float" || types[0]->name=="Fixed") request.goals.push_back(build("numeric_unary",{types[0],terms().make("|"),value,result}));
                    else {
                        auto negative=fresh("absolute negative");request.goals.push_back(add(value,negative,number(0)));
                        request.goals.push_back(call("maximum",{value,negative,result}));
                    }
                });
            });
        } else if(n.syntax==Syntax::Binary && (n.text=="==" || n.text=="!=" || n.text=="<" || n.text=="<=" || n.text==">" || n.text==">=")) {
            if(facet==Facet::Type) equation(result,terms().make("Bool"));
            else if(facet==Facet::Value) {
                compareOperands(demand(c.items[0],Facet::Type),demand(c.items[0],Facet::Value),
                                demand(c.items[1],Facet::Type),demand(c.items[1],Facet::Value),n.text,result);
            } else pending("comparison result has no shape",c.site);
        } else if(n.syntax==Syntax::Binary && (n.text=="+" || n.text=="-" || n.text=="*" || n.text=="/" || n.text=="%" || n.text=="&" || n.text=="|" || n.text=="^" || n.text=="<<" || n.text==">>" || n.text=="&&" || n.text=="||")) {
            binaryProperty(id,n.text,facet,result);
        } else if(n.syntax==Syntax::Unary && n.text=="!") {
            request.goals.push_back(call("logical_operand",{demand(c.items[0],Facet::Type)}));
            if(facet==Facet::Type) equation(result,terms().make("Bool"));
            else if(facet==Facet::Value) {
                auto value=fresh("logical operand");
                request.goals.push_back(call("implicit",{demand(c.items[0],Facet::Type),terms().make("Bool"),demand(c.items[0],Facet::Value),value}));
                equation(result,booleanNot(value));
            } else pending("logical result has no shape",c.site);
        } else if(n.syntax==Syntax::Unary && (n.text=="+" || n.text=="-" || n.text=="~")) {
            request.goals.push_back(call(n.text=="~" ? "bitwise_operand" : "arithmetic_operand",{demand(c.items[0],Facet::Type)}));
            if(facet==Facet::Type) equation(result,demand(c.items[0],Facet::Type));
            else if(facet==Facet::Value && n.text=="+") equation(result,demand(c.items[0],Facet::Value));
            else if(facet==Facet::Value) later({demand(id,Facet::Type)},[this,id,result,op=n.text](const auto& types){
                auto value=demand(cells[id].items[0],Facet::Value);
                if(types[0]->name=="Float" || types[0]->name=="Fixed") request.goals.push_back(build("numeric_unary",{types[0],terms().make(op),value,result}));
                else request.goals.push_back(call("arithmetic-",{types[0],number(op=="~" ? -1 : 0),value,result}));
            });
            else pending("unary result has no shape",c.site);
        } else pending("source construct is not yet adapted: "+std::to_string(static_cast<int>(n.syntax)),c.site);
        return result;
    }
    Id exported(const std::string& name,const std::string& occurrence={}) {
        const auto key=occurrence.empty() ? name : "@"+occurrence;
        if(auto found=roots.find(key);found!=roots.end()) return found->second;
        Id value=lookup(0,name);
        if(value==none) {
            auto defs=functions(0,name);
            if(defs.size()!=1) throw std::invalid_argument("unknown or overloaded source root: "+name);
            const auto& fn=graph.nodes[defs[0].site];std::vector<Id> arguments;
            Id parameterScope=environment(defs[0].scope);
            for(Id site:fn.children) {
                const auto& param=graph.nodes[site];
                if(param.annotation==none) throw Error(param.span,"boundary parameters require explicit interfaces");
                if(param.expression || fn.receiverCount) throw Error(param.span,"source boundaries require value parameters");
                Cell input;input.kind=Cell::Input;input.expanded=true;input.site=site;input.scope=parameterScope;input.annotation=param.annotation;
                input.inputName=param.text;input.location=arguments.size();
                for(const auto& tag:param.attributes) {
                    if(tag.name!="range") continue;
                    if(tag.arguments.size()!=2) throw Error(tag.span,"range annotation needs lower and upper bounds");
                    auto lower=observedValue(expression(tag.arguments[0],defs[0].scope));
                    auto upper=observedValue(expression(tag.arguments[1],defs[0].scope));
                    if(input.inputRange) {equation(input.inputMin,lower);equation(input.inputMax,upper);}
                    else {input.inputRange=true;input.inputMin=lower;input.inputMax=upper;}
                }
                arguments.push_back(addCell(std::move(input)));
                environments[parameterScope].values[param.text]=arguments.back();
            }
            value=instantiate(defs[0],arguments,defs[0].site,0);cells[value].parameters=arguments;
        }
        roots[key]=value;return value;
    }
    void sourceInput(Id id,Ref result);
    void configure();
    void mapperStorage(Id id,Ref persistent);
    unsigned configured(const Model& model,const std::string& key) const;
    void validateContracts();
    Ref externalAccesses(const std::string& name);
    Id path(const std::string& text,bool evaluateOwner) {
        (void)evaluateOwner;
        auto dot=text.find('.');Id cell=exported(text.substr(0,dot));
        while(dot!=std::string::npos) {
            auto end=text.find('.',dot+1);std::string member=text.substr(dot+1,end==std::string::npos ? end : end-dot-1);
            cell=projection(cell,{member});dot=end;
        }
        return cell;
    }
    void scoreArguments(Definition definition,const std::vector<Id>& arguments,std::size_t at,Id scope,Ref total,Ref result,bool signatureOnly=false);
    void matchArgument(Id annotation,Id scope,Id argument,Ref result,bool invariant=false,bool signatureOnly=false);
    void matchScore(Ref supplied,Ref target,Id argument,Ref result,bool invariant=false,bool signatureOnly=false);
    void matchingProperty(Id argument,Facet facet,const std::function<void(Ref)>& use);
    void memberProbe(Id owner,const std::string& member,Ref result);
    void captureSupport(Ref tree,const std::map<Id,std::string>& lexical,const std::set<Id>& owned,std::set<Property> visited,Ref result);
    Ref structure(const Ref& type,const std::function<Ref(const Ref&)>& scalar);
    void selectScalarValue(Ref type,Ref condition,Ref left,Ref right,Ref result);
    void selectValue(Ref type,Ref condition,Ref left,Ref right,Ref result);
    void indexValue(Ref type,Ref array,Ref index,Ref result);
    void memberValue(Ref type,Ref value,const std::string& name,Ref resultType,Ref result);
    void flowRecord(Id id,Id before,const Flow& out);
    void flowMember(Id id,Id before,const Flow& out);
    void flowAssign(Id id,Id before,const Flow& out);
    void flowDestructure(Id id,Id before,const Flow& out);
    bool mutableReceiver(Id argument);
    Id captureLocation(Id value) const;
    std::vector<Id> callCaptures(Definition definition,const std::vector<Id>& arguments,bool visible=false) const;
    std::vector<Definition> callCandidates(Id site,Id scope) const;
    void flowReceivers(const std::vector<Id>& arguments,const std::vector<bool>& deferred,std::size_t count,Id site,Id before,const std::function<void(std::vector<Id>,Id,Ref)>& finish);
    void flowReceiver(Id id,Id before,const Flow& out);
    void flowReceiverStore(Id id,Id before,const Flow& out);
    std::string callName(Id id) const;
    std::size_t callReceivers(Id id) const;
    std::string operatorName(Id id) const;
    Id methodCall(Id site,Id scope,const std::string& name,const std::vector<Id>& arguments,bool allowBuiltin=false);
    Id operatorCall(Id id);
    void operatorProperty(Id id,Id call,Facet facet,Ref result);
    void flowOperator(Id id,Id call,Id before,const Flow& out);
    void flowCall(Id id,Id before,const Flow& out);
    void flowAssignAt(Id id,Id slot,const std::vector<Ref>& path,Id before,const Flow& out,bool builtin=false);
    void storePlace(Id slot,const std::vector<Ref>& path,Id value,Id before,const Flow& out,bool statement);
    void binaryProperty(Id id,const std::string& op,Facet facet,Ref result);
    std::string broadcastName(Id id) const;
    Id element(Id owner,std::size_t index);
    std::vector<Id> broadcastElements(Id id,const std::string& op,const std::vector<Id>& operands,const std::vector<Ref>& types,const std::vector<std::size_t>& shape,bool prototype=false,bool math=false);
    void mathProperty(Id id,Facet facet,Ref result);
    Ref broadcastType(const std::vector<Id>& elements,const std::vector<std::size_t>& shape);
    void broadcastProperty(Id id,const std::string& op,Facet facet,Ref result);
    void flowBroadcast(Id id,const std::string& op,Id before,const Flow& out);
    void flowBroadcastStore(Id id,Id slot,const std::vector<Ref>& path,Id previous,Id rhs,Id before,const Flow& out);
    void compareValue(Ref type,Ref left,Ref right,const std::string& op,Ref result);
    void compareOperands(Ref leftType,Ref left,Ref rightType,Ref right,const std::string& op,Ref result);
    void compareSequence(const std::vector<Ref>& types,const std::vector<Ref>& left,const std::vector<Ref>& right,const std::string& op,Ref result);
    Ref arithmeticCompletion(Id id);
    Ref castCompletion(Id id);
    void castValue(Ref source,Ref target,Ref input,Ref result);
    void flowLogical(Id id,Id before,const Flow& out);
    Ref booleanAnd(Ref a,Ref b);
    Ref booleanNot(Ref value);
    Ref conditionedValue(Ref type,Ref value,const std::vector<Ref>& premises,bool expandNative=true);
    Ref controlMeaning(Ref value);
    std::optional<std::vector<Ref>> nativeControlResult(Id temporary);
    using FlowAction=std::function<void(Id,const Flow&)>;
    void compoundFlow(const std::string& op,Id previous,const std::vector<Ref>& path,Id before,Ref prefix,const Flow& out,const FlowAction& action);
    void normalFlow(Id before,const Flow& out,const FlowAction& action);
    void continueFlow(Id before,Ref prefix,const Flow& out,const FlowAction& action);
    void guardedFlow(Id id,Id before,const Flow& out);
    void finishFunction(const Flow& body,Id before,Id scope,Id annotation,Id owner,const Flow& out);
    void flowArguments(const std::vector<Id>& arguments,const std::vector<bool>& expressions,Id before,const std::function<void(std::vector<Id>,Id,Ref)>& finish);
    void flowThunk(Id id,Id before,const Flow& out);
    void flowExit(Id id,Id before,const Flow& out);
    InitializationFacts initialization(Id slot,const FlowState& at);
    InitializationFacts mergeInitialization(Ref condition,const InitializationFacts& yes,const InitializationFacts& no);
    Ref runtimeInitialized(Id slot,const FlowState& at);
    Ref initialized(Id slot,const FlowState& state);
    void requireInitialized(Id slot,Id state);
    Id reachingValue(Id slot,Id state) const;
    void initializationObligation(Id slot,Ref ready,const std::vector<Ref>& premises,Id loop);
    Ref closeLoopInitialization(Id loop,Id before,Id checked,Id ended,Ref condition,Ref stopped,Ref checkAgain,Ref escaped,
                                const std::vector<Id>& carried,Ref iterableType,Id firstTemporary);
    void finiteChoice(Id id,Ref result);
    void rangeProperty(Id id,Facet facet,Ref result);
    void buildLoop(Id id,Id before,const Flow& out,Ref iterableType,Ref iterableValue,Ref prefix);
    void runtimeLoop(Id id,Id before,const Flow& out);
    void repeatStep(Id id,Id before,std::size_t exitBase,const Flow& out,std::set<std::string> visited);
    void repeatAdvance(Id id,Id before,std::size_t exitBase,const Flow& out,std::set<std::string> visited,Id predicate=none);
    void flowIteration(Id id,Id before,const Flow& out);
    void flowCustomIteration(Id id,Id before,const Flow& out);
    void flowIterationBody(Id id,Id before,const Flow& out);
    void resourceProperty(Id id,Facet facet,Ref result);
    void sortItems(Id id,std::vector<Id> items,std::size_t next,std::size_t position,Id before,const std::function<void(std::vector<Id>)>& finish);
    void iterateItems(Id id,const std::vector<Id>& items,std::size_t at,Id before,std::size_t exitBase,Ref breaks,const Flow& out);
    void place(Id site,Id scope,Id before,const std::function<void(Id,std::vector<Ref>,Id,Ref)>& finish);
    void replaceValue(Ref type,Ref value,const std::vector<Ref>& path,Id replacement,Ref result,Ref assigned);
    void placeType(Ref type,const std::vector<Ref>& path,Ref result,Ref checks);
    void resolvePlaceType(Ref type,const std::vector<Ref>& path,
                          const std::function<void(Ref,Ref)>& present,
                          const std::function<void(Diagnostic)>& absent={});
    bool global(Id id) const {return cells[id].kind==Cell::Bound && cells[id].site!=none && graph.nodes[cells[id].site].syntax==Syntax::Global;}
    void storage(Id id,Ref result);
    std::pair<bool,bool> effectSignature(Id site,Id scope,std::set<Id>& calls);
    void flowGlobal(Id id,Id before,const Flow& out);
    void flowGlobalStore(Id slot,Id value,Id before,const Flow& out,bool statement,Ref mask={});
    void flowGlobalAssign(Id id,Id slot,const std::vector<Ref>& path,Id before,const Flow& out);
    Ref placeMask(const Ref& type,const std::vector<Ref>& path);
    void flowExternal(Definition definition,const std::vector<Id>& arguments,Id before,const Flow& out);
    void prepareAliases(Id scope);
    Ref aliasesReady(Id scope);
    void aliasProperty(Id id,Facet facet,Ref result);
    void commonType(Ref a,Ref b,Ref result);
    void equationProperty(Id owner,const std::vector<std::string>& path);
    void closeTypeEquations(const std::vector<Id>& owners,const std::vector<std::string>& path,const std::map<Property,Ref>& inputs={});
    Id projection(Id owner,std::vector<std::string> path);
    void projectionOwner(Id owner,const std::function<void(Id)>& follow);
    void projectedProperty(Id owner,const std::vector<std::string>& path,Ref result,Facet facet=Facet::Type);
    void identity(Id id,Ref result);
    void category(Id id,Ref result);
    void typeCategory(Ref type,Ref result);
    void typeProperty(Ref type,Facet facet,Ref result);
    void annotationCategory(Id site,Id scope,Ref result);
    void annotationShape(Id site,Id scope,Ref result);
    void shapeProperty(Id id,Ref result);
    void broadcastDimensions(const std::vector<Id>& operands,Ref result);
    void flowProjection(Id id,Id before,const Flow& out);
    Id schemaAlias(Id target,const std::string& name,Id lexical);
    Id schemaOrigin(const std::string& name,Id lexical);
    bool schemaBranches(Id site,Id scope);
    void captureSchemaType(Id id,std::set<Id> visited);
    Ref recordRequirement(Ref type,Id annotation,Id scope,Id dimensionScope=none);
    Ref validateSchema(Ref type,Ref value);
    bool changesProjectionState(Id id);
    Id frozenScope(Id lexical,Id at);
    Id frozenProperty(Id id,Id at);
    Ref boundaryAccesses(Ref code,std::set<Id> visitedFrames={},std::set<Id> visitedStorage={});
    // Physical candidate construction follows below. It does not consume a
    // solved source model: ranges, types and extents remain shared promises.
    void physical();
    void project(SourceResult& answer,const Model& model,std::size_t observationCount);
    void projectStream(const Output&,const std::string&,const std::string&,unsigned,unsigned,unsigned,std::ostream&,std::ostream&);
    std::optional<std::vector<Ref>> closeObservation(const std::vector<Ref>& observations,const std::map<std::string,Ref>& artifacts);
    void projectBoundary(const Model& model,std::ostream& header,std::ostream& code);
    bool needsFlow(Id id);
    Id known(Ref type,Ref value,Id site=none);
    Id state(FlowState value);
    Id location(Id id) const;
    Ref locationType(Id slot,Id at);
    std::vector<Id> captures(Id scope,Id declaration=none) const;
    Ref sequence(Ref a,Ref b);
    Ref after(const Ref& type,const Ref& value,const Ref& code);
    Ref observedValue(Id id) {return demand(id,needsFlow(id) ? Facet::Execution : Facet::Value);}
    Ref observedType(Id id,bool implementing=false,bool* execution=nullptr);
    Ref functionType(Id id);
    bool isolatedFunctionInterface(Id id);
    void closeFunctionType(Id id,Ref result,const std::vector<Ref>& types,const std::map<Property,Ref>& inputs={});
    void capturePrototypeValue(Id id,Ref result);
    void selectCaptures(Definition definition,const std::vector<Id>& arguments,Id site,Id scope,Id before,
                        const std::vector<Id>& candidates,const std::function<void(const CapturePlan&)>& finish,
                        const std::map<Property,Ref>& inputs={});
    bool captureAvailable(Id slot,Id at);
    void runtime(Ref type,Ref expression,Ref result);
    Flow flow(Id id,Id before);
    void flowBody(Id id,Id before,const Flow& out);
    void flowSequence(const std::vector<Id>& list,Id target,Id before,const Flow& out);
    void flowItems(const std::vector<Id>& items,Id before,const std::function<void(std::vector<Id>,Id,Ref)>& finish);
    void flowInvoke(Definition definition,std::vector<Id> arguments,Id site,Id callerScope,Id before,const Flow& out);
    void flowReturn(const Flow& body,Id before,const std::vector<Id>& captured,const Flow& out);
    void flowMerge(Ref condition,const Flow& left,const Flow& right,Id before,const Flow& out);
    void materialize(Definition definition,const std::vector<Id>& arguments,const std::vector<Ref>& types,
                     Id site,Id scope,Id before,const CapturePlan& captures,const Flow& out);
    Ref framePattern(Ref type,Ref value);
    void frameValue(Ref type,Ref pattern,Ref expression,Ref result);
public:
    Session(Graph& g,const SourceOptions& o):graph(g),options(o) {
        searchBudget=o.search.budget ? o.search.budget : std::make_shared<SearchBudget>(SearchBudget{o.search.maxWork,o.search.maxDecisions,0,0});
        startingWork=searchBudget->work;startingDecisions=searchBudget->decisions;
        states[0].live=number(1);
        environment(none);
        auto declarationsList=moduleDeclarations(graph,graph.nodes[graph.files[0].node].body);
        declarations(declarationsList,0,true);
        for(const auto& n:graph.nodes) if(n.syntax==Syntax::Type) {
            if(auto bits=unsignedWidth(n.text)) widths.insert(*bits);
            if(auto bits=signedWidth(n.text)) widths.insert(*bits);
        }
        sourceRules(program,widths);
        numericFormats={terms().make("Exact"),terms().make("Bool"),terms().make("String")};
        for(unsigned bits:widths) {numericFormats.push_back(terms().make("UInt",{number(bits)}));numericFormats.push_back(terms().make("SInt",{number(bits)}));}
        std::map<std::string,Ref> extra;
        for(const auto& spelling:{"f16","f32","f64"}) {
            auto type=extraNumericType(terms(),spelling);extra.emplace(show(type),type);
        }
        for(const auto& n:graph.nodes) if(n.syntax==Syntax::Type) {
            try {if(auto type=extraNumericType(terms(),n.text)) extra.emplace(show(type),type);}
            // Recognition is not a demand. A demanded oversized annotation
            // reports exhaustion inside run(), without poisoning other roots.
            catch(const ConstructionLimit&) {}
        }
        for(const auto& [name,type]:extra) {(void)name;numericFormats.push_back(type);}
        numericRules(program,numericFormats);
    }
    SourceResult run();
};

#include "source_support.inc"
#include "source_aggregate.inc"
#include "source_control_native.inc"
#include "source_control.inc"
#include "source_operators.inc"
#include "source_broadcast.inc"
#include "source_representation.inc"
#include "source_loop_initialization.inc"
#include "source_loop.inc"
#include "source_finite.inc"
#include "source_boundary.inc"
#include "source_contracts.inc"
#include "source_configuration.inc"
#include "source_types.inc"
#include "source_type_equations.inc"
#include "source_schema.inc"
#include "source_projection.inc"
#include "source_shape.inc"
#include "source_methods.inc"
#include "source_iteration_methods.inc"
#include "source_resources.inc"
#include "source_math.inc"
#include "source_frame.inc"
#include "source_inputs.inc"
#include "source_function_type.inc"
#include "source_capture_closure.inc"
#include "source_observation.inc"
#include "source_flow.inc"

void Session::physical() {
    const Ref maximumPayload=configuration.at("payload_bytes");
    std::vector<Ref> accesses;
    program.define("physical_encoding",3,{
        {{number(16),number(2),number(0)},{},"prefer 16-bit storage"},
        {{number(8),number(1),number(1)},{},"8-bit storage"},
        {{number(32),number(4),number(2)},{},"32-bit storage"}
    });
    auto v=fresh("value"),e=fresh("expression"),lo=fresh("lo"),hi=fresh("hi");
    program.define("stored_count",2,{
        {{v,number(0)},{add(v,number(0),v)},"embed a constant"},
        {{terms().make("Runtime",{e,lo,hi}),number(1)},{},"store a runtime result"}
    });
    program.define("physical_fits",3,{
        {{number(8),lo,hi},{lessEqual(number(0),lo),lessEqual(hi,number(255))},"one byte"},
        {{number(16),lo,hi},{lessEqual(number(0),lo),lessEqual(hi,number(65535))},"two bytes"},
        {{number(32),lo,hi},{lessEqual(number(0),lo),lessEqual(hi,number((Integer(1)<<32)-1))},"four bytes"}
    });
    auto encoding=fresh("encoding"),width=fresh("logical scalar width");
    std::vector<Clause> physicalValues;
    for(const auto& type:{terms().make("UInt",{width}),terms().make("Exact"),terms().make("Bool")}) {
        physicalValues.push_back({{type,encoding,v},{lessEqual(number(0),v),lessEqual(v,number((Integer(1)<<32)-1))},"embedded unsigned ABI constant"});
        physicalValues.push_back({{type,encoding,terms().make("Runtime",{e,lo,hi})},{call("physical_fits",{encoding,lo,hi})},"stored unsigned runtime value"});
    }
    auto signedType=terms().make("SInt",{width});
    physicalValues.push_back({{signedType,encoding,v},{lessEqual(number(-(Integer(1)<<31)),v),lessEqual(v,number((Integer(1)<<31)-1))},"embedded signed ABI constant"});
    for(unsigned bits:{8u,16u,32u})
        physicalValues.push_back({{signedType,number(bits),terms().make("Runtime",{e,lo,hi})},{lessEqual(number(-(Integer(1)<<(bits-1))),lo),lessEqual(hi,number((Integer(1)<<(bits-1))-1))},"stored signed runtime value"});
    for(const auto& type:numericFormats) if(type->name=="Float" || type->name=="Fixed") {
        if(!nativeScalar(type)) continue;
        bool sign=scalarSigned(type);
        physicalValues.push_back({{type,encoding,v},{lessEqual(number(sign ? -(Integer(1)<<31) : Integer(0)),v),lessEqual(v,number((Integer(1)<<(sign ? 31 : 32))-1))},"embedded numeric ABI constant"});
        for(unsigned bits:{8u,16u,32u})
            physicalValues.push_back({{type,number(bits),terms().make("Runtime",{e,lo,hi})},{lessEqual(number(sign ? -(Integer(1)<<(bits-1)) : Integer(0)),lo),lessEqual(hi,number((Integer(1)<<(bits-(sign ? 1 : 0)))-1))},"stored numeric runtime value"});
    }
    program.define("physical_value",3,std::move(physicalValues));
    std::vector<Clause> observable{{{terms().make("Exact")},{},"exact"},{{terms().make("Bool")},{},"boolean"}};
    for(unsigned bits=1;bits<=32;++bits) {
        observable.push_back({{terms().make("UInt",{number(bits)})},{},"unsigned"});
        observable.push_back({{terms().make("SInt",{number(bits)})},{},"signed"});
    }
    for(const auto& type:numericFormats) if((type->name=="Float" || type->name=="Fixed") && nativeScalar(type)) observable.push_back({{type},{},"numeric"});
    program.define("projectable_scalar",1,std::move(observable));
    ActivationGraph activation;std::vector<Id> starts,produced,released;
    auto event=[&](const std::string& name) {
        Id id=activation.event(name);boundaryEventNames.push_back(name);return id;
    };
    auto before=[&](Id first,Id second) {
        activation.before(first,second);boundaryEventOrder.emplace(first,second);
    };
    Ref occupied=number(0);std::set<std::string> seen,publicNames{
        "payload","trap","fault","segment_b","bank_b","msx_putchar","signed","mul","divmod","shift",
        "rescale","fixed_mul","fixed_div","float_binary","float_convert","math","last_result",
        "stream_running","stream_sequence","call_depth","cartridge_slot","mapper_slot",
        "mapper_init","mapper_read","mapper_write","mapper_transfer","link_root"};
    for(const auto& name:options.sources) {
        for(const auto* prefix:{"flow_","ensure_","load_","store_","invoke_external_","external_","chunk_"})
            if(name.starts_with(prefix)) throw std::invalid_argument("source boundary conflicts with a generated function namespace: "+name+
                "; use --occurrence with another boundary name");
        if(!seen.insert(name).second) throw std::invalid_argument("duplicate source boundary: "+name);
        if(!publicNames.insert(name).second || !publicNames.insert(name+"_read").second ||
           !publicNames.insert(name+"_submit").second || !publicNames.insert(name+"_release").second ||
           !publicNames.insert(name+"_read_lease").second || !publicNames.insert(name+"_read_at").second ||
           !publicNames.insert(name+"_leases").second)
            throw std::invalid_argument("source boundary conflicts with a generated or runtime C name: "+name);
        const auto definition=options.occurrenceSources.find(name);
        auto identifier=[](const std::string& text) {
            auto alpha=[](char c){return c=='_' || (c>='a' && c<='z') || (c>='A' && c<='Z');};
            return !text.empty() && alpha(text[0]) && std::all_of(text.begin(),text.end(),[&](char c){return alpha(c) || (c>='0' && c<='9');});
        };
        if(!identifier(name)) throw std::invalid_argument("source occurrence needs a C identifier: "+name);
        Output out;out.name=name;out.source=definition==options.occurrenceSources.end() ? name : definition->second;
        out.cell=exported(out.source,definition==options.occurrenceSources.end() ? "" : name);out.parameters=cells[out.cell].parameters;
        Ref inputChecks=terms().make("Done");
        for(Id parameter:out.parameters)
            inputChecks=sequence(inputChecks,validateSchema(demand(parameter,Facet::Type),demand(parameter,Facet::Value)));
        request.goals.push_back(witness("source_checks_"+std::to_string(out.cell),inputChecks));
        observableArtifacts.push_back("source_checks_"+std::to_string(out.cell));
        out.type=needsFlow(out.cell) ? demand(flow(out.cell,0).value,Facet::Type) : demand(out.cell,Facet::Type);
        out.value=observedValue(out.cell);
        if(needsFlow(out.cell)) observableArtifacts.push_back("execution_"+std::to_string(out.cell));
        auto access=boundaryAccesses(needsFlow(out.cell) ? flow(out.cell,0).code : terms().make("Done"));
        accesses.push_back(access);request.goals.push_back(witness("access_"+name,access));
        out.bits=fresh(name+" encoding");out.bytes=fresh(name+" bytes");out.offset=fresh(name+" offset");out.preference=fresh(name+" preference");
        out.types=fresh("output leaf types");out.values=fresh("output leaf values");out.paths=fresh("output leaf paths");
        Ref unit=fresh("bytes per scalar"),count=fresh("stored scalar count");
        request.goals.push_back(call("physical_encoding",{out.bits,unit,out.preference}));
        later({out.type},[this,out,count](const auto& args){
            std::vector<Ref> types,values,paths;
            // Shape can itself be a candidate fact. Do not flatten source syntax
            // or mutate a globally shared output description during search.
            auto value=structure(args[0],[&](const Ref& type){auto leafValue=fresh("output leaf");types.push_back(type);values.push_back(leafValue);return leafValue;});
            equation(out.value,value);
            for(const auto& leaf:leaves(args[0],{},out.name)) paths.push_back(terms().make(leaf.path));
            equation(out.types,terms().make("Types",types));equation(out.values,terms().make("Values",values));
            equation(out.paths,terms().make("Paths",paths));
            Ref total=number(0);
            for(std::size_t i=0;i<values.size();++i) {
                auto stored=fresh("stored scalar"),sum=fresh("scalar count");
                request.goals.push_back(call("projectable_scalar",{types[i]}));
                request.goals.push_back(call("physical_value",{types[i],out.bits,values[i]}));
                request.goals.push_back(call("stored_count",{values[i],stored}));
                request.goals.push_back(add(total,stored,sum));total=sum;
            }
            equation(count,total);
        });
        request.goals.push_back(multiply(count,unit,out.bytes));
        out.allocationBytes=out.bytes;
        if(options.streams.contains(name)) {
            out.allocationBytes=fresh(name+" stream storage");
            auto buffers=fresh(name+" stream buffers");
            request.goals.push_back(add(streamCapacities.at(name),number(1),buffers));
            request.goals.push_back(multiply(out.bytes,buffers,out.allocationBytes));
        }
        auto end=fresh("output extent"),maximum=fresh("occupied extent");
        request.goals.push_back(add(out.offset,out.allocationBytes,end));request.goals.push_back(lessEqual(end,maximumPayload));
        request.goals.push_back(call("maximum",{occupied,end,maximum}));occupied=maximum;
        starts.push_back(event(name+".begin"));produced.push_back(event(name+".produce"));released.push_back(event(name+".release"));
        before(starts.back(),produced.back());before(produced.back(),released.back());
        activation.live(name+" output",starts.back(),released.back(),out.allocationBytes,out.offset);
        request.observation.push_back(out.type);request.observation.push_back(out.value);
        request.realization.insert(request.realization.end(),{out.bits,out.bytes,out.offset,out.types,out.values,out.paths});
        request.minimize.push_back(out.preference);outputs.push_back(std::move(out));
    }
    for(std::size_t i=1;i<outputs.size();++i) {
        if(options.activation=="serial") before(released[i-1],starts[i]);
        else if(options.activation=="retained") before(produced[i-1],starts[i]);
    }
    for(const auto& [first,second]:options.accessOrder) {
        auto find=[&](const std::string& name) {
            auto found=std::find_if(outputs.begin(),outputs.end(),[&](const Output& item){return item.name==name;});
            if(found==outputs.end()) throw std::invalid_argument("access order names an unrequested source: "+name);
            return static_cast<std::size_t>(found-outputs.begin());
        };
        before(produced[find(first)],starts[find(second)]);
    }
    for(const auto& [first,second]:options.eventOrder) {
        auto find=[&](const std::string& name) {
            auto found=std::find(boundaryEventNames.begin(),boundaryEventNames.end(),name);
            if(found==boundaryEventNames.end()) throw std::invalid_argument("unknown activation event: "+name);
            return static_cast<Id>(found-boundaryEventNames.begin());
        };
        before(find(first),find(second));
    }
    if(options.activation=="overlap" || options.activation=="retained" || options.activation=="stream")
        for(std::size_t i=0;i<outputs.size();++i) for(std::size_t j=0;j<outputs.size();++j) before(produced[i],released[j]);
    else if(options.activation=="open") activation.closed=false;
    else if(options.activation!="serial" && options.activation!="graph") throw std::invalid_argument("unknown activation contract");
    // The legacy overlap profile retains its conservative automatic-frame
    // policy. Graph mode supplies explicit finite occurrence ownership.
    executionExclusive=activation.closed && activation.valid() && options.activation!="overlap";
    for(std::size_t i=0;i<outputs.size();++i) for(std::size_t j=0;j<i;++j)
        if(options.activation!="stream")
        if(!activation.precedes(produced[i],starts[j]) && !activation.precedes(produced[j],starts[i])) {
            executionExclusive=false;
            overlappingExecutions.emplace("vx_"+outputs[i].name,"vx_"+outputs[j].name);
        }
    boundaryClosed=activation.closed;
    if(options.activation=="overlap" || options.activation=="graph") later(accesses,[this,activation,starts,produced](const auto& resources){
        std::vector<std::map<std::string,bool>> uses;
        for(const auto& resource:resources) {
            std::map<std::string,bool> access;
            std::function<void(const Ref&)> visit=[&](const Ref& part){
                if(part->name=="Access") access[part->arguments[0]->name]=access[part->arguments[0]->name] || part->arguments[1]->number!=0;
                else for(const auto& child:part->arguments) visit(child);
            };visit(resource);uses.push_back(std::move(access));
        }
        auto aliases=[](const std::string& a,const std::string& b) {
            return a==b || (a=="external-world" && b.starts_with("external:")) || (b=="external-world" && a.starts_with("external:"));
        };
        for(std::size_t i=0;i<uses.size();++i) for(std::size_t j=0;j<i;++j) {
            if(activation.precedes(produced[i],starts[j]) || activation.precedes(produced[j],starts[i])) continue;
            for(const auto& [resource,write]:uses[i]) for(const auto& [other,otherWrite]:uses[j]) if(aliases(resource,other) && (write || otherWrite))
                pending("overlapping sources "+outputs[j].name+" and "+outputs[i].name+" need an access-order contract for "+resource);
        }
    });
    activation.constrain(request);
    // This output arena has byte alignment and no fixed-address constraints.
    // Its lifetime proof can supply a canonical minimum-extent realization
    // without enumerating equivalent placements. This is a target policy;
    // source observations and generic relation search are unchanged.
    bool exclusive=activation.closed && activation.valid(),simultaneous=exclusive;
    for(std::size_t i=0;i<activation.storage.size();++i) for(std::size_t j=0;j<i;++j) {
        auto overlap=activation.overlap(activation.storage[i],activation.storage[j]);
        exclusive=exclusive && overlap==ActivationGraph::Overlap::Excluded;
        simultaneous=simultaneous && overlap==ActivationGraph::Overlap::Admitted;
    }
    if(exclusive && options.activation!="stream") for(const auto& out:outputs) {
        equation(out.offset,number(0));
        // With exclusive, byte-aligned output lifetimes, each encoding has
        // only its own payload bound. Its preferred feasible encoding cannot
        // make another output infeasible. Prove that local physical choice
        // against the same relations; keep every original relation below in
        // the joint request. No source observation or value is selected here.
        later({out.type,maximumPayload},[this,out](const auto& types){
            const auto payload=types[1]->number.template convert_to<unsigned>();
            auto flat=leaves(types[0]);
            bool preferredFits=flat.size()<=payload/2 &&
                std::all_of(flat.begin(),flat.end(),[](const Leaf& leaf){return nativeScalar(leaf.type) && scalarBits(leaf.type)<=16;});
            if(preferredFits) {equation(out.bits,number(16));return;}
            later({out.types,out.values},[this,out,payload](const auto& values){
                Request proof;auto bits=fresh("exclusive output encoding"),unit=fresh("exclusive output unit"),preference=fresh("exclusive output preference");
                proof.goals.push_back(call("physical_encoding",{bits,unit,preference}));
                Ref count=number(0);
                if(values[0]->arguments.size()!=values[1]->arguments.size()) throw std::logic_error("output encoding interface mismatch");
                for(std::size_t i=0;i<values[0]->arguments.size();++i) {
                    auto type=values[0]->arguments[i],value=values[1]->arguments[i];
                    auto stored=fresh("exclusive stored leaf"),sum=fresh("exclusive stored count");
                    proof.goals.push_back(call("projectable_scalar",{type}));
                    proof.goals.push_back(call("physical_value",{type,bits,value}));
                    proof.goals.push_back(call("stored_count",{value,stored}));
                    proof.goals.push_back(add(count,stored,sum));count=sum;
                }
                auto bytes=fresh("exclusive output bytes");proof.goals.push_back(multiply(count,unit,bytes));
                proof.goals.push_back(lessEqual(bytes,number(payload)));
                proof.realization={bits};proof.minimize={preference};
                auto search=options.search;search.budget=searchBudget;auto checked=Machine(program,search).solve(proof);
                if(checked.outcome==Outcome::Exhausted) throw ConstructionLimit("exclusive encoding proof exhausted its search budget");
                if(checked.outcome==Outcome::Resolved) equation(out.bits,checked.selected->realization[0]);
                else if(checked.outcome==Outcome::Contradictory) conflict();
                else pending("exclusive output encoding proof remains open");
            });
        });
    }
    else if(simultaneous) {
        auto bytes=fresh("packed size"),cursor=fresh("packed cursor"),next=fresh("next cursor");
        program.define("physical_pack",4,{
            {{number(0),cursor,number(0),cursor},{},"empty result has no storage"},
            {{bytes,cursor,cursor,next},{lessEqual(number(1),bytes),add(cursor,bytes,next)},"live byte interval"}
        });
        Ref end=number(0);
        for(const auto& out:outputs) {
            auto after=fresh("packed output end");
            request.goals.push_back(call("physical_pack",{out.allocationBytes,end,out.offset,after}));end=after;
        }
    } else {
        later({maximumPayload},[this](const auto& values){
            auto maximum=values[0]->number.template convert_to<unsigned>();
            std::vector<Ref> offsets;for(unsigned i=0;i<=maximum;++i) offsets.push_back(number(i));
            auto name="physical_offset_"+std::to_string(maximum);program.domain(name,offsets);
            for(const auto& out:outputs) request.goals.push_back(call(name,{out.offset}));
        });
    }
    extent=occupied;
    request.realization.push_back(extent);request.minimize.push_back(extent);
    for(const auto& out:outputs) request.minimize.push_back(out.offset);
}

std::string cType(unsigned bits);
std::string cScalarType(const Ref& type);
std::string powerName(const Ref& type) {
    auto format=scalarFormat(type).value();
    return "vxm_power_"+type->name+"_"+std::to_string(format.bits)+"_"+std::to_string(format.fraction)+(format.sign ? "_signed" : "_unsigned");
}
std::string cExpression(const Ref& value) {
    if(value->kind==Term::Number) return value->number.str()+"UL";
    if(value->kind!=Term::Constructor) throw std::logic_error("unresolved C expression");
    if(value->name=="Runtime") return cExpression(value->arguments.at(0));
    if(value->name=="Input") return "vxp_"+value->arguments.at(0)->name;
    if(value->name=="FrameArg") return "vxa_"+show(value->arguments.at(0));
    if(value->name=="CaptureArg") return "vxc_"+show(value->arguments.at(0));
    if(value->name=="LoopIndex") return "VX_SNAPSHOT32(vxi_"+show(value->arguments.at(0))+")";
    if(value->name=="LoopValue") {
        const auto& type=value->arguments.at(2);auto width=nativeBytes(type)*8;
        auto load="VX_SNAPSHOT"+std::to_string(width)+"(vxl_"+show(value->arguments[0])+"_s"+show(value->arguments[1])+")";
        return scalarSigned(type) ? "(("+cScalarType(type)+")"+load+")" : load;
    }
    if(value->name=="LoopSlotValue") {
        auto type=value->arguments.at(3);
        auto load="VX_SNAPSHOT"+std::to_string(nativeBytes(type)*8)+"(vxl_"+show(value->arguments[0])+"_s"+
            show(value->arguments[1])+"_l"+show(value->arguments[2])+")";
        return scalarSigned(type) ? "(("+cScalarType(type)+")"+load+")" : load;
    }
    if(value->name=="LoopReady") return "VX_SNAPSHOT8(vxlq_"+show(value->arguments[0])+"_s"+show(value->arguments[1])+")";
    if(value->name=="LoopFlag" || value->name=="LoopReturned") return
        std::string(value->name=="LoopFlag" ? "VX_SNAPSHOT8(" : "VX_SNAPSHOT32(")+
        (value->name=="LoopFlag" ? "vxlf_" : "vxlr_")+show(value->arguments.at(0))+"["+show(value->arguments.at(1))+"])";
    if(value->name=="CallValue") return "VX_SNAPSHOT32(vxt_"+show(value->arguments.at(0))+"["+show(value->arguments.at(1))+"])";
    if(value->name=="After") return cExpression(value->arguments.at(1));
    if(value->name=="Index") {
        std::string result="(vx_trap(16), 0UL)";const auto& choices=value->arguments[1]->arguments;
        for(std::size_t i=choices.size();i-->0;) result="("+cExpression(value->arguments[0])+" == "+std::to_string(i)+"UL ? "+cExpression(choices[i])+" : "+result+")";
        return result;
    }
    if(value->name=="Not") return "(!("+cExpression(value->arguments.at(0))+"))";
    if(value->name=="And") return "("+cExpression(value->arguments.at(0))+" && "+cExpression(value->arguments.at(1))+")";
    if(value->name=="Select") return "("+cExpression(value->arguments.at(0))+" ? "+cExpression(value->arguments.at(1))+" : "+cExpression(value->arguments.at(2))+")";
    if(value->name=="Compare") return "((uint32_t)("+cExpression(value->arguments.at(1))+") "+value->arguments.at(0)->name+" (uint32_t)("+cExpression(value->arguments.at(2))+"))";
    if(value->name=="Math") {
        auto type=value->arguments[0];auto format=scalarFormat(type).value();auto bits=std::to_string(format.bits);
        unsigned op=value->arguments[1]->number.convert_to<unsigned>();
        auto a=cExpression(value->arguments[2]),b=cExpression(value->arguments[3]);std::string expression;
        if(op==16) expression=powerName(type)+"((uint32_t)("+a+"), (uint32_t)("+b+"))";
        else if((op==5 || op==6) && format.kind!=ScalarFormat::Float) {
            if(format.sign) {a="vx_signed("+a+", "+bits+")";b="vx_signed("+b+", "+bits+")";}
            expression="("+a+(op==5 ? " < " : " >= ")+b+" ? "+a+" : "+b+")";
        } else {
            if(op==12) b=format.sign ? "1" : "0";
            else if(format.kind==ScalarFormat::Fixed) {op+=128;b=std::to_string(format.fraction+(format.sign ? 256 : 0));}
            expression="vx_math((uint32_t)("+a+"), (uint32_t)("+b+"), "+bits+", "+std::to_string(op)+")";
        }
        return format.sign ? "vx_signed("+expression+", "+bits+")" : expression;
    }
    if(value->name=="RepresentationPart") return "(((uint32_t)("+cExpression(value->arguments[0])+") >> "+show(value->arguments[1])+") & "+show(value->arguments[2])+"UL)";
    if(value->name=="RepresentationJoin") {
        auto format=scalarFormat(value->arguments[0]).value();auto unit=value->arguments[1]->number.convert_to<unsigned>();
        std::string bits="0UL";const auto& parts=value->arguments[2]->arguments;
        for(std::size_t i=0;i<parts.size();++i) bits+=" | ((uint32_t)("+cExpression(parts[i])+") << "+std::to_string(i*unit)+")";
        bits="("+bits+")";
        if(format.kind==ScalarFormat::Bool) return "("+bits+" <= 1UL ? "+bits+" : (vx_trap(16), 0UL))";
        if(format.sign) return "vx_signed("+bits+", "+std::to_string(format.bits)+")";
        return "("+bits+" & "+Integer((Integer(1)<<format.bits)-1).str()+"UL)";
    }
    if(value->name=="FloatBinary" || value->name=="FixedBinary" || value->name=="NumericUnary" || value->name=="NumericCast") {
        auto format=scalarFormat(value->arguments[0]).value();
        auto wrap=[&](const ScalarFormat& f,const std::string& expression) {
            return f.sign ? "vx_signed("+expression+", "+std::to_string(f.bits)+")" : "((uint32_t)("+expression+") & "+Integer((Integer(1)<<f.bits)-1).str()+"UL)";
        };
        if(value->name=="NumericCast") {
            auto target=scalarFormat(value->arguments[1]).value();auto operand=cExpression(value->arguments[2]);
            if(format.kind==ScalarFormat::Float || target.kind==ScalarFormat::Float) {
                auto descriptor=[](const ScalarFormat& f) {
                    return std::to_string(f.bits)+", "+std::to_string(f.kind==ScalarFormat::Float ? 2 : f.kind==ScalarFormat::Bool ? 3 : f.sign ? 1 : 0)+", "+std::to_string(f.fraction);
                };
                return wrap(target,"vx_float_convert("+operand+", "+descriptor(format)+", "+descriptor(target)+")");
            }
            if(target.kind==ScalarFormat::Bool) return "((uint32_t)("+operand+") != 0UL)";
            return wrap(target,"vx_rescale("+operand+", "+std::to_string(format.bits)+", "+(format.sign ? "1" : "0")+", "+std::to_string(format.fraction)+", "+std::to_string(target.fraction)+")");
        }
        const auto& op=value->arguments[1]->name;auto a=cExpression(value->arguments[2]);
        if(value->name=="NumericUnary") {
            if(format.kind==ScalarFormat::Float) {
                auto sign=Integer(Integer(1)<<(format.bits-1)).str();
                return op=="-" ? "((uint32_t)("+a+") ^ "+sign+"UL)" : op=="|" ? "((uint32_t)("+a+") & "+Integer((Integer(1)<<(format.bits-1))-1).str()+"UL)" : a;
            }
            if(op=="|") return format.sign ? wrap(format,"(vx_signed("+a+", "+std::to_string(format.bits)+") < 0 ? 0UL-(uint32_t)("+a+") : (uint32_t)("+a+"))") : a;
            return wrap(format,op=="~" ? "~(uint32_t)("+a+")" : "0UL-(uint32_t)("+a+")");
        }
        auto b=cExpression(value->arguments[3]);auto bits=std::to_string(format.bits);
        if(value->name=="FloatBinary") {
            const std::map<std::string,unsigned> codes{{"+",0},{"-",1},{"*",2},{"/",3},{"%",4},{"==",5},{"!=",6},{"<",7},{"<=",8},{">",9},{">=",10}};
            return "vx_float_binary("+a+", "+b+", "+bits+", "+std::to_string(codes.at(op))+")";
        }
        auto sign=format.sign ? "1" : "0";std::string expression;
        if(op=="*" || op=="/") expression="vx_fixed_"+std::string(op=="*" ? "mul" : "div")+"("+a+", "+b+", "+bits+", "+sign+", "+std::to_string(format.fraction)+")";
        else if(op=="%") expression="vx_divmod("+a+", "+b+", "+bits+", "+sign+", 1)";
        else if(op=="<<" || op==">>") expression="vx_shift("+a+", "+b+", "+bits+", "+(op==">>" ? "1" : "0")+", "+sign+")";
        else expression="((uint32_t)("+a+") "+op+" (uint32_t)("+b+"))";
        return wrap(format,expression);
    }
    if(value->name=="SignedCompare") return "(vx_signed("+cExpression(value->arguments[2])+", "+show(value->arguments[1])+") "+value->arguments[0]->name+" vx_signed("+cExpression(value->arguments[3])+", "+show(value->arguments[1])+"))";
    if(value->name=="SignedCast") return "vx_signed("+cExpression(value->arguments[1])+", "+show(value->arguments[0])+")";
    if(value->name=="SignedAbsolute") {
        auto operand=cExpression(value->arguments[1]),bits=show(value->arguments[0]);
        return "vx_signed((vx_signed("+operand+", "+bits+") < 0 ? 0UL - (uint32_t)("+operand+") : (uint32_t)("+operand+")), "+bits+")";
    }
    if(value->name=="SignedArithmetic") {
        const auto& op=value->arguments[0]->name;auto bits=show(value->arguments[1]);
        auto a=cExpression(value->arguments[2]),b=cExpression(value->arguments[3]);std::string result;
        if(op=="/" || op=="%") result="vx_divmod("+a+", "+b+", "+bits+", 1, "+(op=="%" ? "1" : "0")+")";
        else if(op=="<<" || op==">>") result="vx_shift("+a+", "+b+", "+bits+", "+(op==">>" ? "1" : "0")+", 1)";
        else if(op=="*") result="vx_mul("+a+", "+b+")";
        else result="((uint32_t)("+a+") "+op+" (uint32_t)("+b+"))";
        return "vx_signed("+result+", "+bits+")";
    }
    if(value->name=="ExactArithmetic") {
        const auto& lo=value->arguments[1]->number;const auto& hi=value->arguments[2]->number;
        if(lo<0 || hi>((Integer(1)<<32)-1)) throw std::runtime_error("exact runtime arithmetic has no supported C representation");
        return "((uint32_t)("+cExpression(value->arguments[3])+") "+value->arguments[0]->name+" (uint32_t)("+cExpression(value->arguments[4])+"))";
    }
    if(value->name=="Cast" || value->name=="Arithmetic") {
        bool cast=value->name=="Cast";auto at=cast ? 0u : 1u;
        unsigned bits=value->arguments.at(at)->number.convert_to<unsigned>();
        std::string mask=Integer((Integer(1)<<bits)-1).str()+"UL";
        std::string expression="(uint32_t)("+cExpression(value->arguments.at(at+1))+")";
        if(!cast) {
            const auto& op=value->arguments[0]->name;auto a=cExpression(value->arguments[2]),b=cExpression(value->arguments[3]);
            if(op=="/" || op=="%") return "vx_divmod("+a+", "+b+", "+std::to_string(bits)+", 0, "+(op=="%" ? "1" : "0")+")";
            if(op=="<<" || op==">>") return "vx_shift("+a+", "+b+", "+std::to_string(bits)+", "+(op==">>" ? "1" : "0")+", 0)";
            if(op=="*") return "(vx_mul("+a+", "+b+") & "+mask+")";
            expression+=" "+op+" (uint32_t)("+b+")";
        }
        return "(("+expression+") & "+mask+")";
    }
    throw std::logic_error("unsupported residual C expression: "+show(value));
}
std::string cType(unsigned bits) {return bits<=8 ? "uint8_t" : bits<=16 ? "uint16_t" : "uint32_t";}
std::string cScalarType(const Ref& type) {
    auto name=cType(scalarBits(type));
    return scalarSigned(type) ? name.substr(1) : name;
}
std::size_t cTemporaries(std::ostream& code,const Ref& trace,bool staticFrame=false) {
    std::map<std::string,std::size_t> temporaries;
    std::size_t bytes=0;const auto storage=staticFrame ? "static " : "";
    std::function<void(const Ref&)> visit=[&](const Ref& item) {
        if(item->name=="Invoke") temporaries.emplace(show(item->arguments[0]),item->arguments[4]->number.convert_to<std::size_t>());
        else if(item->name=="Load" || item->name=="ExternalCall") temporaries.emplace(show(item->arguments[0]),item->arguments.back()->number.convert_to<std::size_t>());
        else if(item->name=="Loop") {
            auto id=show(item->arguments[0]);const auto& types=item->arguments.at(10)->arguments;
            std::size_t at=0;
            for(const auto& slot:item->arguments.at(11)->arguments) {
                auto root=show(slot->arguments[0]);auto count=slot->arguments[1]->number.convert_to<std::size_t>();
                for(std::size_t i=0;i<count;++i) {
                    auto suffix=id+"_s"+root+"_l"+std::to_string(i);
                    code<<"    "<<storage<<cScalarType(types.at(at))<<" vxl_"<<suffix<<", vxln_"<<suffix<<";\n";
                    bytes+=2*nativeBytes(types[at++]);
                }
                code<<"    "<<storage<<"uint8_t vxlq_"<<id<<"_s"<<root<<", vxlqn_"<<id<<"_s"<<root<<";\n";bytes+=2;
            }
            code<<"    "<<storage<<"uint32_t vxi_"<<id<<";\n";bytes+=4;
            auto count=item->arguments[8]->arguments.size();std::size_t resultSize=0;
            for(const auto& result:item->arguments[8]->arguments) resultSize+=result->arguments[1]->arguments.size();
            if(count) code<<"    "<<storage<<"uint8_t vxlf_"<<id<<"["<<count<<"];\n";
            if(resultSize) code<<"    "<<storage<<"uint32_t vxlr_"<<id<<"["<<resultSize<<"];\n";
            bytes+=count+4*resultSize;
            visit(item->arguments[2]);visit(item->arguments[5]);
        }
        else if(item->name=="Sequence") for(const auto& part:item->arguments) visit(part);
        else if(item->name=="If") {visit(item->arguments[1]);visit(item->arguments[2]);}
    };
    visit(trace);
    for(const auto& [name,size]:temporaries) {code<<"    "<<storage<<"uint32_t vxt_"<<name<<"["<<size<<"];\n";bytes+=4*size;}
    return bytes;
}
void cExecution(std::ostream& code,const Ref& trace,const std::string& indent="    ") {
    if(trace->name=="Done") return;
    if(trace->name=="Load") {code<<indent<<"vx_load_"<<show(trace->arguments[1])<<"(vxt_"<<show(trace->arguments[0])<<");\n";return;}
    if(trace->name=="Store") {
        code<<indent<<"vx_store_"<<show(trace->arguments[0])<<'(';bool first=true;
        std::size_t i=0;
        for(const auto& leaf:leaves(trace->arguments[1],trace->arguments[2])) {
            if(!first) code<<", ";
            first=false;
            code<<(trace->arguments.size()==4 ? cExpression(trace->arguments[3]->arguments[i]) : "1")<<", "<<cExpression(leaf.value);++i;
        }
        code<<");\n";return;
    }
    if(trace->name=="ExternalCall") {
        code<<indent<<"vx_invoke_external_"<<trace->arguments[1]->name<<"(vxt_"<<show(trace->arguments[0]);
        std::function<void(const Ref&)> argument=[&](const Ref& value){
            if(value->name=="Values") for(const auto& item:value->arguments) argument(item);
            else if(value->name=="RecordValue") argument(value->arguments[1]);
            else code<<", "<<cExpression(value);
        };
        argument(trace->arguments[2]);code<<");\n";return;
    }
    if(trace->name=="Evaluate") {code<<indent<<"(void)("<<cExpression(trace->arguments[0])<<");\n";return;}
    if(trace->name=="CheckIndex") {
        code<<indent<<"if ((uint32_t)("<<cExpression(trace->arguments[0])<<") >= "<<show(trace->arguments[1])<<"UL) vx_trap(16);\n";return;
    }
    if(trace->name=="CheckSchema") {code<<indent<<"if (!("<<cExpression(trace->arguments[0])<<")) vx_trap(19);\n";return;}
    if(trace->name=="Sequence") {for(const auto& part:trace->arguments) cExecution(code,part,indent);return;}
    if(trace->name=="If") {
        code<<indent<<"if ("<<cExpression(trace->arguments[0])<<") {\n";
        cExecution(code,trace->arguments[1],indent+"    ");code<<indent<<"} else {\n";
        cExecution(code,trace->arguments[2],indent+"    ");code<<indent<<"}\n";return;
    }
    if(trace->name=="Loop") {
        auto id=show(trace->arguments[0]);
        std::function<void(const Ref&,std::vector<Ref>&)> flatten=[&](const Ref& value,std::vector<Ref>& out){
            if(value->name=="Values") for(const auto& item:value->arguments) flatten(item,out);
            else if(value->name=="RecordValue") flatten(value->arguments[1],out);
            else out.push_back(value);
        };
        const auto& slots=trace->arguments.at(11)->arguments;
        auto update=[&](const Ref& values,const Ref& readiness,const std::string& pad){
            std::vector<Ref> flat;flatten(values,flat);std::size_t at=0;
            for(std::size_t s=0;s<slots.size();++s) {
                auto root=id+"_s"+show(slots[s]->arguments[0]);
                code<<pad<<"vxlqn_"<<root<<" = "<<cExpression(readiness->arguments.at(s))<<";\n";
                auto count=slots[s]->arguments[1]->number.convert_to<std::size_t>();
                for(std::size_t i=0;i<count;++i)
                    code<<pad<<"vxln_"<<root<<"_l"<<i<<" = vxlqn_"<<root<<" ? ("<<cExpression(flat.at(at++))<<") : 0;\n";
            }
            for(const auto& slot:slots) {
                auto root=id+"_s"+show(slot->arguments[0]);auto count=slot->arguments[1]->number.convert_to<std::size_t>();
                for(std::size_t i=0;i<count;++i) code<<pad<<"vxl_"<<root<<"_l"<<i<<" = vxln_"<<root<<"_l"<<i<<";\n";
                code<<pad<<"vxlq_"<<root<<" = vxlqn_"<<root<<";\n";
            }
        };
        update(trace->arguments[1],trace->arguments[12],indent);
        const auto& returns=trace->arguments[8]->arguments;
        for(std::size_t i=0;i<returns.size();++i) code<<indent<<"vxlf_"<<id<<"["<<i<<"] = 0;\n";
        code<<indent<<"vxi_"<<id<<" = 0;\n"<<indent<<"for (;;) {\n";
        auto pad=indent+"    ";cExecution(code,trace->arguments[2],pad);
        code<<pad<<"if (!("<<cExpression(trace->arguments[3])<<")) {\n";
        std::size_t checkOffset=0;
        for(std::size_t i=0;i<returns.size();++i) {
            const auto& values=returns[i]->arguments[1]->arguments;
            if(returns[i]->arguments[2]->number!=0) {
                code<<pad<<"    if ("<<cExpression(returns[i]->arguments[0])<<") {\n"<<pad<<"        vxlf_"<<id<<"["<<i<<"] = 1;\n";
                for(std::size_t j=0;j<values.size();++j) code<<pad<<"        vxlr_"<<id<<"["<<checkOffset+j<<"] = "<<cExpression(values[j])<<";\n";
                code<<pad<<"    }\n";
            }
            checkOffset+=values.size();
        }
        code<<pad<<"    if ("<<cExpression(trace->arguments[9])<<") {\n";
        update(trace->arguments[4],trace->arguments[13],pad+"        ");code<<pad<<"        continue;\n"<<pad<<"    }\n";
        update(trace->arguments[4],trace->arguments[13],pad+"    ");
        code<<pad<<"    break;\n"<<pad<<"}\n";
        cExecution(code,trace->arguments[5],pad);std::size_t offset=0;
        for(std::size_t i=0;i<returns.size();++i) {
            code<<pad<<"if ("<<cExpression(returns[i]->arguments[0])<<") {\n"<<pad<<"    vxlf_"<<id<<"["<<i<<"] = 1;\n";
            for(const auto& value:returns[i]->arguments[1]->arguments) code<<pad<<"    vxlr_"<<id<<"["<<offset++<<"] = "<<cExpression(value)<<";\n";
            code<<pad<<"}\n";
        }
        // Test the exit before overwriting carried values used by its guard.
        code<<pad<<"if ("<<cExpression(trace->arguments[7])<<") {\n";
        update(trace->arguments[6],trace->arguments[14],pad+"    ");code<<pad<<"    break;\n"<<pad<<"}\n";
        update(trace->arguments[6],trace->arguments[14],pad);code<<pad<<"++vxi_"<<id<<";\n"<<indent<<"}\n";return;
    }
    if(trace->name=="Invoke") {
        code<<indent<<"vx_flow_"<<show(trace->arguments[1])<<"(vxt_"<<show(trace->arguments[0]);
        std::function<void(const Ref&)> argument=[&](const Ref& value){
            if(value->name=="Values") for(const auto& item:value->arguments) argument(item);
            else if(value->name=="RecordValue") argument(value->arguments[1]);
            else code<<", "<<cExpression(value);
        };
        for(std::size_t i=2;i<4;++i) argument(trace->arguments[i]);
        code<<");\n";return;
    }
    throw std::logic_error("unclosed execution projection: "+show(trace));
}
#include "source_boundary_project.inc"
#include "source_banks.inc"
#include "source_frame_plan.inc"
#include "source_streams.inc"
void Session::project(SourceResult& answer,const Model& model,std::size_t observationCount) {
    std::ostringstream header,code,report;
    BankProjection banks;
    FramePlanning framesPlan;
    unsigned mapperBytes=0;
    // Stable declaration identities determine physical offsets; obligation
    // discovery order never determines ownership.
    std::map<std::string,Id> mapperOrder;
    for(const auto& [id,size]:mapperSizes) {
        (void)size;mapperOrder.emplace(graph.nodes[cells[id].site].identity,id);
    }
    for(const auto& [identity,id]:mapperOrder) {
        (void)identity;const auto bytes=model.witnesses.at("mapper_"+std::to_string(id))->number.convert_to<unsigned>();
        if(bytes) {mapperOffsets.emplace(id,mapperBytes);mapperBytes+=bytes;}
    }
    for(const auto& [name,contract]:options.boundaries) if(contract.noFeedback) framesPlan.noFeedback.insert(name);
    for(const auto& [name,artifact]:model.witnesses) if(name.starts_with("storage_") && artifact->arguments[3]->name=="Lazy")
        framesPlan.initializers.emplace(artifact->arguments[0]->number.convert_to<Id>(),"vx_ensure_"+show(artifact->arguments[0]));
    for(const auto& [name,artifact]:model.witnesses) if(name.starts_with("storage_") && artifact->arguments[3]->name=="Lazy")
        // The resident initializer emitter retains automatic temporaries. Its
        // real calls still participate in ownership, recursion and feedback.
        framesPlan.add("vx_ensure_"+show(artifact->arguments[0]),artifact->arguments[4],0,false);
    for(const auto& [name,artifact]:model.witnesses) if(name.starts_with("runtime_frame_"))
        framesPlan.add("vx_flow_"+show(artifact->arguments[0]),artifact->arguments[4]);
    for(std::size_t i=0;i<outputs.size();++i) {
        const auto& output=outputs[i];auto trace=model.witnesses.at("source_checks_"+std::to_string(output.cell));
        if(auto executed=model.witnesses.find("execution_"+std::to_string(output.cell));executed!=model.witnesses.end())
            trace=terms().make("Sequence",{trace,executed->second});
        std::size_t snapshots=0;
        if(model.realization[i*6]->number>8) for(const auto& leaf:model.realization[i*6+4]->arguments) if(leaf->kind!=Term::Number) snapshots+=4;
        framesPlan.add("vx_"+output.name,trace,snapshots);
        framesPlan.roots.insert("vx_"+output.name);
    }
    framesPlan.close(executionExclusive,options.activation=="graph",overlappingExecutions,configured(model,"frame_ram_bytes"));
    header<<"#ifndef VX_RELATIONAL_SOURCE_H\n#define VX_RELATIONAL_SOURCE_H\n#include \"vx_runtime.h\"\n";
    code<<"/* Generated from relational source obligations and a closed activation contract. */\n#include \"vexel.h\"\n";
    if(!options.streams.empty()) code<<"static uint8_t vx_stream_running;\nstatic uint32_t vx_stream_sequence;\n";
    // SDCC 4.2/4.5/4.6 can clobber a still-live byte operand in A while
    // narrowing related expressions (e.g. save(x,x+1,x+2)). Keep residual
    // roots in their automatic storage across arithmetic. These volatile
    // accesses are a C realization constraint, not source boundary effects.
    // Cast only the read lvalue: caller-owned result buffers retain their ABI.
    header<<"/* Preserve residual roots across SDCC byte arithmetic; see docs/sdcc-operands.md. */\n"
        <<"#ifdef __SDCC\n#define VX_STABLE volatile\n"
        <<"#define VX_SNAPSHOT8(x) (*(volatile uint8_t *)&(x))\n"
        <<"#define VX_SNAPSHOT16(x) (*(volatile uint16_t *)&(x))\n"
        <<"#define VX_SNAPSHOT32(x) (*(volatile uint32_t *)&(x))\n"
        <<"#else\n#define VX_STABLE\n#define VX_SNAPSHOT8(x) (x)\n#define VX_SNAPSHOT16(x) (x)\n#define VX_SNAPSHOT32(x) (x)\n#endif\n";
    // Z80 SDCC and the host C ABI already use automatic parameter/local storage.
    // SDCC's __reentrant attribute belongs to other target ports, not Z80.
    code<<"/* Local storage follows the per-function plan; ABI parameters remain automatic. */\n";
    std::map<std::string,Ref> powers;std::set<const Term*> visitedMath;
    std::function<void(const Ref&)> collectMath=[&](const Ref& value){
        if(!visitedMath.insert(value.get()).second) return;
        if(value->name=="Math" && value->arguments[1]->number==16) powers.emplace(powerName(value->arguments[0]),value->arguments[0]);
        for(const auto& child:value->arguments) collectMath(child);
    };
    for(const auto& value:model.observation) collectMath(value);
    for(const auto& value:model.realization) collectMath(value);
    for(const auto& [name,value]:model.witnesses) {(void)name;collectMath(value);}
    for(const auto& [name,type]:powers) {
        auto format=scalarFormat(type).value();Integer mask=(Integer(1)<<format.bits)-1,one=(Integer(1)<<format.fraction)&mask;
        if(format.kind==ScalarFormat::Float) {FloatParts seed;seed.numerator=1;seed.denominator=1;one=joinFloat(seed,format.bits);}
        auto multiply=[&](const std::string& left,const std::string& right) {
            auto args=left+", "+right+", "+std::to_string(format.bits);
            if(format.kind==ScalarFormat::Float) return "vx_float_binary("+args+", 2)";
            if(format.kind==ScalarFormat::Fixed) return "vx_fixed_mul("+args+", "+(format.sign ? "1" : "0")+", "+std::to_string(format.fraction)+")";
            return "(vx_mul("+left+", "+right+") & "+mask.str()+"UL)";
        };
        auto begin=static_cast<std::size_t>(code.tellp());
        header<<"uint32_t "<<name<<"(VX_STABLE uint32_t base, VX_STABLE uint32_t power) VX_ABI VX_RESIDENT;\n";
        code<<"uint32_t "<<name<<"(VX_STABLE uint32_t base, VX_STABLE uint32_t power) VX_ABI VX_RESIDENT {\n"
            <<"    VX_STABLE uint32_t result = "<<one<<"UL;\n    while (power) {\n"
            <<"        if (power & 1) result = "<<multiply("result","base")<<";\n"
            <<"        power >>= 1;\n        if (power) base = "<<multiply("base","base")<<";\n    }\n    return result;\n}\n";
        banks.cut(code,begin,name,"vxp_"+name,"base, power");
    }
    auto framePrototype=[](const Ref& artifact) {
        std::string text="void vx_flow_"+show(artifact->arguments[0])+"(uint32_t *vxo";
        for(std::size_t kind=1;kind<=2;++kind) {
            std::size_t offset=0;
            for(std::size_t i=0;i<artifact->arguments[kind]->arguments.size();++i) {
                if(kind==2 && artifact->arguments[7]->arguments[i]->number==0) continue;
                for(const auto& leaf:leaves(artifact->arguments[kind]->arguments[i])) if(nativeScalar(leaf.type))
                    text+=", VX_STABLE "+cScalarType(leaf.type)+(kind==1 ? " vxa_" : " vxc_")+std::to_string(offset++);
            }
        }
        return text+") VX_ABI VX_RESIDENT";
    };
    for(const auto& [name,artifact]:model.witnesses) if(name.starts_with("runtime_frame_")) header<<framePrototype(artifact)<<";\n";
    projectBoundary(model,header,code);
    for(const auto& [name,artifact]:model.witnesses) if(name.starts_with("runtime_frame_")) {
        auto begin=static_cast<std::size_t>(code.tellp());
        auto symbol="vx_flow_"+show(artifact->arguments[0]);
        code<<framePrototype(artifact)<<" {\n";cTemporaries(code,artifact->arguments[4],framesPlan.shared(symbol));cExecution(code,artifact->arguments[4]);
        auto resultLeaves=leaves(artifact->arguments[3],artifact->arguments[5]);std::size_t offset=0;
        for(const auto& leaf:resultLeaves) if(nativeScalar(leaf.type)) code<<"    vxo["<<offset++<<"] = "<<cExpression(leaf.value)<<";\n";
        if(offset==0) code<<"    vxo["<<offset++<<"] = 0UL;\n";
        for(std::size_t i=0;i<artifact->arguments[6]->arguments.size();++i) if(artifact->arguments[8]->arguments[i]->number!=0)
            for(const auto& leaf:leaves(artifact->arguments[2]->arguments[i],artifact->arguments[6]->arguments[i])) if(nativeScalar(leaf.type))
                code<<"    vxo["<<offset++<<"] = "<<cExpression(leaf.value)<<";\n";
        code<<"}\n";
        std::string arguments="vxo";
        for(std::size_t kind=1;kind<=2;++kind) {
            std::size_t index=0;
            for(std::size_t i=0;i<artifact->arguments[kind]->arguments.size();++i) {
                if(kind==2 && artifact->arguments[7]->arguments[i]->number==0) continue;
                for(const auto& leaf:leaves(artifact->arguments[kind]->arguments[i])) {
                    if(nativeScalar(leaf.type)) arguments+=std::string(kind==1 ? ", vxa_" : ", vxc_")+std::to_string(index++);
                }
            }
        }
        banks.cut(code,begin,symbol,"vxp_flow_"+show(artifact->arguments[0]),arguments,framesPlan.policy(symbol));
    }
    auto size=model.realization.back()->number.convert_to<unsigned>();
    if(size) {header<<"extern uint8_t vx_payload["<<size<<"];\n";code<<"uint8_t vx_payload["<<size<<"];\n";}
    report<<",\"activation\":"<<vx::quote(options.activation)<<",\"payload_budget\":"<<configured(model,"payload_bytes")<<",\"extent\":"<<size<<",\"sources\":[";
    for(std::size_t i=0;i<outputs.size();++i) {
        const auto& output=outputs[i];std::size_t at=i*6;
        unsigned bits=model.realization[at]->number.convert_to<unsigned>();
        unsigned bytes=model.realization[at+1]->number.convert_to<unsigned>();
        unsigned offset=model.realization[at+2]->number.convert_to<unsigned>();
        const auto& types=model.realization[at+3]->arguments;const auto& values=model.realization[at+4]->arguments;
        std::vector<Ref> inputParameters,inputLeaves;
        for(Id id:output.parameters) {
            auto input=model.witnesses.at("source_input_"+std::to_string(id));inputParameters.push_back(input);
            for(const auto& leaf:input->arguments[2]->arguments) inputLeaves.push_back(leaf);
        }
        std::string parameters,stableParameters;
        for(const auto& leaf:inputLeaves) {
            const auto& input=leaf->arguments;
            if(!parameters.empty()) {parameters+=",";stableParameters+=",";}
            parameters+=cScalarType(input[2])+" vxp_"+input[0]->name;
            stableParameters+="VX_STABLE "+cScalarType(input[2])+" vxp_"+input[0]->name;
        }
        if(parameters.empty()) parameters=stableParameters="void";
        std::string prototype="void vx_"+output.name+"("+parameters+") VX_ABI VX_RESIDENT";
        header<<prototype<<";\nuint32_t vx_"<<output.name<<"_read(uint16_t index) VX_ABI VX_RESIDENT;\n";
        auto begin=static_cast<std::size_t>(code.tellp());
        code<<"void vx_"<<output.name<<"("<<stableParameters<<") VX_ABI VX_RESIDENT {\n";
        auto execution=model.witnesses.find("execution_"+std::to_string(output.cell));
        const auto& inputChecks=model.witnesses.at("source_checks_"+std::to_string(output.cell));
        bool staticLocals=framesPlan.shared("vx_"+output.name);
        cTemporaries(code,framesPlan.nodes.at("vx_"+output.name).trace,staticLocals);
        for(const auto& leaf:inputLeaves) {
            const auto& input=leaf->arguments;auto width=scalarBits(input[2]);bool sign=scalarSigned(input[2]);
            const auto& minimum=input[3]->number;const auto& maximum=input[4]->number;
            unsigned abiWidth=width<=8 ? 8u : width<=16 ? 16u : 32u;
            Integer lower=sign ? -(Integer(1)<<(abiWidth-1)) : Integer(0),upper=(Integer(1)<<(abiWidth-(sign ? 1 : 0)))-1;
            if(minimum==lower && maximum==upper) continue;
            code<<"    if (";
            if(minimum>lower) code<<"vxp_"<<input[0]->name<<" < "<<minimum<<(sign ? "L" : "UL")<<" || ";
            code<<"vxp_"<<input[0]->name<<" > "<<maximum<<(sign ? "L" : "UL")<<") vx_trap(16);\n";
        }
        cExecution(code,inputChecks);
        if(execution!=model.witnesses.end()) cExecution(code,execution->second);
        std::vector<unsigned> locations;unsigned used=0;
        for(const auto& value:values) {
            locations.push_back(offset+used);
            if(value->kind==Term::Number) continue;
            std::string expression=cExpression(value);
            if(bits>8) {
                // A logical leaf is evaluated once. Serializing its bytes must
                // not duplicate a residual computation, including numeric
                // helper calls and bounds checks, in every stored byte.
                auto local="vxo_"+std::to_string(locations.size()-1);
                if(staticLocals) code<<"    static VX_STABLE uint32_t "<<local<<";\n    "<<local<<" = (uint32_t)("<<expression<<");\n";
                else code<<"    VX_STABLE uint32_t "<<local<<" = (uint32_t)("<<expression<<");\n";
                expression="VX_SNAPSHOT32("+local+")";
            }
            code<<"    vx_payload["<<offset+used<<"] = (uint8_t)("<<expression<<");\n";
            for(unsigned byte=1;byte<bits/8;++byte)
                code<<"    vx_payload["<<offset+used+byte<<"] = (uint8_t)(((uint32_t)("<<expression<<")) >> "<<8*byte<<");\n";
            used+=bits/8;
        }
        if(used!=bytes) throw std::logic_error("projection disagrees with selected byte extent");
        code<<"}\n";
        std::string arguments;
        for(const auto& leaf:inputLeaves) {if(!arguments.empty()) arguments+=", ";arguments+="vxp_"+leaf->arguments[0]->name;}
        banks.cut(code,begin,"vx_"+output.name,"vxp_source_"+output.name,arguments,framesPlan.policy("vx_"+output.name));
        const bool stream=options.streams.contains(output.name);
        code<<(stream ? "static " : "")<<"uint32_t vx_"<<output.name<<(stream ? "_read_at(uint16_t index, uint16_t shift)" : "_read(uint16_t index)")
            <<" VX_ABI VX_RESIDENT {\n    switch(index) {\n";
        for(std::size_t j=0;j<values.size();++j) {
            code<<"    case "<<j<<": return ";
            if(values[j]->kind==Term::Number) code<<values[j]->number<<"UL";
            else {
                if(scalarSigned(types[j])) code<<"(uint32_t)vx_signed(";
                code<<"(uint32_t)vx_payload["<<locations[j]<<(stream ? " + shift" : "")<<"]";
                for(unsigned byte=1;byte<bits/8;++byte) code<<" + ((uint32_t)vx_payload["<<locations[j]+byte<<(stream ? " + shift" : "")<<"] << "<<8*byte<<")";
                if(scalarSigned(types[j])) code<<", "<<bits<<")";
            }
            code<<";\n";
        }
        code<<"    default: vx_trap(16); return 0;\n    }\n}\n";
        unsigned streamSlots=stream ? model.witnesses.at("stream_"+output.name)->number.convert_to<unsigned>() : 0;
        if(stream) projectStream(output,parameters,arguments,offset,bytes,streamSlots,header,code);
        if(i) report<<',';
        report<<"{\"name\":"<<vx::quote(output.name)<<",\"source\":"<<vx::quote(output.source)<<",\"logical_type\":"<<vx::quote(typeText(model.observation[observationCount+i*2]))
              <<",\"element_bits\":"<<bits<<",\"bytes\":"<<bytes<<",\"offset\":"<<offset<<",\"parameters\":[";
        for(std::size_t j=0;j<output.parameters.size();++j) {
            const auto& input=cells[output.parameters[j]];if(j) report<<',';
            const auto& logical=inputParameters[j]->arguments;const auto& flat=logical[2]->arguments;
            report<<"{\"name\":"<<vx::quote(input.inputName)<<",\"logical_type\":"<<vx::quote(typeText(logical[0]));
            if(!aggregate(logical[0]) && flat.size()==1)
                report<<",\"min\":"<<flat[0]->arguments[3]->number<<",\"max\":"<<flat[0]->arguments[4]->number;
            report<<",\"leaves\":[";
            for(std::size_t k=0;k<flat.size();++k) {
                const auto& leaf=flat[k]->arguments;if(k) report<<',';
                report<<"{\"parameter\":"<<vx::quote("vxp_"+leaf[0]->name)<<",\"path\":"<<vx::quote(leaf[1]->name)
                      <<",\"logical_type\":"<<vx::quote(typeText(leaf[2]))<<",\"min\":"<<leaf[3]->number<<",\"max\":"<<leaf[4]->number<<"}";
            }
            report<<"]}";
        }
        report<<"],\"leaves\":[";
        for(std::size_t j=0;j<values.size();++j) {
            if(j) report<<',';
            report<<"{\"path\":"<<vx::quote(model.realization[at+5]->arguments[j]->name)<<",\"logical_type\":"<<vx::quote(typeText(types[j]))<<",\"stored\":"<<(values[j]->kind==Term::Number ? "false" : "true")
                  <<",\"value\":"<<vx::quote(show(values[j]))<<"}";
        }
        report<<']';
        if(stream) report<<",\"stream\":{\"slots\":"<<streamSlots<<",\"metadata_bytes\":"<<4*streamSlots
            <<",\"execution\":\"serial-guarded\",\"ownership\":\"lease-until-release\",\"token_wrap\":\"refuse\"}";
        report<<'}';
    }
    report<<"],\"runtime_functions\":[";bool firstFrame=true;
    for(const auto& [name,artifact]:model.witnesses) if(name.starts_with("runtime_frame_")) {
        if(!firstFrame) report<<',';
        firstFrame=false;
        report<<"{\"id\":"<<show(artifact->arguments[0])<<",\"result_type\":"<<vx::quote(typeText(artifact->arguments[3]))
              <<",\"captures\":"<<artifact->arguments[2]->arguments.size()
              <<",\"input_captures\":"<<std::count_if(artifact->arguments[7]->arguments.begin(),artifact->arguments[7]->arguments.end(),[](const Ref& flag){return flag->number!=0;})
              <<",\"output_captures\":"<<std::count_if(artifact->arguments[8]->arguments.begin(),artifact->arguments[8]->arguments.end(),[](const Ref& flag){return flag->number!=0;})
              <<",\"automatic_frame\":"
              <<(framesPlan.shared("vx_flow_"+show(artifact->arguments[0])) ? "false" : "true")<<"}";
    }
    report<<"],\"boundary_storage\":[";bool firstStorage=true;
    for(const auto& [name,artifact]:model.witnesses) if(name.starts_with("storage_")) {
        if(!firstStorage) report<<',';
        firstStorage=false;unsigned bytes=0;
        for(const auto& leaf:leaves(artifact->arguments[2])) bytes+=nativeBytes(leaf.type);
        report<<"{\"id\":"<<show(artifact->arguments[0])<<",\"name\":"<<vx::quote(artifact->arguments[1]->name)
              <<",\"kind\":"<<vx::quote(artifact->arguments[3]->name)<<",\"type\":"<<vx::quote(typeText(artifact->arguments[2]))
              <<",\"value_bytes\":"<<bytes<<",\"initialization_bytes\":"<<(artifact->arguments[3]->name=="External" ? 0 : 1);
        Id slot=artifact->arguments[0]->number.convert_to<Id>();
        auto addressFact=model.witnesses.find("binding_"+std::to_string(slot));
        auto binding=msxBinding(graph.nodes[cells[slot].site],artifact->arguments[2],addressFact==model.witnesses.end() ? Ref{} : addressFact->second);
        if(!binding.space.empty()) report<<",\"binding\":{\"space\":"<<vx::quote(binding.space)<<",\"address\":"<<binding.address<<",\"bytes\":"<<bytes<<"}";
        report<<"}";
    }
    report<<"],\"source_accesses\":{";bool firstAccess=true;
    for(const auto& output:outputs) {
        if(!firstAccess) report<<',';
        firstAccess=false;report<<vx::quote(output.name)<<":[";
        std::map<std::string,bool> accesses;
        std::function<void(const Ref&)> visit=[&](const Ref& part){
            if(part->name=="Access") accesses[part->arguments[0]->name]=accesses[part->arguments[0]->name] || part->arguments[1]->number!=0;
            else for(const auto& child:part->arguments) visit(child);
        };visit(model.witnesses.at("access_"+output.name));bool first=true;
        for(const auto& [resource,write]:accesses) {if(!first) report<<',';first=false;report<<"{\"resource\":"<<vx::quote(resource)<<",\"write\":"<<(write ? "true" : "false")<<"}";}
        report<<']';
    }
    report<<"}";header<<"#endif\n";
    report<<",\"mapper_storage\":{\"bytes\":"<<mapperBytes<<",\"segments\":"<<configured(model,"mapper_segments")<<",\"allocations\":[";
    bool firstMapped=true;
    for(const auto& [id,offset]:mapperOffsets) {
        if(!firstMapped) report<<',';
        firstMapped=false;
        report<<"{\"name\":"<<vx::quote(graph.nodes[cells[id].site].text)<<",\"offset\":"<<offset
              <<",\"bytes\":"<<model.witnesses.at("mapper_"+std::to_string(id))->number<<"}";
    }
    report<<"]}";
    answer.header=header.str();answer.implementation=code.str();
    banks.emit(answer,report,configured(model,"call_depth"),configured(model,"bank_chunk_operations"));
    framesPlan.report(report);
    report<<",\"boundary_contracts\":[";bool firstContract=true;
    for(const auto& [name,contract]:options.boundaries) {
        if(!firstContract) report<<',';
        firstContract=false;report<<"{\"symbol\":"<<vx::quote(name)<<",\"no_feedback\":"<<(contract.noFeedback ? "true" : "false")<<",\"reads\":[";
        bool first=true;for(const auto& resource:contract.reads) {if(!first) report<<',';first=false;report<<vx::quote(resource);}
        report<<"],\"writes\":[";first=true;
        for(const auto& resource:contract.writes) {if(!first) report<<',';first=false;report<<vx::quote(resource);}
        report<<"]}";
    }
    report<<"],\"access_order\":[";
    for(std::size_t i=0;i<options.accessOrder.size();++i) {
        if(i) report<<',';
        report<<'['<<vx::quote(options.accessOrder[i].first)<<','<<vx::quote(options.accessOrder[i].second)<<']';
    }
    report<<']';
    report<<",\"activation_graph\":{\"closed\":"<<(boundaryClosed ? "true" : "false")
          <<",\"execution_exclusive\":"<<(executionExclusive ? "true" : "false")<<",\"events\":[";
    for(std::size_t i=0;i<boundaryEventNames.size();++i) {if(i) report<<',';report<<vx::quote(boundaryEventNames[i]);}
    report<<"],\"before\":[";bool firstEdge=true;
    for(const auto& [from,to]:boundaryEventOrder) {
        if(!firstEdge) report<<',';
        firstEdge=false;report<<'['<<vx::quote(boundaryEventNames[from])<<','<<vx::quote(boundaryEventNames[to])<<']';
    }
    report<<"]}";
    report<<",\"runtime_features\":{\"fixed\":"<<(answer.implementation.find("vx_fixed_")!=std::string::npos || answer.implementation.find("vx_rescale(")!=std::string::npos ? "true" : "false")
          <<",\"float\":"<<(answer.implementation.find("vx_float_")!=std::string::npos ? "true" : "false")
          <<",\"math\":"<<(answer.implementation.find("vx_math(")!=std::string::npos ? "true" : "false")<<"}";
    answer.json+=report.str();
}
SourceResult Session::run() {
    SourceResult answer;
    try {
    configure();
    validateContracts();
    auto requested=options.queries;
    if(requested.empty() && options.sources.empty()) {
        for(const auto& [name,ids]:graph.symbols) if(graph.nodes[ids[0]].exported) requested.push_back("value:"+name);
    }
    for(const auto& query:requested) {
        auto colon=query.find(':');if(colon==std::string::npos) throw std::invalid_argument("query requires type:, value:, or length:");
        std::string kind=query.substr(0,colon);Facet facet;
        if(kind=="type") facet=Facet::Type;else if(kind=="value") facet=Facet::Value;else if(kind=="length") facet=Facet::Length;
        else throw std::invalid_argument("unknown query facet: "+kind);
        auto target=path(query.substr(colon+1),facet==Facet::Value);
        auto value=facet==Facet::Value ? observedValue(target) : facet==Facet::Type ? observedType(target) : demand(target,facet);
        queries.push_back({query,value,facet==Facet::Type});request.observation.push_back(value);
        if(facet==Facet::Value && needsFlow(target)) observableArtifacts.push_back("execution_"+std::to_string(target));
    }
    if(!options.sources.empty()) physical();
    request.observationClosure=[this](const auto& observations,const auto& artifacts){return closeObservation(observations,artifacts);};
    auto search=options.search;search.budget=searchBudget;
    answer.resolution=Machine(program,search).solve(request);
    } catch(const ConstructionLimit& limit) {
        answer.resolution.outcome=Outcome::Exhausted;answer.resolution.pending.push_back(limit.what());
    } catch(const Error& error) {
        if(error.category!=Diagnostic::Exhausted) throw;
        answer.resolution.outcome=Outcome::Exhausted;answer.resolution.pending.push_back(error.what());
    }
    answer.resolution.stats.work=searchBudget->work-startingWork;
    answer.resolution.stats.decisions=searchBudget->decisions-startingDecisions;
    std::ostringstream json;const auto& result=answer.resolution;
    json<<"{\"format\":\"vexel-relational-source-1\",\"status\":"<<vx::quote(outcomeName(result.outcome))
        <<",\"stats\":{\"work\":"<<result.stats.work<<",\"decisions\":"<<result.stats.decisions<<",\"models\":"<<result.stats.models<<",\"unfinished\":"<<result.stats.unfinished<<"},\"pending\":[";
    for(std::size_t i=0;i<result.pending.size();++i) {if(i) json<<',';json<<vx::quote(result.pending[i]);}
    auto fingerprint=[](const std::string& bytes) {
        std::uint64_t hash=14695981039346656037ULL;
        for(unsigned char byte:bytes) {hash^=byte;hash*=1099511628211ULL;}
        std::ostringstream digest;digest<<std::hex<<std::setw(16)<<std::setfill('0')<<hash;
        return digest.str();
    };
    json<<']';
    json<<",\"interface_inputs\":[";bool firstInterface=true;
    for(const auto& input:result.inputs) {if(!firstInterface) json<<',';firstInterface=false;json<<vx::quote(input);}
    json<<']';
    json<<",\"capture_proofs\":[";
    for(std::size_t i=0;i<captureProofReports.size();++i) {if(i) json<<',';json<<captureProofReports[i];}
    json<<']';
    for(bool source:{true,false}) {
        json<<(source ? ",\"source_inputs\":[" : ",\"resource_inputs\":[");bool first=true;
        for(const auto& file:graph.files) if(file.source==source) {
            if(!first) json<<',';
            first=false;json<<"{\"path\":"<<vx::quote(file.path.string())<<",\"fnv1a\":"<<vx::quote(fingerprint(file.bytes))<<"}";
        }
        json<<']';
    }
    json<<",\"directory_inputs\":[";bool firstDirectory=true;
    for(const auto& [path,names]:*graph.directorySnapshots) {
        if(!firstDirectory) json<<',';
        firstDirectory=false;json<<"{\"path\":"<<vx::quote(path)<<",\"files\":[";
        for(std::size_t i=0;i<names.size();++i) {if(i) json<<',';json<<vx::quote(names[i]);}
        json<<"]}";
    }
    json<<"],\"process_inputs\":[";bool firstProcess=true;
    for(const auto& [key,output]:*graph.processSnapshots) {
        if(!firstProcess) json<<',';
        firstProcess=false;json<<"{\"key\":"<<vx::quote(key)<<",\"status\":"<<vx::quote(output.second)
            <<",\"bytes\":"<<output.first.size()<<",\"fnv1a\":"<<vx::quote(fingerprint(output.first))<<"}";
    }
    json<<"],\"input_dependencies\":[";bool firstDependency=true;
    for(const auto& path:graph.dependencies) {if(!firstDependency) json<<',';firstDependency=false;json<<vx::quote(path);}
    json<<"],\"input_selections\":[";bool firstSelection=true;
    for(const auto& [key,value]:*graph.inputSnapshots) if(key.starts_with("resource-kind:") || key.starts_with("resource-path:") || key.starts_with("path-exists:") || key.starts_with("path-target:") || key.starts_with("source-kind:")) {
        if(!firstSelection) json<<',';
        firstSelection=false;json<<"{\"key\":"<<vx::quote(key)<<",\"value\":"<<vx::quote(value)<<"}";
    }
    json<<"],\"source_metadata\":[";bool firstMetadata=true;
    for(const auto& node:graph.nodes) if(!node.metadata.empty()) {
        if(!firstMetadata) json<<',';
        firstMetadata=false;json<<"{\"location\":"<<vx::quote(graph.location(node.span))<<",\"tags\":[";
        for(std::size_t i=0;i<node.metadata.size();++i) {if(i) json<<',';json<<vx::quote(node.metadata[i]);}
        json<<"]}";
    }
    json<<']';
    json<<",\"configuration_inputs\":{";bool firstConfigInput=true;
    for(const auto& [key,origins]:configurationOrigins) {
        if(!firstConfigInput) json<<',';
        firstConfigInput=false;json<<vx::quote(key)<<":[";
        for(std::size_t i=0;i<origins.size();++i) {if(i) json<<',';json<<vx::quote(origins[i]);}
        json<<']';
    }
    json<<'}';
    if(result.outcome==Outcome::Resolved) {
        json<<",\"configuration\":{";bool firstConfig=true;
        for(const auto& [key,value]:configuration) {
            (void)value;if(!firstConfig) json<<',';firstConfig=false;
            json<<vx::quote(key)<<":{\"value\":"<<configured(*result.selected,key)<<",\"origins\":[";
            const auto& origins=configurationOrigins.at(key);
            for(std::size_t i=0;i<origins.size();++i) {if(i) json<<',';json<<vx::quote(origins[i]);}
            json<<"]}";
        }
        json<<'}';
        json<<",\"queries\":[";
        for(std::size_t i=0;i<queries.size();++i) {if(i) json<<',';json<<"{\"query\":"<<vx::quote(queries[i].name)<<",\"answer\":"<<vx::quote(queries[i].type ? typeText(result.selected->observation[i]) : show(result.selected->observation[i]))<<"}";}
        json<<"]";
    }
    answer.json=json.str();
    if(result.outcome==Outcome::Resolved && !outputs.empty()) project(answer,*result.selected,queries.size());
    answer.json+="}\n";return answer;
}
} // namespace
struct SourceProgram::Impl {Graph graph;explicit Impl(const std::filesystem::path& path){loadProgram(graph,path);}};
SourceProgram::SourceProgram(const std::filesystem::path& path):impl(std::make_unique<Impl>(path)){}
SourceProgram::~SourceProgram()=default;
SourceResult SourceProgram::solve(const SourceOptions& options) {
    auto& graph=impl->graph;graph.options.maxFileBytes=options.maxFileBytes;
    auto answer=Session(graph,options).run();
    auto inputs=std::make_shared<SourceInputs>();
    for(const auto& file:graph.files) inputs->files.emplace(file.path.string(),file.bytes);
    for(const auto& [key,value]:*graph.inputSnapshots)
        if(key.starts_with("path-exists:") || key.starts_with("path-target:") ||
           key.starts_with("resource-kind:") || key.starts_with("source-kind:")) inputs->selections.emplace(key,value);
    inputs->directories=*graph.directorySnapshots;
    inputs->hasProcesses=!graph.processSnapshots->empty();
    answer.inputs=std::move(inputs);return answer;
}
} // namespace vx::relational
