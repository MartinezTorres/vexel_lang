#include "compiler.h"
#include "codegen.h"
#include <filesystem>
#include <functional>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace vexel {

namespace {

static bool has_annotation(const std::vector<Annotation>& anns, const std::string& name) {
    for (const auto& ann : anns) {
        if (ann.name == name) return true;
    }
    return false;
}

static std::string fq_name(StmtPtr func) {
    if (!func) return "";
    if (func->type_namespace.empty()) return func->func_name;
    return func->type_namespace + "::" + func->func_name;
}

} // namespace

void Compiler::emit_banked_backend(const Module& mod, TypeChecker& checker,
                                   CodeGenerator& codegen, const CCodegenResult& result,
                                   const OutputPaths& paths) {
    (void)mod;
    (void)checker;

    std::filesystem::path legacy_path = paths.dir / (paths.stem + ".c");
    if (std::filesystem::exists(legacy_path)) {
        std::filesystem::remove(legacy_path);
    }

    // Write header file once
    std::ostringstream header_builder;
    header_builder << result.header;
    header_builder << "\nextern int __vexel_current_page;\n";
    for (const auto& info : codegen.functions()) {
        if (!info.declaration) continue;
        std::string ret = build_return_type(codegen, info.declaration);
        std::string params = build_param_list(codegen, info.declaration, true);
        header_builder << ret << " " << info.c_name << "_pageA(" << params << ");\n";
        header_builder << ret << " " << info.c_name << "_pageB(" << params << ");\n";
    }
    std::filesystem::path header_path = paths.dir / (paths.stem + ".h");
    write_file(header_path.string(), header_builder.str());
    std::string header_include = "#include \"" + header_path.filename().string() + "\"\n";

    // Runtime state shared across modules
    std::filesystem::path runtime_path = paths.dir / (paths.stem + "__runtime.c");
    std::ostringstream runtime_builder;
    runtime_builder << header_include << "#include \"megalinker.h\"\n";
    runtime_builder << "int __vexel_current_page = 0;\n";

    // Build quick lookup of functions for alternation checks
    std::unordered_map<std::string, StmtPtr> func_map;
    std::unordered_map<std::string, int> simple_counts;
    for (const auto& info : codegen.functions()) {
        if (!info.declaration) continue;
        func_map[fq_name(info.declaration)] = info.declaration;
        simple_counts[info.declaration->func_name]++;
    }
    std::unordered_map<std::string, std::string> fq_to_cname;
    std::unordered_map<std::string, std::string> simple_to_fq;
    for (const auto& info : codegen.functions()) {
        if (!info.declaration) continue;
        if (simple_counts[info.declaration->func_name] == 1) {
            simple_to_fq[info.declaration->func_name] = fq_name(info.declaration);
        }
        fq_to_cname[fq_name(info.declaration)] = info.c_name;
    }
    std::function<void(ExprPtr, std::unordered_set<std::string>&)> collect_calls =
        [&](ExprPtr expr, std::unordered_set<std::string>& out) {
            if (!expr) return;
            if (expr->kind == Expr::Kind::Call && expr->operand && expr->operand->kind == Expr::Kind::Identifier) {
                out.insert(expr->operand->name);
            }
            collect_calls(expr->left, out);
            collect_calls(expr->right, out);
            collect_calls(expr->operand, out);
            collect_calls(expr->condition, out);
            collect_calls(expr->true_expr, out);
            collect_calls(expr->false_expr, out);
            for (const auto& arg : expr->args) collect_calls(arg, out);
            for (const auto& elem : expr->elements) collect_calls(elem, out);
            for (const auto& st : expr->statements) {
                if (!st) continue;
                if (st->expr) collect_calls(st->expr, out);
                if (st->return_expr) collect_calls(st->return_expr, out);
            }
            collect_calls(expr->result_expr, out);
        };

    // Build adjacency and enforce simple alternation: non-reentrant functions must alternate pages along calls.
    std::unordered_map<std::string, std::vector<std::string>> adj;
    std::unordered_map<std::string, int> color; // -1 uncolored, 0=pageA,1=pageB
    for (const auto& info : codegen.functions()) {
        if (!info.declaration) continue;
        std::string name = fq_name(info.declaration);
        adj[name]; // ensure exists
        color[name] = -1;
        if (!info.declaration->body) continue;
        std::unordered_set<std::string> calls;
        collect_calls(info.declaration->body, calls);
        bool is_reentrant = has_annotation(info.declaration->annotations, "reentrant");
        bool self_recurses = calls.count(info.declaration->func_name) || calls.count(name);
        if (!self_recurses) {
            auto it_simple = simple_to_fq.find(info.declaration->func_name);
            if (it_simple != simple_to_fq.end()) {
                self_recurses = calls.count(it_simple->second);
            }
        }
        if (self_recurses) {
            if (!is_reentrant) {
                throw CompileError("Banked backend: recursion in '" + name + "' requires [[reentrant]]", info.declaration->location);
            }
        }
        for (const auto& callee : calls) {
            auto it = func_map.find(callee);
            std::string target = "";
            if (it != func_map.end()) {
                target = it->first;
            } else {
                auto it_simple = simple_to_fq.find(callee);
                if (it_simple != simple_to_fq.end()) {
                    target = it_simple->second;
                }
            }
            if (!target.empty()) {
                adj[name].push_back(target);
                adj[target].push_back(name); // treat alternation constraints as undirected edges
            }
        }
    }

    auto bfs_color = [&](const std::string& start) {
        std::queue<std::string> q;
        q.push(start);
        while (!q.empty()) {
            std::string cur = q.front();
            q.pop();
            int cur_color = color[cur];
            for (const auto& neigh : adj[cur]) {
                auto decl_it = func_map.find(neigh);
                if (decl_it == func_map.end()) continue;
                bool neigh_reentrant = has_annotation(decl_it->second->annotations, "reentrant");
                if (neigh_reentrant) continue;
                if (color[neigh] == -1) {
                    color[neigh] = 1 - cur_color;
                    q.push(neigh);
                } else if (color[neigh] == cur_color) {
                    throw CompileError("Banked backend: alternation conflict between '" + cur + "' and '" + neigh + "'. Mark one as [[reentrant]] to break the cycle.", decl_it->second->location);
                }
            }
        }
    };

    // Color non-reentrant exports first (alternate starting colors to avoid trivial conflicts)
    int next_export_color = 0;
    for (const auto& info : codegen.functions()) {
        if (!info.declaration || !info.declaration->is_exported) continue;
        std::string name = fq_name(info.declaration);
        if (has_annotation(info.declaration->annotations, "reentrant")) continue;
        if (color[name] == -1) {
            color[name] = next_export_color;
            next_export_color = 1 - next_export_color;
            bfs_color(name);
        }
    }
    // Color remaining non-reentrant functions
    int next_color = next_export_color;
    for (const auto& kv : color) {
        if (kv.second != -1) continue;
        auto decl_it = func_map.find(kv.first);
        if (decl_it == func_map.end()) continue;
        if (has_annotation(decl_it->second->annotations, "reentrant")) continue;
        color[kv.first] = next_color;
        next_color = 1 - next_color;
        bfs_color(kv.first);
    }

    auto ensure_fn_type = [&](TypePtr type, const SourceLocation& loc, const std::string& what) {
        if (!type) {
            throw CompileError("Banked backend requires explicit type for " + what, loc);
        }
        if (type->kind == Type::Kind::Primitive) {
            if (type->primitive == PrimitiveType::F32 || type->primitive == PrimitiveType::F64) {
                throw CompileError("Banked backend does not support floating-point " + what, loc);
            }
            return;
        }
        throw CompileError("Banked backend cannot pass '" + what + "' by value; use primitives or restructure the call", loc);
    };

    auto ensure_non_float_var = [&](TypePtr type, const SourceLocation& loc, const std::string& name) {
        if (!type) {
            throw CompileError("Banked backend requires explicit type for global '" + name + "'", loc);
        }
        if (type->kind == Type::Kind::Primitive &&
            (type->primitive == PrimitiveType::F32 || type->primitive == PrimitiveType::F64)) {
            throw CompileError("Banked backend does not support floating-point globals: " + name, loc);
        }
    };

    // Validate globals and functions
    for (const auto& stmt : mod.top_level) {
        if (!stmt) continue;
        if (stmt->kind == Stmt::Kind::VarDecl) {
            ensure_non_float_var(stmt->var_type, stmt->location, stmt->var_name);
            if (!stmt->is_mutable && !has_annotation(stmt->annotations, "nonbanked")) {
                if (!stmt->var_init || stmt->var_init->kind != Expr::Kind::ArrayLiteral) {
                    // Immutable globals default to ROM; arrays are allowed, others are considered ROM by default
                }
            }
        } else if (stmt->kind == Stmt::Kind::FuncDecl) {
            ensure_fn_type(stmt->return_type, stmt->location, "return type of " + fq_name(stmt));
            for (size_t i = 0; i < stmt->params.size(); ++i) {
                const auto& p = stmt->params[i];
                ensure_fn_type(p.type, stmt->location, "parameter " + std::to_string(i) + " of " + fq_name(stmt));
            }
            for (size_t i = 0; i < stmt->ref_params.size(); ++i) {
                (void)i;
                // ref params map to void* / receiver pointers; type checking already enforces receiver types
            }
        }
    }

    auto write_banked_file = [&](const std::string& stem, const std::string& body) {
        std::filesystem::path path = paths.dir / ("banked/" + stem);
        std::filesystem::create_directories(path.parent_path());
        write_file(path.string(), header_include + body);
    };

    // Emit per-function files (pageA/pageB and optional RAM/ROM)
    for (const auto& info : codegen.functions()) {
        if (!info.declaration) continue;
        bool reentrant = has_annotation(info.declaration->annotations, "reentrant");
        int page = reentrant ? 0 : color[fq_name(info.declaration)];
        std::string suffix = page == 0 ? "_pageA" : "_pageB";
        std::string filename = info.c_name + suffix + ".c";
        std::string header = "// page " + std::string(page == 0 ? "A" : "B") + "\n";
        write_banked_file(filename, header + info.code);
    }

    // Emit ROM globals into pageA, RAM globals into a shared file
    std::ostringstream rom;
    std::ostringstream ram;
    rom << header_include;
    ram << header_include;
    for (const auto& var : codegen.variables()) {
        if (has_annotation(var.declaration->annotations, "nonbanked") || var.declaration->is_mutable) {
            ram << var.code << "\n";
        } else {
            rom << var.code << "\n";
        }
    }
    write_banked_file("rom_globals_pageA.c", rom.str());
    write_banked_file("ram_globals.c", ram.str());

    // Compute simple co-location directives for mutually recursive non-reentrant functions
    std::unordered_map<std::string, int> index, lowlink;
    std::unordered_set<std::string> on_stack;
    std::vector<std::string> stack;
    int idx = 0;
    std::vector<std::vector<std::string>> sccs;

    std::function<void(const std::string&)> strongconnect = [&](const std::string& v) {
        index[v] = lowlink[v] = idx++;
        stack.push_back(v);
        on_stack.insert(v);
        for (const auto& w : adj[v]) {
            if (!index.count(w)) {
                strongconnect(w);
                lowlink[v] = std::min(lowlink[v], lowlink[w]);
            } else if (on_stack.count(w)) {
                lowlink[v] = std::min(lowlink[v], index[w]);
            }
        }
        if (lowlink[v] == index[v]) {
            std::vector<std::string> comp;
            while (!stack.empty()) {
                std::string w = stack.back();
                stack.pop_back();
                on_stack.erase(w);
                comp.push_back(w);
                if (w == v) break;
            }
            if (comp.size() > 1) sccs.push_back(comp);
        }
    };

    for (const auto& kv : adj) {
        if (!index.count(kv.first)) strongconnect(kv.first);
    }

    for (const auto& comp : sccs) {
        // Only emit for non-reentrant functions
        std::vector<std::string> members;
        for (const auto& name : comp) {
            auto decl_it = func_map.find(name);
            if (decl_it != func_map.end() && !has_annotation(decl_it->second->annotations, "reentrant")) {
                members.push_back(name);
            }
        }
        if (members.size() <= 1) continue;
        std::string anchor = members.front();
        std::string anchor_c = fq_to_cname[anchor];
        for (size_t i = 1; i < members.size(); ++i) {
            std::string src_c = fq_to_cname[members[i]];
            runtime_builder << "ML_MOVE_SYMBOLS_TO(" << anchor_c << "," << src_c << ");\n";
        }
    }
    write_file(runtime_path.string(), runtime_builder.str());
}

} // namespace vexel
