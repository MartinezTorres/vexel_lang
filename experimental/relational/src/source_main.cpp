#include "source.hpp"
#include "vexel.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
using namespace vx::relational;
unsigned natural(const std::string& text) {
    if(text.empty() || text.find_first_not_of("0123456789")!=std::string::npos) throw std::invalid_argument("expected an unsigned decimal number");
    auto n=std::stoul(text);if(n>100000000) throw std::invalid_argument("numeric option is too large");return static_cast<unsigned>(n);
}
void write(const std::filesystem::path& path,const std::string& text) {
    std::ofstream file(path);if(!file || !(file<<text)) throw std::runtime_error("cannot write "+path.string());
}
std::pair<std::string,std::string> pairOption(const std::string& text) {
    auto colon=text.find(':');
    if(colon==std::string::npos || colon==0 || colon+1==text.size())
        throw std::invalid_argument("expected NAME:NAME relationship");
    return {text.substr(0,colon),text.substr(colon+1)};
}
struct Command {
    std::filesystem::path source,output;
    std::vector<SourceOptions> requests;
};
Command parse(const std::vector<std::string>& args,bool session=false) {
        if(args.empty()) throw std::invalid_argument("expected a source path");
        Command command;command.source=args[0];
        SourceOptions options;
        for(std::size_t i=1;i<args.size();++i) {
            std::string flag=args[i];auto value=[&]() -> std::string {if(++i==args.size()) throw std::invalid_argument("missing value for "+flag);return args[i];};
            if(flag=="--query") options.queries.push_back(value());
            else if(flag=="--source") options.sources.push_back(value());
            else if(flag=="--stream") {
                auto [name,count]=pairOption(value());auto size=natural(count);
                if(size==0 || size>255) throw std::invalid_argument("stream capacity must be 1..255");
                auto [entry,inserted]=options.streams.emplace(name,size);
                if(!inserted && entry->second!=size) throw std::invalid_argument("conflicting stream capacities: "+name);
                if(inserted) options.sources.push_back(name);
            }
            else if(flag=="--occurrence") {
                auto [name,sourceName]=pairOption(value());
                if(!options.occurrenceSources.emplace(name,sourceName).second)
                    throw std::invalid_argument("duplicate source occurrence: "+name);
                options.sources.push_back(name);
            }
            else if(flag=="--event-order") options.eventOrder.push_back(pairOption(value()));
            else if(flag=="--mode") options.activation=value();
            else if(flag=="--budget") {options.payloadBudget=natural(value());options.configuration.push_back({"require","payload_bytes",options.payloadBudget});}
            else if(flag=="--frame-ram-bytes") {options.frameRamBytes=natural(value());options.configuration.push_back({"prefer","frame_ram_bytes",options.frameRamBytes});}
            else if(flag=="--fact" || flag=="--require" || flag=="--prefer") {
                auto text=value();auto equals=text.find('=');
                if(equals==std::string::npos) throw std::invalid_argument("configuration needs KEY=NUMBER");
                options.configuration.push_back({flag.substr(2),text.substr(0,equals),natural(text.substr(equals+1))});
            }
            else if(flag=="--sink-no-feedback") options.boundaries[value()].noFeedback=true;
            else if(flag=="--resource-read" || flag=="--resource-write") {
                auto [symbol,resource]=pairOption(value());auto& contract=options.boundaries[symbol];
                (flag=="--resource-read" ? contract.reads : contract.writes).insert(resource);
            }
            else if(flag=="--access-order") options.accessOrder.push_back(pairOption(value()));
            else if(flag=="--schedule") options.search.schedule=value();
            else if(flag=="--branch-order") options.search.branches=value();
            else if(flag=="--max-nodes") options.maxNodes=natural(value());
            else if(flag=="--allow-processes") options.allowProcesses=true;
            else if(flag=="--process-timeout-ms") options.processTimeoutMs=natural(value());
            else if(flag=="--max-file-bytes") options.maxFileBytes=natural(value());
            else if(flag=="--max-work") options.search.maxWork=natural(value());
            else if(flag=="--max-decisions") options.search.maxDecisions=natural(value());
            else if(flag=="--emit-c") command.output=value();
            else if(flag=="--then") {
                if(session) throw std::invalid_argument("use one solve command per session request; --then retains a fixed snapshot");
                command.requests.push_back(options);
            }
            else throw std::invalid_argument("unknown option: "+flag);
        }
        command.requests.push_back(options);return command;
}
void emit(const SourceResult& answer,const std::filesystem::path& output) {
    if(output.empty()) return;
    if(answer.resolution.outcome!=Outcome::Resolved || answer.implementation.empty())
        throw std::runtime_error("a resolved physical plan is required for C emission");
    if(std::filesystem::exists(output) && (!std::filesystem::is_directory(output) || !std::filesystem::is_empty(output)))
        throw std::runtime_error("C output directory must be new or empty");
    std::filesystem::create_directories(output);
    write(output/"vexel.h",answer.header);write(output/"vx_bridges.c",answer.implementation);write(output/"source-plan.json",answer.json);
    for(const auto& [name,contents]:answer.files) write(output/name,contents);
}
int session(bool verify) {
    SourceWorkspace workspace;std::string line;
    while(std::getline(std::cin,line)) {
        try {
            std::istringstream input(line);std::vector<std::string> args;
            while(input>>std::ws && input.peek()!=std::char_traits<char>::eof()) {
                std::string arg;
                if(!(input>>std::quoted(arg))) throw std::invalid_argument("invalid session quoting; use double-quoted strings");
                args.push_back(std::move(arg));
            }
            if(args.empty()) continue;
            if(args==std::vector<std::string>{"quit"}) return 0;
            if(args==std::vector<std::string>{"clear"}) {
                workspace.clear();std::cout<<"{\"status\":\"cleared\"}\n"<<std::flush;continue;
            }
            if(args[0]!="solve") throw std::invalid_argument("expected solve FILE [options], clear, or quit");
            args.erase(args.begin());auto command=parse(args,true);
            auto result=workspace.solve(command.source,command.requests[0],verify);
            emit(result.answer,command.output);
            std::cout<<result.json()<<std::flush;
        } catch(const std::exception& error) {
            // One reply per request, and no last-known-good projection after
            // an invalid edit. The user can repair the file and request again.
            std::cout<<"{\"status\":\"error\",\"error\":"<<vx::quote(error.what())<<"}\n"<<std::flush;
        }
    }
    return std::cin.bad() ? 2 : 0;
}
}
int main(int argc,char** argv) {
    const std::string usage =
        "usage: vexel FILE.vx [options]\n"
        "       vexel --session [--verify-incremental]\n"
        "  --query type:NAME | value:NAME | length:NAME\n"
        "  --source NAME             Resolve a source activation\n"
        "  --stream NAME:CAPACITY    Repeated serial activations with leased results\n"
        "  --emit-c DIRECTORY        Emit into a new or empty directory\n"
        "  --mode serial|overlap|retained|graph|open\n"
        "  --budget N                Output payload budget in bytes\n"
        "  --frame-ram-bytes N        Budget for exclusive local buffers\n"
        "  --fact KEY=N | --require KEY=N | --prefer KEY=N\n"
        "  --occurrence NAME:SOURCE  Name a distinct source occurrence\n"
        "  --event-order A.begin:B.produce | --access-order A:B\n"
        "  --sink-no-feedback NAME   Close a sink's activation feedback\n"
        "  --resource-read NAME:RESOURCE | --resource-write NAME:RESOURCE\n"
        "  --schedule fifo|lifo|random --branch-order forward|reverse\n"
        "  --max-work N --max-decisions N --max-nodes N\n"
        "  --allow-processes --process-timeout-ms N --max-file-bytes N\n"
        "  --then                    Start another request on the same inputs\n"
        "Session: solve FILE [options], clear, quit (one command per line).\n"
        "Double-quote arguments containing spaces; commands do not use a shell.\n"
        "Each solve observes current inputs; up to 32 closed request groups are retained.\n"
        "--verify-incremental also solves cache hits afresh and compares reports and C.\n";
    try {
        if(argc==2 && std::string(argv[1])=="--help") {std::cout<<usage;return 0;}
        if(argc<2) throw std::invalid_argument(usage);
        if(std::string(argv[1])=="--session") {
            if(argc==2) return session(false);
            if(argc==3 && std::string(argv[2])=="--verify-incremental") return session(true);
            throw std::invalid_argument("usage: vexel --session [--verify-incremental]");
        }
        auto command=parse(std::vector<std::string>(argv+1,argv+argc));
        SourceProgram program(command.source);int status=0;
        for(std::size_t i=0;i<command.requests.size();++i) {
            auto answer=program.solve(command.requests[i]);std::cout<<answer.json;
            status=answer.resolution.outcome==Outcome::Resolved ? 0 : 1;
            if(i+1==command.requests.size()) emit(answer,command.output);
        }
        return status;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 2;}
}
