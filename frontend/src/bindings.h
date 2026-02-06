#pragma once
#include "symbols.h"
#include <unordered_map>

namespace vexel {

struct BindingKey {
    int instance_id = -1;
    const void* node = nullptr;

    bool operator==(const BindingKey& other) const {
        return instance_id == other.instance_id && node == other.node;
    }
};

struct BindingKeyHash {
    size_t operator()(const BindingKey& key) const noexcept {
        return std::hash<int>()(key.instance_id) ^ (std::hash<const void*>()(key.node) << 1);
    }
};

class Bindings {
public:
    void bind(int instance_id, const void* node, Symbol* sym) {
        if (!node) return;
        symbol_map[{instance_id, node}] = sym;
    }

    Symbol* lookup(int instance_id, const void* node) const {
        auto it = symbol_map.find({instance_id, node});
        if (it == symbol_map.end()) return nullptr;
        return it->second;
    }

    void set_new_variable(int instance_id, const void* node, bool value) {
        if (!node) return;
        new_var_map[{instance_id, node}] = value;
    }

    bool is_new_variable(int instance_id, const void* node) const {
        auto it = new_var_map.find({instance_id, node});
        if (it == new_var_map.end()) return false;
        return it->second;
    }

private:
    std::unordered_map<BindingKey, Symbol*, BindingKeyHash> symbol_map;
    std::unordered_map<BindingKey, bool, BindingKeyHash> new_var_map;
};

} // namespace vexel
