#include "analysis_report.h"
#include <algorithm>
#include <sstream>
#include <vector>

namespace vexel {

namespace {
std::string mutability_label(VarMutability mut) {
    switch (mut) {
        case VarMutability::Mutable:
            return "mutable";
        case VarMutability::Constexpr:
            return "constexpr";
    }
    return "unknown";
}

std::string symbol_label(const Symbol* sym) {
    if (!sym) return "<unknown>";
    std::string label = sym->name;
    if (sym->instance_id >= 0) {
        label += "@" + std::to_string(sym->instance_id);
    }
    return label;
}
}

std::string format_analysis_report(const Module& mod, const AnalysisFacts& analysis,
                                   const OptimizationFacts* optimization) {
    std::ostringstream out;
    out << "# Vexel Analysis Report\n";
    if (!mod.name.empty()) {
        out << "Module: " << mod.name << "\n";
    }
    out << "\n";

    if (optimization) {
        out << "## Optimization Summary\n";
        out << "- Constexpr expressions: " << optimization->constexpr_values.size() << "\n";
        out << "- Constexpr inits: " << optimization->constexpr_inits.size() << "\n";
        out << "- Foldable functions: " << optimization->foldable_functions.size() << "\n";
        out << "- Constexpr conditions: " << optimization->constexpr_conditions.size() << "\n\n";

        std::vector<const Symbol*> skipped;
        skipped.reserve(optimization->fold_skip_reasons.size());
        for (const auto& pair : optimization->fold_skip_reasons) {
            skipped.push_back(pair.first);
        }
        std::sort(skipped.begin(), skipped.end(), [](const Symbol* a, const Symbol* b) {
            if (!a || !b) return a < b;
            if (a->name == b->name) return a->instance_id < b->instance_id;
            return a->name < b->name;
        });

        out << "## Fold Skip Reasons\n";
        for (const auto& sym : skipped) {
            out << "- " << symbol_label(sym) << ": " << optimization->fold_skip_reasons.at(sym) << "\n";
        }
        out << "\n";
    }

    std::vector<const Symbol*> reachable(analysis.reachable_functions.begin(),
                                         analysis.reachable_functions.end());
    std::sort(reachable.begin(), reachable.end(), [](const Symbol* a, const Symbol* b) {
        if (!a || !b) return a < b;
        if (a->name == b->name) return a->instance_id < b->instance_id;
        return a->name < b->name;
    });
    out << "## Reachable Functions\n";
    for (const auto& sym : reachable) {
        out << "- " << symbol_label(sym) << "\n";
    }
    out << "\n";

    std::vector<const Symbol*> reent_keys;
    reent_keys.reserve(analysis.reentrancy_variants.size());
    for (const auto& pair : analysis.reentrancy_variants) {
        reent_keys.push_back(pair.first);
    }
    std::sort(reent_keys.begin(), reent_keys.end(), [](const Symbol* a, const Symbol* b) {
        if (!a || !b) return a < b;
        if (a->name == b->name) return a->instance_id < b->instance_id;
        return a->name < b->name;
    });
    out << "## Reentrancy Variants\n";
    for (const auto& sym : reent_keys) {
        const auto& variants = analysis.reentrancy_variants.at(sym);
        std::vector<char> sorted(variants.begin(), variants.end());
        std::sort(sorted.begin(), sorted.end());
        std::string tags;
        for (char v : sorted) {
            if (!tags.empty()) tags += ",";
            tags.push_back(v);
        }
        out << "- " << symbol_label(sym) << ": " << tags << "\n";
    }
    out << "\n";

    std::vector<const Symbol*> ref_syms;
    ref_syms.reserve(analysis.ref_variants.size());
    for (const auto& pair : analysis.ref_variants) {
        ref_syms.push_back(pair.first);
    }
    std::sort(ref_syms.begin(), ref_syms.end(), [](const Symbol* a, const Symbol* b) {
        if (!a || !b) return a < b;
        if (a->name == b->name) return a->instance_id < b->instance_id;
        return a->name < b->name;
    });
    out << "## Ref Variants\n";
    for (const auto& sym : ref_syms) {
        const auto& masks = analysis.ref_variants.at(sym);
        std::vector<std::string> sorted_masks(masks.begin(), masks.end());
        std::sort(sorted_masks.begin(), sorted_masks.end());
        out << "- " << symbol_label(sym) << ": ";
        for (size_t i = 0; i < sorted_masks.size(); ++i) {
            if (i > 0) out << ", ";
            out << (sorted_masks[i].empty() ? "<default>" : sorted_masks[i]);
        }
        out << "\n";
    }
    out << "\n";

    std::vector<std::string> mut_lines;
    mut_lines.reserve(analysis.var_mutability.size());
    for (const auto& pair : analysis.var_mutability) {
        mut_lines.push_back(symbol_label(pair.first) + " -> " + mutability_label(pair.second));
    }
    std::sort(mut_lines.begin(), mut_lines.end());
    out << "## Variable Mutability\n";
    for (const auto& line : mut_lines) {
        out << "- " << line << "\n";
    }
    out << "\n";

    std::vector<std::string> used_globals;
    used_globals.reserve(analysis.used_global_vars.size());
    for (const auto& sym : analysis.used_global_vars) {
        used_globals.push_back(symbol_label(sym));
    }
    std::sort(used_globals.begin(), used_globals.end());
    out << "## Used Globals\n";
    for (const auto& name : used_globals) {
        out << "- " << name << "\n";
    }
    out << "\n";

    std::vector<std::string> used_types(analysis.used_type_names.begin(),
                                        analysis.used_type_names.end());
    std::sort(used_types.begin(), used_types.end());
    out << "## Used Types\n";
    for (const auto& name : used_types) {
        out << "- " << name << "\n";
    }

    return out.str();
}

} // namespace vexel
