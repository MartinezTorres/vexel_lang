#include "backend_registry.h"

namespace vexel {

static std::vector<Backend>& backend_registry() {
    static std::vector<Backend> backends;
    return backends;
}

bool register_backend(Backend backend) {
    if (backend.info.name.empty() || backend.emit == nullptr) {
        return false;
    }
    auto& backends = backend_registry();
    for (const auto& existing : backends) {
        if (existing.info.name == backend.info.name) {
            return false;
        }
    }
    backends.push_back(std::move(backend));
    return true;
}

const Backend* find_backend(const std::string& name) {
    for (const auto& backend : backend_registry()) {
        if (backend.info.name == name) {
            return &backend;
        }
    }
    return nullptr;
}

std::vector<BackendInfo> list_backends() {
    std::vector<BackendInfo> out;
    for (const auto& backend : backend_registry()) {
        out.push_back(backend.info);
    }
    return out;
}

}
