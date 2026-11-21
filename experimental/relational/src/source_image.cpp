#include "source_image.hpp"
#include "vexel.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace vx::relational {
namespace {
std::string fingerprint(const std::string& bytes) {
    std::uint64_t hash=14695981039346656037ULL;
    for(unsigned char byte:bytes) {hash^=byte;hash*=1099511628211ULL;}
    std::ostringstream out;out<<std::hex<<std::setw(16)<<std::setfill('0')<<hash;return out.str();
}
void strings(std::ostream& out,const auto& values) {
    out<<'[';bool first=true;for(const auto& value:values) {if(!first) out<<',';first=false;out<<vx::quote(value);}out<<']';
}
}
std::string termJson(const Ref& value) {
    if(value->kind==Term::Number) return "{\"number\":"+vx::quote(value->number.str())+"}";
    if(value->kind==Term::Variable) throw std::logic_error("unresolved term in closed program observation");
    std::string result="{\"constructor\":"+vx::quote(value->name)+",\"arguments\":[";
    for(std::size_t i=0;i<value->arguments.size();++i) {if(i) result+=',';result+=termJson(value->arguments[i]);}
    return result+"]}";
}
std::string ProgramImage::emit(SourceResult& answer,const SourceOptions& options) const {
    auto ordered=units;
    std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){return a.begin<b.begin;});
    std::set<std::string> symbols;std::vector<std::string> segments;std::string support;
    std::ostringstream out;out<<"{\"format\":\"vexel-program-image-1\",\"selection\":"
        <<vx::quote(options.wholeProgram ? "all-exports" : "selected-observations")
        <<",\"activation\":"<<vx::quote(options.activation)<<",\"report\":\"source-plan.json\",\"roots\":[";
    for(std::size_t i=0;i<roots.size();++i) {
        if(i) out<<',';
        out<<"{\"name\":"<<vx::quote(roots[i].name)<<",\"definition\":"<<vx::quote(roots[i].definition)
           <<",\"unit\":"<<vx::quote("vx_"+roots[i].name)<<"}";
    }
    out<<"],\"observations\":";strings(out,options.queries);out<<",\"units\":[";
    std::size_t copied=0,gaps=0;
    auto gap=[&](std::size_t end) {
        if(end==copied) return;
        auto name="vx_program_shared_"+std::to_string(gaps++)+".inc";
        auto text=answer.implementation.substr(copied,end-copied);
        answer.files.emplace(name,text);support+=text;segments.push_back(name);copied=end;
    };
    for(std::size_t i=0;i<ordered.size();++i) {
        const auto& unit=ordered[i];
        if(!symbols.insert(unit.symbol).second || unit.begin<copied || unit.end<=unit.begin || unit.end>answer.implementation.size())
            throw std::logic_error("overlapping or invalid program code unit: "+unit.symbol);
        gap(unit.begin);auto file="vx_unit_"+unit.symbol+".inc";
        answer.files.emplace(file,answer.implementation.substr(unit.begin,unit.end-unit.begin));
        segments.push_back(file);copied=unit.end;
        if(i) out<<',';
        out<<"{\"symbol\":"<<vx::quote(unit.symbol)<<",\"kind\":"<<vx::quote(unit.kind)
           <<",\"fragment\":"<<vx::quote(file)<<",\"owners\":";strings(out,unit.owners);
        out<<",\"calls\":";strings(out,unit.calls);
        out<<",\"support\":";
        if(unit.support.empty()) out<<"null";
        else {
            auto name="vx_support_unit_"+unit.symbol+".h";
            answer.files.emplace(name,"#include \"vexel.h\"\n#undef VX_VALUE_LOCATION\n#define VX_VALUE_LOCATION VX_RESIDENT\n"+
                unit.support+"#undef VX_VALUE_LOCATION\n#define VX_VALUE_LOCATION\n");
            out<<vx::quote(name);
        }
        out<<'}';
    }
    gap(answer.implementation.size());answer.files.emplace("vx_program_support.inc",support);
    out<<"],\"segments\":";strings(out,segments);
    out<<",\"support\":\"vx_program_support.inc\",\"artifacts\":[";
    auto artifacts=answer.files;artifacts.emplace("vexel.h",answer.header);artifacts.emplace("vx_bridges.c",answer.implementation);
    bool first=true;
    for(const auto& [name,contents]:artifacts) {
        if(!first) out<<',';
        first=false;out<<"{\"path\":"<<vx::quote(name)<<",\"bytes\":"<<contents.size()<<",\"fnv1a\":"<<vx::quote(fingerprint(contents))<<"}";
    }
    out<<"]}";auto json=out.str();answer.files.emplace("program-image.json",json+"\n");return json;
}
}
