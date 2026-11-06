#include "vexel.hpp"

namespace vx {
void Solver::loopTypeRule(Id id) {
    auto& n=g.nodes[id];const auto& syntax=g.nodes[n.site];
    auto flow=[&](Id state)->const Completion& {
        const auto& value=g.nodes[state];return value.fact ? value.completion : value.typeCompletion;
    };
    auto close=[&](Id state) {
        if(n.key!="closed") {n.key="closed";n.endState=resume(state,id,"loop",n.site,n.scope);}
        if(!needType(id,n.endState)) return;
        n.typeCompletion=flow(n.endState);
        publishType(id,Type::simple(Type::Empty));
    };
    if(n.key=="closed") {close(n.endState);return;}
    if(n.rule==Rule::Iterate) {
        const Type* array=needType(id,n.inputs[0]);if(!array) return;
        if(array->kind!=Type::Array) fail(id,"unrolled iteration requires a fixed-size array or range",Diagnostic::NotIterable);
        if(!n.expanded) {n.expanded=true;n.endState=n.previous;}
        if(n.expansion!=none) {
            if(!needType(id,n.expansion)) return;
            n.endState=n.expansion;n.expansion=none;
        }
        if(!needType(id,n.endState)) return;
        if(n.phase==array->size || !flow(n.endState).normal) {close(n.endState);return;}
        Id frame=g.scope(n.scope,n.site,n.identity+"/type-iteration/"+std::to_string(n.phase));
        g.scopes[frame].loop=id;
        Id item;
        // A sorted value is irrelevant to a value-independent body signature.
        // If a body uses an element to choose a type, it needs the real sort.
        Id arrayValue=n.inputs[0];
        if(n.text=="@@") {
            if(n.annotation==none) {n.annotation=g.semantic(Rule::Sort,n.site,{arrayValue},n.scope);g.nodes[n.annotation].previous=n.previous;}
            arrayValue=n.annotation;
        }
        Id index=literal(n.site,Type::simple(Type::Exact),Value::integer(n.phase++));
        item=g.semantic(Rule::Index,n.site,{arrayValue,index},frame);
        Id variable=g.semantic(Rule::Slot,n.site,{item},frame);g.nodes[variable].readonly=true;g.scopes[frame].slots["_"]=variable;
        Id state=g.semantic(Rule::Store,n.site,{n.endState,item},frame);g.nodes[state].slot=variable;g.nodes[state].previous=n.endState;
        Built body=build(syntax.children[1],frame,state);
        n.branches.push_back(body.value);n.ends.push_back(body.state);
        n.expansion=resume(body.state,id,"continue",n.site,frame);
        (void)needType(id,n.expansion);return;
    }
    bool arrayLoop=n.text!="condition";
    const Type* array=nullptr;
    if(arrayLoop) {
        array=needType(id,n.inputs[0]);if(!array) return;
        if(array->kind!=Type::Array) fail(id,"runtime iteration requires a fixed-size array",Diagnostic::NotIterable);
        if(!array->size) {close(n.previous);return;}
    }
    if(!n.expanded) {
        n.expanded=true;
        Id frame=g.scope(n.scope,n.site,n.identity+"/type-loop");g.scopes[frame].loop=id;
        Id state=n.previous;
        if(arrayLoop) {
            Id item=literal(n.site,*array->element,Value::runtime());
            Id variable=g.semantic(Rule::Slot,n.site,{item},frame);g.nodes[variable].readonly=true;g.scopes[frame].slots["_"]=variable;
            Id store=g.semantic(Rule::Store,n.site,{state,item},frame);g.nodes[store].slot=variable;g.nodes[store].previous=state;state=store;
        } else {
            Built condition=build(syntax.children[0],frame,state);n.annotation=condition.value;state=condition.state;
        }
        n.previous=state;
        n.lexicalScope=frame;
    }
    if(n.annotation!=none) {
        const Type* condition=needType(id,n.annotation);if(!condition) return;
        if(condition->kind!=Type::Boolean && condition->kind!=Type::Exact) fail(id,"condition must be boolean");
        const auto& known=g.nodes[n.annotation].fact;
        if(known && known->value.kind==Value::Number && known->value.number==0) {close(n.previous);return;}
    }
    if(n.expansion==none) {
        Built body=build(syntax.children[1],n.lexicalScope,n.previous);
        n.expansion=body.state;n.body=body.value;
    }
    if(!needType(id,n.expansion)) return;
    n.typeCompletion=flow(n.expansion);
    bool maySkip=!arrayLoop;
    if(n.annotation!=none) {
        const auto& known=g.nodes[n.annotation].fact;
        if(known && known->value.kind==Value::Number && known->value.number!=0) maySkip=false;
    }
    Completion result;result.normal=n.typeCompletion.normal || maySkip;
    for(Id exit:n.typeCompletion.exits) {
        const auto& jump=g.nodes[exit];
        if(jump.region==id && jump.text!="return") result.normal=true;
        else result.exits.push_back(exit);
    }
    n.typeCompletion=std::move(result);
    publishType(id,Type::simple(Type::Empty));
}
} // namespace vx
