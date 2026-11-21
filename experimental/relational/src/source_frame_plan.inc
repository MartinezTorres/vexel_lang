// Physical storage policy over a closed execution graph. The input is the
// selected candidate's traces, never a syntactic list of possible callees.
// This policy trades a supplied RAM budget for fewer automatic local bytes;
// it does not infer a stack bound from those bytes.
struct FramePlanning {
    struct Node {
        Ref trace;
        std::size_t localBytes=0,calculationBytes=0,calculationIntervals=0,retainedCalculations=0;
        bool reuseScratch=true;
        bool shareResults=true;
        std::size_t resultGroups=0;
        bool reusePublication=true;
        bool narrowPublication=true;
        unsigned publicationBits=0;
        std::size_t publicationValues=0,publicationBytes=0;
        bool shareDivisions=false;
        bool unrollDivisions=true;
        bool rotationHelpers=false;
        std::size_t unrolledDivisionHelpers=0;
        std::size_t sharedDivisions=0;
        std::size_t scratchReuses=0;
        std::size_t narrowedValues=0,narrowedOperations=0;
        std::size_t linearCombinations=0;
        unsigned linearTerms=16;
        std::set<std::string> calls,sources;
        bool openBoundary=false,recursive=false,overlapping=false,staticLocals=false,allowStatic=true;
        std::string reason;
    };
    std::map<std::string,Node> nodes;
    std::set<std::string> noFeedback,roots;
    std::map<Id,std::string> initializers;
    std::size_t budget=0,used=0;
    void add(const std::string& name,Ref trace,std::size_t additional=0,bool allowStatic=true) {
        Node node;node.trace=trace;node.allowStatic=allowStatic;std::ostringstream declarations;
        node.localBytes=cTemporaries(declarations,trace)+additional;
        std::function<void(const Ref&)> visit=[&](const Ref& code) {
            if(code->name=="Invoke") node.calls.insert("vx_flow_"+show(code->arguments[1]));
            // An external implementation or persistent initializer can have
            // activation edges not represented in this function graph. Until
            // that boundary has a stronger contract its ancestors stay local.
            else if(code->name=="ExternalCall") node.openBoundary=node.openBoundary || !noFeedback.contains(code->arguments[1]->name);
            else if(code->name=="Load" || code->name=="Store") {
                node.openBoundary=true;
                // Both reads and first writes can execute a lazy initializer.
                // Its callees inherit the loading/storing source's ownership.
                auto slot=code->arguments[code->name=="Load" ? 1 : 0]->number.convert_to<Id>();
                if(auto found=initializers.find(slot);found!=initializers.end()) node.calls.insert(found->second);
            }
            else if(code->name=="Sequence") for(const auto& child:code->arguments) visit(child);
            else if(code->name=="If") {visit(code->arguments[1]);visit(code->arguments[2]);}
            else if(code->name=="Loop") {visit(code->arguments[2]);visit(code->arguments[5]);}
        };visit(trace);
        if(!nodes.emplace(name,std::move(node)).second) throw std::logic_error("duplicate physical function in frame graph: "+name);
    }
    void close(bool executionExclusive,bool finiteOccurrences,
               const std::set<std::pair<std::string,std::string>>& overlap,unsigned ramBytes) {
        budget=ramBytes;std::vector<std::string> eligible;
        // A source occurrence owns one activation. Propagate those owners over
        // actual calls; concurrent roots matter only where their paths meet.
        for(const auto& root:roots) {
            std::set<std::string> visited;
            std::function<void(const std::string&)> visit=[&](const std::string& name) {
                if(!visited.insert(name).second) return;
                auto found=nodes.find(name);
                if(found==nodes.end()) throw std::logic_error("closed frame graph lacks its invoked function");
                found->second.sources.insert(root);
                for(const auto& child:found->second.calls) visit(child);
            };visit(root);
        }
        for(auto& [name,node]:nodes) {
            std::set<std::string> visited;
            std::function<void(const std::string&)> visit=[&](const std::string& callee) {
                if(callee==name) node.recursive=true;
                if(!visited.insert(callee).second) return;
                auto found=nodes.find(callee);
                if(found==nodes.end()) throw std::logic_error("closed frame graph lacks its invoked function");
                node.openBoundary=node.openBoundary || found->second.openBoundary;
                for(const auto& child:found->second.calls) visit(child);
            };
            for(const auto& callee:node.calls) visit(callee);
            node.overlapping=!executionExclusive && !finiteOccurrences;
            if(!executionExclusive && finiteOccurrences)
                for(const auto& [a,b]:overlap) if(node.sources.contains(a) && node.sources.contains(b)) node.overlapping=true;
            if(node.recursive) node.reason="recursive activation path";
            else if(node.overlapping) node.reason="activation overlap is not excluded";
            else if(node.openBoundary) node.reason="boundary activation edges remain open";
            else if(node.sources.empty()) node.reason="activation owners remain unproved";
            else if(!node.allowStatic) node.reason="initializer locals retain automatic storage";
            else if(!node.localBytes) node.reason="no projected local buffers";
            else eligible.push_back(name);
        }
        // A deterministic feasible policy is sufficient. Largest buffers get
        // first use of the budget; complete linked RAM checks still apply.
        // The automatic alternative is always available with a zero budget.
        std::sort(eligible.begin(),eligible.end(),[&](const auto& a,const auto& b){
            return nodes.at(a).localBytes!=nodes.at(b).localBytes ? nodes.at(a).localBytes>nodes.at(b).localBytes : a<b;
        });
        for(const auto& name:eligible) {
            auto& node=nodes.at(name);
            if(node.localBytes<=budget-used) {
                node.staticLocals=true;used+=node.localBytes;node.reason="closed exclusive activation path";
            } else node.reason="static RAM budget";
        }
    }
    bool shared(const std::string& name) const {return nodes.at(name).staticLocals;}
    bool exclusive(const std::string& name) const {
        const auto& n=nodes.at(name);
        return !n.recursive && !n.overlapping && !n.openBoundary && !n.sources.empty() && n.allowStatic;
    }
    std::string policy(const std::string& name) const {return shared(name) ? "static-exclusive" : "automatic";}
    void report(std::ostream& out) const {
        out<<",\"frame_planning\":{\"ram_budget\":"<<budget<<",\"static_local_bytes\":"<<used
           <<",\"policy\":\"largest eligible buffers first\",\"stack_bound\":null,\"functions\":[";bool first=true;
        for(const auto& [name,node]:nodes) {
            if(!first) out<<',';
            first=false;out<<"{\"name\":"<<vx::quote(name)<<",\"frame_policy\":"<<vx::quote(policy(name))
                <<",\"calculation_scratch_bytes\":"<<node.calculationBytes
                <<",\"calculation_intervals\":"<<node.calculationIntervals
                <<",\"result_policy\":"<<vx::quote("calculation_result_sharing."+name)
                <<",\"result_sharing\":"<<(node.shareResults ? "true" : "false")
                <<",\"result_groups\":"<<node.resultGroups
                <<",\"publication_policy\":"<<vx::quote("calculation_publication_reuse."+name)
                <<",\"publication_reuse\":"<<(node.reusePublication ? "true" : "false")
                <<",\"publication_width_policy\":"<<vx::quote("calculation_publication_narrow."+name)
                <<",\"publication_narrow\":"<<(node.narrowPublication ? "true" : "false")
                <<",\"publication_carrier_bits\":"<<node.publicationBits
                <<",\"publication_values\":"<<node.publicationValues
                <<",\"publication_snapshot_bytes\":"<<node.publicationBytes
                <<",\"division_policy\":"<<vx::quote("calculation_division_sharing."+name)
                <<",\"division_sharing\":"<<(node.shareDivisions ? "true" : "false")
                <<",\"shared_divisions\":"<<node.sharedDivisions
                <<",\"division_unroll_policy\":"<<vx::quote("calculation_division_unroll."+name)
                <<",\"division_unroll\":"<<(node.unrollDivisions ? "true" : "false")
                <<",\"rotation_policy\":"<<vx::quote("calculation_rotation_helpers."+name)
                <<",\"rotation_helpers\":"<<(node.rotationHelpers ? "true" : "false")
                <<",\"unrolled_division_helpers\":"<<node.unrolledDivisionHelpers
                <<",\"retained_calculations\":"<<node.retainedCalculations
                <<",\"scratch_reuse\":"<<(node.reuseScratch ? "true" : "false")
                <<",\"scratch_reuses\":"<<node.scratchReuses
                <<",\"narrowed_calculations\":"<<node.narrowedValues
                <<",\"narrowed_operations\":"<<node.narrowedOperations
                <<",\"linear_combinations\":"<<node.linearCombinations
                <<",\"linear_terms\":"<<node.linearTerms
                <<",\"linear_policy\":"<<vx::quote("calculation_linear_terms."+name)
                <<",\"scratch_policy\":"<<vx::quote("calculation_scratch_reuse."+name)
                <<",\"projected_local_bytes\":"<<node.localBytes<<",\"recursive\":"<<(node.recursive ? "true" : "false")
                <<",\"open_boundary\":"<<(node.openBoundary ? "true" : "false")
                <<",\"overlapping_occurrences\":"<<(node.overlapping ? "true" : "false")<<",\"reason\":"<<vx::quote(node.reason)
                <<",\"source_occurrences\":[";
            bool firstSource=true;for(const auto& source:node.sources) {if(!firstSource) out<<',';firstSource=false;out<<vx::quote(source);}
            out<<"],\"calls\":[";
            bool firstCall=true;for(const auto& call:node.calls) {if(!firstCall) out<<',';firstCall=false;out<<vx::quote(call);}
            out<<"]}";
        }
        out<<"]}";
    }
};
