#include "vexel.hpp"

namespace vx {
void Solver::selectTypeRule(Id id,bool typesOnly) {
    auto& node=g.nodes[id];
    const bool schemaOnly=node.key=="select-schema";
    auto emit=[&](Type type) {
        type=resolvedType(type);
        if(schemaOnly) {
            if(type.schemaObligation!=none) {
                if(!need(id,type.schemaObligation)) return;
                type=resolvedType(type);
            }
            publish(id,{type,{},Effect::Pure});
        } else if(typesOnly) publishType(id,std::move(type));
        else {
            requireTypeProperties(type);
            if(type.settled()) publish(id,{type,{},Effect::Pure});
            else publishType(id,std::move(type));
        }
    };
    std::optional<Type> types[2];std::optional<Error> errors[2];
    for(unsigned i=0;i<2;++i) {
        try {if(const Type* type=needType(id,node.inputs[i+1])) types[i]=resolvedType(*type);}
        catch(const Error& error) {
            if(error.category==Diagnostic::Exhausted || error.category==Diagnostic::InvalidReplay || error.category==Diagnostic::SearchAmbiguous) throw;
            errors[i]=error;
        }
    }
    auto choose=[&]() -> const Fact* {
        const Fact* condition=need(id,node.inputs[0]);
        if(condition && condition->value.symbolic()) requestChoice(condition->value,id);
        return condition;
    };
    auto selected=[&](const Fact* condition) {
        if(!condition || condition->value.kind!=Value::Number) return false;
        unsigned index=condition->value.number!=0 ? 0U : 1U;
        if(errors[index]) throw *errors[index];
        if(types[index]) emit(*types[index]);
        return true;
    };
    // Once the source condition is established, inactive branch properties
    // cannot become prerequisites through a signature projection.
    if(selected(g.nodes[node.inputs[0]].fact ? &*g.nodes[node.inputs[0]].fact : nullptr)) return;
    if(errors[0] || errors[1]) {
        const Fact* condition=choose();if(!condition || selected(condition)) return;
        for(const auto& error:errors) if(error) throw *error;
    }
    if(!types[0] || !types[1]) return;
    Type left=*types[0],right=*types[1];
    if(schemaOnly) {
        bool same=left.schemaObligation==right.schemaObligation && left.schema==right.schema;
        if(same) {emit(left);return;}
        const Fact* condition=choose();if(!condition || selected(condition)) return;
        bool ready=true;
        for(const Type* type:{&left,&right}) if(type->schemaObligation!=none && !need(id,type->schemaObligation)) ready=false;
        if(!ready) return;
        left=resolvedType(left);right=resolvedType(right);
        if(left.schema!=right.schema) fail(id,"record types have different captured schema values");
        emit(left);return;
    }
    if(left==right) {emit(left);return;}
    bool record=left.kind==Type::Record && right.kind==Type::Record && left.declaration==right.declaration && left.fieldNames==right.fieldNames;
    bool array=left.kind==Type::Array && right.kind==Type::Array && left.size==right.size;
    if(record || array) {
        // These are type projections, including index zero for an empty array:
        // no element value is read. Their obligations remain independently lazy.
        std::size_t count=record ? left.fields.size() : 1;
        if(node.branches.empty()) for(std::size_t i=0;i<count;++i) {
            Id offset=literal(node.site,Type::simple(Type::Exact),Value::integer(i));
            Id a=g.semantic(Rule::Index,node.site,{node.inputs[1],offset},node.scope);
            Id b=g.semantic(Rule::Index,node.site,{node.inputs[2],offset},node.scope);
            Id joined=g.semantic(Rule::TypeJoin,node.site,{node.inputs[0],a,b},node.scope);
            g.nodes[joined].key="select-type";node.branches.push_back(joined);
        }
        Type result=left;
        for(std::size_t i=0;i<count;++i) {
            Type field;field.obligation=node.branches[i];
            if(record) result.fields[i]=field;
            else result.element=std::make_shared<Type>(std::move(field));
        }
        if(record) {
            // Captured schema identity is independent of field layout. Its
            // selected origin follows the schema when the promise resolves.
            if(node.signature==none) {
                node.signature=g.semantic(Rule::TypeJoin,node.site,node.inputs,node.scope);
                g.nodes[node.signature].key="select-schema";
            }
            result.schema.clear();result.schemaObligation=node.signature;
        }
        emit(result);return;
    }
    // A scalar width, nominal family or array extent can depend on the guard.
    // Pending operand properties must resolve before comparing these leaves.
    requireTypeProperties(left);requireTypeProperties(right);
    const Fact* condition=choose();if(!condition || selected(condition)) return;
    emit(common(left,right,id));
}
} // namespace vx
