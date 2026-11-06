#include "kernel.hpp"
#include "process.hpp"
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <sstream>

namespace vx {
RelationKernel::~RelationKernel() { if (library) dlclose(library); }
std::shared_ptr<RelationKernel> compileRelations(ConstraintProblem& problem) {
    if (problem.options.compiledSearch.empty()) return {};
    try {
        auto root = std::filesystem::absolute(problem.options.compiledSearch);
        std::filesystem::create_directories(root);
        std::string pattern = (root / "puzzle-XXXXXX").string();
        std::vector<char> directory(pattern.begin(),pattern.end()); directory.push_back(0);
        if (!mkdtemp(directory.data())) throw std::runtime_error("cannot create kernel directory");
        problem.kernelDirectory = directory.data();
        auto path = std::filesystem::path(problem.kernelDirectory);
        std::ostringstream source;
        source << "// Generated finite relation filter; domain indices, not source arithmetic.\n#include <cstddef>\n";
        for (std::size_t i = 0; i < problem.relations.size(); ++i) {
            source << "static const std::size_t table" << i << "[]={";
            const auto& relation = problem.relations[i];
            for (const auto& row : relation.tuples) for (auto value : row) source << value << ',';
            source << "0};\n";
            if (source.tellp() > static_cast<std::streamoff>(problem.options.maxFileBytes))
                throw std::runtime_error("kernel source exceeds file budget");
        }
        source << "struct Relation{std::size_t columns,rows;const std::size_t* tuples;};\nstatic const Relation relations[]={\n";
        for (std::size_t i = 0; i < problem.relations.size(); ++i) {
            const auto& r = problem.relations[i];
            source << '{' << r.variables.size() << ',' << r.tuples.size() << ",table" << i << "},\n";
        }
        source << "{0,0,nullptr}};\n";
        source << R"CPP(
extern "C" int vx_filter(std::size_t id,const unsigned char* const* active,
    unsigned char* const* supported,std::size_t budget,std::size_t* checks) {
    const auto& r=relations[id];
    bool any=false; *checks=0;
    for(std::size_t row=0;row<r.rows;++row) {
        if(*checks==budget) return -2;
        ++*checks;
        const auto* tuple=r.tuples+row*r.columns;
        bool valid=true;
        for(std::size_t col=0;col<r.columns;++col)
            if(!active[col][tuple[col]]){valid=false;break;}
        if(!valid) continue;
        any=true;
        for(std::size_t col=0;col<r.columns;++col) supported[col][tuple[col]]=1;
    }
    return any?1:0;
}
)CPP";
        std::ofstream cpp(path / "relations.cpp",std::ios::binary);
        if (!(cpp << source.str()) || !cpp.flush()) throw std::runtime_error("cannot write kernel source");
        cpp.close();
        std::ofstream makefile(path / "Makefile",std::ios::binary);
        makefile << "CXX ?= c++\nCXXFLAGS ?= -O2\nall: relations.so\nrelations.so: relations.cpp\n\t$(CXX) $(CXXFLAGS) -std=c++20 -fPIC -shared $< -o $@\n";
        if (!makefile.flush()) throw std::runtime_error("cannot write kernel Makefile");
        makefile.close();
        auto [log,status] = runProcess({"make","--no-print-directory","-s","-C",path.string()}, {}, 30000, true);
        std::ofstream(path/"build.log") << log;
        if (status != "received") { problem.kernelStatus = "compile-" + status; return {}; }
        auto result = std::make_shared<RelationKernel>();
        result->library = dlopen((path/"relations.so").c_str(),RTLD_NOW | RTLD_LOCAL);
        if (!result->library) { problem.kernelStatus = "load-failed"; return {}; }
        result->filter = reinterpret_cast<RelationKernel::Filter>(dlsym(result->library,"vx_filter"));
        if (!result->filter) { problem.kernelStatus = "symbol-missing"; return {}; }
        problem.kernelStatus = problem.options.verifySearchKernel ? "compiled-checked" : "compiled";
        return result;
    } catch (const std::exception& error) {
        problem.kernelStatus = std::string("unavailable: ") + error.what(); return {};
    }
}
} // namespace vx
