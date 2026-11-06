#include "source.hpp"
#include "vexel.hpp"
#include <algorithm>
#include <fstream>
#include <list>
#include <sstream>

namespace vx::relational {
namespace {
bool sameFile(const std::string& path,const std::string& expected) {
    std::error_code error;
    if(!std::filesystem::is_regular_file(path,error) || error) return false;
    std::ifstream file(path,std::ios::binary);
    if(!file) return false;
    char buffer[8192];std::size_t offset=0;
    while(file) {
        file.read(buffer,sizeof buffer);
        auto count=static_cast<std::size_t>(file.gcount());
        if(count>expected.size()-offset || expected.compare(offset,count,buffer,count)!=0) return false;
        offset+=count;
    }
    return file.eof() && offset==expected.size();
}

std::vector<std::string> changedInputs(const SourceInputs& inputs) {
    std::vector<std::string> changed;
    for(const auto& [path,bytes]:inputs.files) if(!sameFile(path,bytes)) changed.push_back(path);
    for(const auto& [key,expected]:inputs.selections) {
        std::error_code error;std::string actual;
        if(key.starts_with("path-exists:")) {
            bool present=std::filesystem::exists(key.substr(12),error);
            actual=present ? "true" : "false";
        } else if(key.starts_with("path-target:")) {
            actual=std::filesystem::weakly_canonical(key.substr(12),error).string();
        } else if(key.starts_with("source-kind:")) {
            bool regular=std::filesystem::is_regular_file(key.substr(12),error);
            // ENOENT describes the recorded 'other' kind, not an I/O failure.
            if(error==std::errc::no_such_file_or_directory) error.clear();
            actual=regular ? "file" : "other";
        } else if(key.starts_with("resource-kind:")) {
            std::filesystem::path path=key.substr(14);
            bool present=std::filesystem::exists(path,error);
            if(!error) {
                bool directory=present ? std::filesystem::is_directory(path,error) : !path.has_extension();
                actual=directory ? "directory" : "file";
            }
        } else throw std::logic_error("unrecognized input support: "+key);
        if(error || actual!=expected) changed.push_back(key);
    }
    for(const auto& [path,expected]:inputs.directories) {
        std::error_code error;std::vector<std::string> names;
        if(std::filesystem::exists(path,error) && !error) {
            std::filesystem::directory_iterator it(path,error),end;
            while(!error && it!=end) {
                if(it->is_regular_file(error)) names.push_back(it->path().filename().string());
                if(!error) it.increment(error);
                // A changed listing cannot turn unbounded growth into an
                // unbounded validation cost for a previously small group.
                if(names.size()>expected.size()) break;
            }
        }
        std::sort(names.begin(),names.end());
        if(error || names!=expected) changed.push_back("directory:"+path);
    }
    return changed;
}

bool sameAnswer(const SourceResult& a,const SourceResult& b) {
    // Each derivation uses the same options and source arena order, so the
    // complete report and projections must agree, including provenance.
    return a.resolution.outcome==b.resolution.outcome && a.json==b.json &&
        a.header==b.header && a.implementation==b.implementation && a.files==b.files;
}
}

struct SourceWorkspace::Impl {
    struct Entry {
        std::filesystem::path path;
        SourceOptions options;
        SourceResult answer;
        std::size_t revision;
    };
    std::list<Entry> entries;
    std::size_t capacity,nextRevision=1;
    explicit Impl(std::size_t limit):capacity(limit) {}
};

SourceWorkspace::SourceWorkspace(std::size_t capacity):impl(std::make_unique<Impl>(capacity)) {}
SourceWorkspace::~SourceWorkspace()=default;
void SourceWorkspace::clear() {impl->entries.clear();}

WorkspaceResult SourceWorkspace::solve(const std::filesystem::path& source,const SourceOptions& options,bool verifyReuse) {
    // Preserve the requested path: canonicalizing the key would lose a change
    // to the entry symlink, or a different project-relative import precedence.
    const auto path=std::filesystem::absolute(source);
    WorkspaceResult result;result.reason="new-request";
    auto& entries=impl->entries;
    auto found=std::find_if(entries.begin(),entries.end(),[&](const auto& entry){return entry.path==path && entry.options==options;});
    if(found!=entries.end()) {
        result.invalidatedInputs=changedInputs(*found->answer.inputs);
        if(result.invalidatedInputs.empty()) {
            if(verifyReuse) {
                auto fresh=SourceProgram(path).solve(options);
                result.verificationWork=fresh.resolution.stats.work;
                if(!sameAnswer(found->answer,fresh)) {
                    entries.erase(found);
                    throw std::runtime_error("incremental verification failed: cached and fresh answers differ for "+path.string());
                }
                result.verified=true;
                // Do not publish a reusable result if inputs changed during
                // the verification solve. The next miss gets a new revision.
                result.invalidatedInputs=changedInputs(*fresh.inputs);
            }
            if(result.invalidatedInputs.empty()) {
                result.answer=found->answer;result.revision=found->revision;
                result.reused=true;result.reason="unchanged-inputs";
                entries.splice(entries.begin(),entries,found);
                result.cacheEntries=entries.size();return result;
            }
        }
        entries.erase(found);result.reason="changed-inputs";
    }
    // Evict stale support before parsing: an invalid edit must never fall back
    // to the last valid answer. Independent groups remain in the workspace.
    result.revision=impl->nextRevision++;
    result.answer=SourceProgram(path).solve(options);
    result.solverWork=result.answer.resolution.stats.work;
    if(result.answer.resolution.outcome!=Outcome::Resolved) result.reason="not-resolved";
    else if(result.answer.inputs->hasProcesses) result.reason="process-input";
    else if(options.search.budget) result.reason="shared-search-budget";
    else if(!impl->capacity) result.reason="cache-disabled";
    else {
        auto changed=changedInputs(*result.answer.inputs);
        if(!changed.empty()) {
            result.reason="inputs-changed-during-solve";
            result.invalidatedInputs.insert(result.invalidatedInputs.end(),changed.begin(),changed.end());
        } else {
            entries.push_front({path,options,result.answer,result.revision});
            if(entries.size()>impl->capacity) entries.pop_back();
        }
    }
    result.cacheEntries=entries.size();return result;
}

std::string WorkspaceResult::json() const {
    auto end=answer.json.find_last_of('}');
    if(end==std::string::npos) throw std::logic_error("missing source report");
    std::ostringstream out;out<<answer.json.substr(0,end)<<",\"incremental\":{\"revision\":"<<revision
        <<",\"reused\":"<<(reused ? "true" : "false")<<",\"solver_work\":"<<solverWork
        <<",\"verified\":"<<(verified ? "true" : "false")<<",\"verification_work\":"<<verificationWork
        <<",\"cache_entries\":"<<cacheEntries<<",\"reason\":"<<vx::quote(reason)<<",\"invalidated_inputs\":[";
    for(std::size_t i=0;i<invalidatedInputs.size();++i) {if(i) out<<',';out<<vx::quote(invalidatedInputs[i]);}
    out<<"]}}\n";return out.str();
}
} // namespace vx::relational
