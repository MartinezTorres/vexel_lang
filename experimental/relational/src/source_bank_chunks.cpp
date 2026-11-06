#include "source_bank_chunks.hpp"
#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace vx::relational {
namespace {
std::string trim(const std::string& s) {
    auto begin=s.find_first_not_of(" \t\r\n");if(begin==std::string::npos) return {};
    return s.substr(begin,s.find_last_not_of(" \t\r\n")-begin+1);
}
struct Statement {enum Kind {Simple,If,Loop} kind=Simple;std::string text;std::vector<Statement> yes,no;};
struct Step {std::string code,condition;unsigned yes=0,no=0;};
struct Builder {
    std::vector<std::string> lines,fields,parameters;
    std::map<std::string,std::string> names;
    std::vector<Step> steps{{}};
    std::size_t at=0;
    bool returns=false;
    std::string rewrite(const std::string& text) const {
        std::string result;
        for(std::size_t i=0;i<text.size();) {
            const auto c=text[i];
            if(std::isalnum(static_cast<unsigned char>(c)) || c=='_') {
                auto end=i+1;while(end<text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end]=='_')) ++end;
                auto word=text.substr(i,end-i);auto found=names.find(word);
                result+=found==names.end() ? word : found->second;i=end;
            } else result+=text[i++];
        }
        return result;
    }
    bool declaration(std::string& text,bool parameter=false) {
        static const std::regex pattern(R"(^(?:static\s+)?(VX_STABLE\s+)?((?:u?int(?:8|16|32)_t)\s*\*?\s+|(?:u?int(?:8|16|32)_t)\s+\*?)(.*?);?$)");
        std::smatch match;if(!std::regex_match(text,match,pattern)) return false;
        std::string type=match[1].str()+match[2].str(),variables=match[3];
        variables=trim(variables);
        if(variables.starts_with('*')) {type+="*";variables=trim(variables.substr(1));}
        if(variables.ends_with(';')) variables.pop_back();
        std::vector<std::string> declarations;
        // Generated initialized declarations always have one declarator.
        if(variables.find('=')!=std::string::npos) declarations.push_back(variables);
        else {std::istringstream input(variables);std::string variable;while(std::getline(input,variable,',')) declarations.push_back(trim(variable));}
        std::string initial;
        for(const auto& variable:declarations) {
            auto end=variable.find_first_of("[ =");auto name=variable.substr(0,end);
            if(name.empty()) throw std::logic_error("empty generated frame member");
            auto equal=variable.find('=');auto field=trim(variable.substr(0,equal));
            fields.push_back(type+field+";");names.emplace(name,"frame->"+name);
            if(parameter) parameters.push_back(name);
            if(equal!=std::string::npos) initial+=name+" = "+trim(variable.substr(equal+1))+";";
        }
        text=initial;return true;
    }
    std::vector<Statement> block() {
        std::vector<Statement> result;
        while(at<lines.size() && lines[at]!="}" && lines[at]!="} else {") {
            auto text=lines[at++];if(text.empty()) continue;
            if(declaration(text) && text.empty()) continue;
            Statement s;s.text=text;
            if(text.ends_with('{')) {
                bool loop=text.starts_with("for (") || text.starts_with("while (");
                if(!loop && !text.starts_with("if (")) throw std::logic_error("unknown generated control block: "+text);
                s.kind=loop ? Statement::Loop : Statement::If;
                s.text=text.starts_with("for (") ? "1" : text.substr(text.find('(')+1,text.rfind(')')-text.find('(')-1);
                s.yes=block();
                if(at==lines.size()) throw std::logic_error("unclosed generated control block");
                if(lines[at++]=="} else {") {
                    s.no=block();
                    if(at==lines.size() || lines[at++]!="}") throw std::logic_error("unclosed generated alternative");
                }
            }
            result.push_back(std::move(s));
        }
        return result;
    }
    unsigned append(Step step) {steps.push_back(std::move(step));return static_cast<unsigned>(steps.size()-1);}
    unsigned close(const std::vector<Statement>& block,unsigned next,unsigned stop=0,unsigned again=0) {
        for(auto i=block.size();i-->0;) {
            const auto& s=block[i];
            if(s.kind==Statement::Loop) {
                unsigned condition=append({});auto body=close(s.yes,condition,next,condition);
                steps[condition]={"",rewrite(s.text),body,next};next=condition;
            } else if(s.kind==Statement::If) {
                auto yes=close(s.yes,next,stop,again),no=close(s.no,next,stop,again);
                next=append({"",rewrite(s.text),yes,no});
            } else if(s.text=="break;") next=stop;
            else if(s.text=="continue;") next=again;
            else if(s.text.starts_with("return ")) next=append({"frame->returned = "+rewrite(s.text.substr(7)),"",0,0});
            else next=append({rewrite(s.text),"",next,0});
        }
        return next;
    }
};
}
BankChunks splitBankFunction(const std::string& name,const std::string& signature,
                            const std::string& body,unsigned operations,bool staticFrame) {
    Builder b;b.returns=!signature.starts_with("void ");
    auto open=signature.find('('),end=signature.find(')',open);
    std::istringstream parameters(signature.substr(open+1,end-open-1));std::string parameter;
    while(std::getline(parameters,parameter,',')) {
        parameter=trim(parameter);if(parameter=="void" || parameter.empty()) continue;
        if(!b.declaration(parameter,true)) throw std::logic_error("unknown generated parameter: "+parameter);
    }
    std::istringstream input(body);std::string line;
    while(std::getline(input,line)) b.lines.push_back(trim(line));
    if(b.lines.empty() || b.lines[0]!="{") throw std::logic_error("missing generated function body");
    b.at=1;auto statements=b.block();auto entry=b.close(statements,0);
    if(b.steps.size()-1<=operations) return {};
    if(b.steps.size()>65536) throw std::runtime_error("bank continuation exceeds 16-bit control-state capacity");
    BankChunks result;const auto frame="vxf_"+name;
    std::ostringstream header,dispatcher;
    header<<"#ifndef VX_FRAME_"<<name<<"\n#define VX_FRAME_"<<name<<"\n#include \"vexel.h\"\nstruct "<<frame<<" {\n    uint16_t pc;\n";
    if(b.returns) header<<"    uint32_t returned;\n";
    for(const auto& field:b.fields) header<<"    "<<field<<'\n';
    header<<"};\n";
    dispatcher<<" {\n    "<<(staticFrame ? "static " : "")<<"struct "<<frame<<" activation;\n    struct "<<frame<<" *frame = &activation;\n";
    for(const auto& p:b.parameters) dispatcher<<"    frame->"<<p<<" = "<<p<<";\n";
    dispatcher<<"    frame->pc = "<<entry<<";\n    while (frame->pc) {\n        switch (";
    if((operations&(operations-1))==0) dispatcher<<"(frame->pc - 1u) / "<<operations<<"u";
    else dispatcher<<"vx_divmod(frame->pc - 1UL, "<<operations<<"UL, 16, 0, 0)";
    dispatcher<<") {\n";
    for(unsigned first=1,chunk=0;first<b.steps.size();first+=operations,++chunk) {
        auto symbol="vx_chunk_"+name+"_"+std::to_string(chunk),module="vxp_chunk_"+name+"_"+std::to_string(chunk);
        auto prototype="void "+symbol+"(struct "+frame+" *frame) VX_ABI VX_RESIDENT";
        header<<prototype<<";\n";
        dispatcher<<"        case "<<chunk<<": "<<symbol<<"(frame); break;\n";
        std::ostringstream code;
        code<<" {\n    for (;;) {\n        switch (frame->pc) {\n";
        for(unsigned pc=first;pc<std::min(first+operations,static_cast<unsigned>(b.steps.size()));++pc) {
            const auto& step=b.steps[pc];code<<"        case "<<pc<<":\n";
            if(!step.code.empty()) code<<"            "<<step.code<<'\n';
            code<<"            frame->pc = ";
            if(step.condition.empty()) code<<step.yes;
            else code<<'('<<step.condition<<") ? "<<step.yes<<" : "<<step.no;
            code<<"; break;\n";
        }
        code<<"        default: return;\n        }\n    }\n}\n";
        result.parts.push_back({symbol,module,prototype,code.str()});
    }
    dispatcher<<"        default: vx_trap(24); return"<<(b.returns ? " 0" : "")<<";\n        }\n    }\n";
    if(b.returns) dispatcher<<"    return frame->returned;\n";
    dispatcher<<"}\n";header<<"#endif\n";
    result.header=header.str();result.body=dispatcher.str();return result;
}
}
