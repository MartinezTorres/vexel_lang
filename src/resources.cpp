#include "vexel.hpp"
#include "process.hpp"
#include <algorithm>

namespace vx {
void Solver::resourceRule(Id id,bool typesOnly) {
    const auto& n=g.nodes[id];
    const Type string=Type::simple(Type::String);
    if (g.nodes[n.site].key=="process") {
        if (typesOnly) { publishType(id,string);return; }
        if (!g.options.allowProcesses) fail(id,"process imports require --allow-processes");
        std::string directory=g.files[n.span.file].path.parent_path().string();
        std::string key=quote(directory)+":"+quote(n.text);
        auto found=g.processSnapshots->find(key);
        if (found==g.processSnapshots->end()) {
            auto result=runProcess({"/bin/sh","-c",n.text},{},g.options.processTimeoutMs,false,directory,g.options.maxFileBytes);
            found=g.processSnapshots->emplace(key,std::move(result)).first;
        }
        if (found->second.second!="received") fail(id,"process import failed: "+found->second.second);
        publish(id,{string,Value::string(found->second.first),Effect::Pure});return;
    }
    auto path=g.resolve(n.text,n.span);
    std::error_code error;
    path=std::filesystem::weakly_canonical(path,error);
    if (error) fail(id,"cannot resolve resource: "+n.text);
    std::string key=path.string();
    auto directory=g.directorySnapshots->find(key);
    bool exists=std::filesystem::exists(path,error);
    if (error) fail(id,"cannot inspect resource: "+key);
    std::string kindKey="resource-kind:"+key;
    auto kind=g.inputSnapshots->find(kindKey);
    bool isDirectory;
    if (kind!=g.inputSnapshots->end()) isDirectory=kind->second=="directory";
    else {
        isDirectory=directory!=g.directorySnapshots->end() || std::filesystem::is_directory(path,error) || (!exists && !path.has_extension());
        g.inputSnapshots->emplace(kindKey,isDirectory ? "directory" : "file");
    }
    if (!isDirectory) {
        if (typesOnly) {publishType(id,string);return;}
        Id file=g.read(path,false,n.span);publish(id,{string,Value::string(g.files[file].bytes),Effect::Pure});return;
    }
    if (directory==g.directorySnapshots->end()) {
        std::vector<std::string> names;
        if (exists) {
            std::filesystem::directory_iterator entries(path,error),end;
            if (error) fail(id,"cannot enumerate directory: "+key);
            for (;entries!=end;entries.increment(error)) {
                if (error) fail(id,"cannot enumerate directory: "+key);
                if (entries->is_regular_file(error)) names.push_back(entries->path().filename().string());
                if (error) fail(id,"cannot inspect directory entry: "+key);
                if (names.size()>g.options.maxElements) fail(id,"directory resource exceeds element budget",Diagnostic::Exhausted);
            }
            if (error) fail(id,"cannot enumerate directory: "+key);
        }
        std::sort(names.begin(),names.end());
        directory=g.directorySnapshots->emplace(key,std::move(names)).first;
    }
    // A missing optional directory depends on the existing parent so that a
    // build can notice a new entry without making Make require a missing file.
    auto dependency=exists ? path : path.parent_path();
    while (!std::filesystem::exists(dependency) && dependency.has_parent_path() && dependency!=dependency.parent_path()) dependency=dependency.parent_path();
    g.dependencies.insert(dependency.string());
    Type tuple=Type::record(none,"",{"__0","__1"},{string,string});
    Type array=Type::array(tuple,directory->second.size());
    if (typesOnly) {publishType(id,array);return;}
    std::vector<Value> entries;std::size_t size=0;
    for (const auto& name:directory->second) {
        Id file=g.read(path/name,false,n.span);
        const auto& contents=g.files[file].bytes;
        if (contents.size()>g.options.maxFileBytes-size) fail(id,"directory contents exceed file size budget",Diagnostic::Exhausted);
        size+=contents.size();entries.push_back(Value::array({Value::string(name),Value::string(contents)}));
    }
    publish(id,{array,Value::array(std::move(entries)),Effect::Pure});
}
} // namespace vx
