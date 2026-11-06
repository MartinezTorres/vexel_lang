#pragma once
#include "machine.hpp"
#include <filesystem>
#include <memory>

namespace vx::relational {
struct BoundaryContract {
    bool noFeedback=false;
    std::set<std::string> reads,writes;
    bool operator==(const BoundaryContract&) const = default;
};
struct SourceOptions {
    struct Configuration {
        std::string role, key;
        unsigned value=0;
        bool operator==(const Configuration&) const = default;
    };
    std::vector<Configuration> configuration;
    Options search = [] {Options options;options.maxWork=10000000;return options;}();
    std::vector<std::string> queries;
    std::vector<std::string> sources;
    std::map<std::string,unsigned> streams;
    // Boundary occurrence names are adapter identities, independent of the
    // source definition. Every occurrence owns one execution/result lifetime.
    std::map<std::string,std::string> occurrenceSources;
    std::vector<std::pair<std::string,std::string>> eventOrder;
    std::string activation = "serial";
    unsigned payloadBudget = 64;
    // Target-owned RAM budget for exclusive function-local buffers. Zero
    // retains automatic locals and permits arbitrary editable module overrides.
    unsigned frameRamBytes = 0;
    std::map<std::string,BoundaryContract> boundaries;
    std::vector<std::pair<std::string,std::string>> accessOrder;
    std::size_t maxNodes=500000;
    bool allowProcesses=false;
    std::size_t processTimeoutMs=1000,maxFileBytes=16*1024*1024;
    bool operator==(const SourceOptions&) const = default;
};
// Exact support for a closed answer, not timestamps or a hash used as proof.
// Selection facts include absent preferred paths and symlink destinations.
struct SourceInputs {
    std::map<std::string,std::string> files, selections;
    std::map<std::string,std::vector<std::string>> directories;
    bool hasProcesses=false;
};
struct SourceResult {
    Result resolution;
    std::string json;
    // A resolved physical plan includes an inspectable C projection. Empty for
    // semantic-only requests and every unfinished/contradictory search.
    std::string header, implementation;
    std::map<std::string,std::string> files;
    std::shared_ptr<const SourceInputs> inputs;
};
class SourceProgram {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    explicit SourceProgram(const std::filesystem::path& path);
    ~SourceProgram();
    SourceResult solve(const SourceOptions& options);
};

struct WorkspaceResult {
    SourceResult answer;
    std::size_t revision=0, solverWork=0, verificationWork=0, cacheEntries=0;
    bool reused=false, verified=false;
    std::string reason;
    std::vector<std::string> invalidatedInputs;
    std::string json() const;
};
// Whole requests are conservative dependency groups. Each miss constructs an
// independent input revision. No solver bindings or physical choices cross it.
class SourceWorkspace {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    explicit SourceWorkspace(std::size_t capacity=32);
    ~SourceWorkspace();
    WorkspaceResult solve(const std::filesystem::path&,const SourceOptions&,bool verifyReuse=false);
    void clear();
};
} // namespace vx::relational
