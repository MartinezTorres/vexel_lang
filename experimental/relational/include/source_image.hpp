#pragma once
#include "source.hpp"

namespace vx::relational {
// The closed program's physical publication boundary. Units are recorded by
// their emitters, never recovered by matching generated function spellings.
struct ProgramImage {
    struct Unit {
        std::string symbol,kind;
        std::size_t begin=0,end=0;
        std::set<std::string> owners,calls;
        // Private realization helpers must travel with a unit when a host
        // integration places it in an independent translation unit.
        std::string support={};
    };
    struct Root {std::string name,definition;};
    std::vector<Unit> units;
    std::vector<Root> roots;
    std::string emit(SourceResult& answer,const SourceOptions& options) const;
};
std::string termJson(const Ref& value);
}
