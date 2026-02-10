#include "annotation_validator.h"

namespace vexel {

void validate_annotations(const Module& /*mod*/) {
    // Annotations are opaque frontend metadata. Backend-specific semantics are
    // interpreted exclusively by the selected backend.
}

} // namespace vexel
