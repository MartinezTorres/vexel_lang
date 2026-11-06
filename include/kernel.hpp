#pragma once
#include "constraints.hpp"
namespace vx {
struct RelationKernel {
    using Filter = int (*)(std::size_t,const unsigned char* const*,unsigned char* const*,std::size_t,std::size_t*);
    void* library = nullptr;
    Filter filter = nullptr;
    ~RelationKernel();
};
std::shared_ptr<RelationKernel> compileRelations(ConstraintProblem& problem);
}
