#include "vexel.hpp"

namespace vx {
bool Solver::signatureSpecialization(Id scope) const {
    std::set<Id> seen;
    while (scope != none && seen.insert(scope).second) {
        const auto& frame = g.scopes[scope];
        const auto& owner = g.nodes[frame.node];
        if (owner.slot != none && owner.region != none && g.nodes[owner.region].valueDependentSignature) return true;
        scope = owner.slot != none ? frame.caller : frame.parent != none ? frame.parent : frame.caller;
    }
    return false;
}
bool Solver::inferRecursiveInterfaces() {
    // At quiescence, solve the return-type equations over the existing graph.
    // Unknown is the bottom element, never an invented default numeric width.
    // A seed from a base arm can close a recursive component; the ordinary
    // rules subsequently validate every operand and every returning path.
    bool progress = false;
    for (Id id = 0; id < g.nodes.size(); ++id) {
        const auto& call = g.nodes[id];
        if (call.rule != Rule::Invoke || call.runtimeBackedge == none || call.inferredType || call.fact) continue;
        std::set<Id> visiting;
        // A recursive interface is a candidate checked against the complete
        // returning region below. A supplied base interface can instantiate an
        // open requirement; its captured identity is provisional until the
        // ordinary annotation obligations validate the lexical environment.
        std::function<Type(const Type&,const Type&)> instantiateRequirement=[&](const Type& target,const Type& supplied)->Type {
            Type result=resolvedType(target),source=resolvedType(supplied);
            if(result.kind==Type::Unknown && result.obligation==none) return source;
            if(result.kind==Type::Record && source.kind==Type::Record && result.declaration==source.declaration && result.fieldNames==source.fieldNames) {
                for(std::size_t i=0;i<result.fields.size();++i) result.fields[i]=instantiateRequirement(result.fields[i],source.fields[i]);
                if(result.schemaObligation!=none && g.nodes[result.schemaObligation].key=="argument-type") {
                    result.schema=source.schema;result.schemaObligation=source.schemaObligation;
                }
            }
            if(result.kind==Type::Array && source.kind==Type::Array && result.size==source.size)
                result=Type::array(instantiateRequirement(*result.element,*source.element),result.size);
            return result;
        };
        bool selectedGuard = false;
        std::function<Type(Id)> property = [&](Id at) -> Type {
            if (at == none) return {};
            const auto& n = g.nodes[at];
            if (n.fact) return n.fact->type;
            if(n.rule==Rule::Type && n.inferredType) return resolvedType(*n.inferredType);
            if (n.inferredType && n.inferredType->settled()) return *n.inferredType;
            if (!visiting.insert(at).second) return {};
            auto join = [&](const std::vector<Id>& inputs) {
                Type result;
                for (Id input : inputs) result = common(result,property(input),at);
                return result;
            };
            Type result;
            switch (n.rule) {
            case Rule::Invoke: case Rule::Evaluate: case Rule::Global: case Rule::Deferred:
                result = property(n.expansion); break;
            case Rule::Finish: case Rule::Bind: case Rule::Update: case Rule::Unpack:
                if (!n.inputs.empty()) result = property(n.inputs[0]);
                break;
            case Rule::Input: case Rule::Slot:
                result = n.annotation != none ? property(n.annotation) : n.inputs.empty() ? Type{} : property(n.inputs[0]); break;
            case Rule::Load: case Rule::GlobalRead: result = property(n.slot); break;
            case Rule::Cast: case Rule::Adapt:
                result=property(n.inputs[1]);
                if(!result.settled()) result=instantiateRequirement(result,property(n.inputs[0]));
                break;
            case Rule::Select:
                if (call.signatureOrigin != none && n.branches.size() == 2 && g.nodes[n.inputs[0]].fact &&
                    g.nodes[n.inputs[0]].fact->value.kind == Value::Number)
                    { selectedGuard = true; result = property(n.branches[g.nodes[n.inputs[0]].fact->value.number != 0 ? 0 : 1]); }
                else result = join(n.branches);
                break;
            case Rule::Construct: case Rule::Tuple: {
                if (n.inferredType) result = resolvedType(*n.inferredType);
                else if (n.annotation != none) result = property(n.annotation);
                // Named constructors carry converted field proxies. Reading
                // raw operands here would bypass their cyclic requirements.
                if (result.kind == Type::Record) for (std::size_t i=0;i<result.fields.size();++i)
                    if (!result.fields[i].settled())
                        result.fields[i] = property(n.environment.empty() ? n.inputs[i] : n.environment[i]);
                break;
            }
            case Rule::Sequence:
                if (!n.branches.empty() && n.branches.size() == g.nodes[n.site].children.size())
                    result = g.nodes[g.nodes[n.site].children.back()].terminated ? Type::simple(Type::Empty) : property(n.branches.back());
                break;
            case Rule::FunctionResult: {
                if(n.annotation!=none) {
                    result=property(n.annotation);
                    if(result.settled()) break;
                }
                std::vector<Id> outputs;
                const auto& state = g.nodes[n.inputs[1]];
                if ((state.fact ? state.completion : state.typeCompletion).normal) outputs.push_back(n.inputs[0]);
                for (const auto& jump : g.nodes)
                    if (jump.rule == Rule::Exit && jump.region == n.region && jump.text == "return" &&
                        (jump.demanded || jump.typeDemanded)) outputs.push_back(jump.inputs[1]);
                result=n.annotation==none ? join(outputs) : instantiateRequirement(result,join(outputs)); break;
            }
            case Rule::Unary:
                result = n.text == "!" ? Type::simple(Type::Boolean) : property(n.inputs[0]); break;
            case Rule::Binary:
                if (n.text == "==" || n.text == "!=" || n.text == "<" || n.text == "<=" || n.text == ">" || n.text == ">=")
                    result = Type::simple(Type::Boolean);
                else if (n.text == "<<" || n.text == ">>") result = property(n.inputs[0]);
                else if (!n.text.starts_with('.') && n.text != "..") result = join(n.inputs);
                break;
            case Rule::ShortCircuit: result = Type::simple(Type::Boolean); break;
            case Rule::Array: result = Type::array(join(n.inputs),n.inputs.size()); break;
            case Rule::Index: {
                Type array = property(n.inputs[0]);
                if (array.kind == Type::Array) result = *array.element;
                else if (array.kind == Type::Record && g.nodes[n.inputs[1]].fact) {
                    const auto& offset = g.nodes[n.inputs[1]].fact->value;
                    if (offset.kind == Value::Number && offset.number >= 0 && offset.number < array.fields.size())
                        result = array.fields[offset.number.convert_to<std::size_t>()];
                } else if (array.kind==Type::Record && g.nodes[n.inputs[1]].rule==Rule::FieldIndex) {
                    const auto& member=g.nodes[n.inputs[1]].text;
                    for (std::size_t i=0;i<array.fieldNames.size();++i)
                        if (array.fieldNames[i]==member) result=array.fields[i];
                }
                break;
            }
            default: break;
            }
            visiting.erase(at); return result;
        };
        Type signature;
        try {
            signature=resolvedType(property(call.expansion));
            requireTypeProperties(signature);
        }
        catch(const TypePropertiesRequired& pending) {
            auto queued=queue.size();
            processingType=true;
            for(Id input:pending.types) (void)needType(id,input);
            for(Id schema:pending.schemas) (void)need(id,schema);
            processingType=false;progress=progress || queue.size()!=queued;continue;
        }
        catch (const Error&) {
            if (call.signatureOrigin == none) throw;
            throw SignatureRevision{call.runtimeBackedge,g.nodes[call.runtimeBackedge].key};
        }
        if (call.signatureOrigin != none && (signature.kind != Type::Unknown || selectedGuard) && !signature.settled())
            throw SignatureRevision{call.runtimeBackedge,g.nodes[call.runtimeBackedge].key};
        if (!signature.settled() || (call.signatureOrigin == none && !signature.concrete() && signature.kind != Type::Empty)) continue;
        publishType(id,signature); if (call.demanded) enqueue(id); progress = true;
    }
    return progress;
}
} // namespace vx
