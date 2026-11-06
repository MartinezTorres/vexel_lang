// A materialized interface carries static facts alongside its dynamic slots.
// Native slots close from their type alone. Waiting for their computed values
// here would introduce a false dependency around every recursive call cycle.
Ref Session::framePattern(Ref type,Ref value) {
    auto result=fresh("materialized static interface");
    later({type},[this,value,result](const auto& types){
        std::vector<Ref> projected;
        auto skeleton=structure(types[0],[&](const Ref& leaf){
            auto part=fresh("materialized result leaf");
            projected.push_back(nativeScalar(leaf) ? terms().make("Dynamic") : part);return part;
        });
        equation(value,skeleton);std::size_t index=0;
        equation(result,structure(types[0],[&](const Ref&){return projected[index++];}));
    });return result;
}
void Session::frameValue(Ref type,Ref pattern,Ref expression,Ref result) {
    later({type,pattern},[this,expression,result](const auto& inputs){
        auto flat=leaves(inputs[0],inputs[1]);std::size_t at=0,offset=0;
        auto value=structure(inputs[0],[&](const Ref& leaf){
            auto part=flat[at++].value;
            if(!nativeScalar(leaf)) {
                if(runtimeValue(part)) {pending("materialized non-native leaf needs a static value or a target representation");return fresh("unsupported frame leaf");}
                return part;
            }
            auto indices=expression->arguments;auto index=fresh("native frame slot");
            request.goals.push_back(add(indices.back(),number(offset++),index));indices.back()=index;
            unsigned bits=scalarBits(leaf);bool sign=scalarSigned(leaf);
            return terms().make("Runtime",{terms().make(expression->name,indices),number(sign ? -(Integer(1)<<(bits-1)) : Integer(0)),
                number((Integer(1)<<(bits-(sign ? 1 : 0)))-1)});
        });
        equation(result,value);
    });
}
