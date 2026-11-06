// A source supplies a logical value. Its physical calling convention flattens
// that value only after the ordinary annotation relationships have closed.
// No host pointer, C struct layout or mutable candidate-specific Cell enters
// the source interface.
void Session::sourceInput(Id id,Ref result) {
    auto type=demand(id,Facet::Type);
    std::vector<Ref> support{type};
    if(cells[id].inputRange) {
        support.push_back(cells[id].inputMin);support.push_back(cells[id].inputMax);
    }
    later(support,[this,id,result](const auto& facts){
        const auto& input=cells[id];const auto& t=facts[0];
        if(input.inputRange && !nativeScalar(t)) {
            throw Error(graph.nodes[input.site].span,"boundary range requires a native numeric scalar");
        }
        auto flat=leaves(t,{},input.inputName);std::vector<Ref> parameters;
        std::size_t index=0;
        auto value=structure(t,[&](const Ref& scalar){
            if(!nativeScalar(scalar)) {
                pending("source input leaf needs a target representation: "+typeText(scalar),input.site);
                return fresh("unrepresented source input");
            }
            unsigned bits=scalarBits(scalar);bool sign=scalarSigned(scalar);
            Integer lower=sign ? -(Integer(1)<<(bits-1)) : Integer(0),upper=(Integer(1)<<(bits-(sign ? 1 : 0)))-1;
            if(input.inputRange) {
                if(facts[1]->kind!=Term::Number || facts[2]->kind!=Term::Number ||
                   facts[1]->number<lower || facts[2]->number<facts[1]->number || facts[2]->number>upper) {
                    conflict();return fresh("invalid boundary range");
                }
                lower=facts[1]->number;upper=facts[2]->number;
            }
            auto name="a"+std::to_string(input.location)+(aggregate(t) ? "_"+std::to_string(index) : "");
            parameters.push_back(terms().make("InputLeaf",{terms().make(name),terms().make(flat[index++].path),scalar,number(lower),number(upper)}));
            return terms().make("Runtime",{terms().make("Input",{terms().make(name)}),number(lower),number(upper)});
        });
        equation(result,value);
        request.goals.push_back(witness("source_input_"+std::to_string(id),
            terms().make("InputParameter",{t,value,terms().make("Leaves",parameters)})));
    });
}
