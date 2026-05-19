#include "ahfl/formal/model_checker_backend.hpp"

#include "ahfl/formal/nuxmv_backend.hpp"
#include "ahfl/formal/spin_backend.hpp"
#include "ahfl/formal/tlaplus_backend.hpp"

namespace ahfl::formal {

[[nodiscard]] std::unique_ptr<ModelCheckerBackend>
create_backend(ModelCheckerKind kind) {
    switch (kind) {
    case ModelCheckerKind::NuSMV:
    case ModelCheckerKind::NuXmv:
        return std::make_unique<NuXmvBackend>();
    case ModelCheckerKind::SPIN:
        return std::make_unique<SpinBackend>();
    case ModelCheckerKind::TLAPlus:
        return std::make_unique<TLAPlusBackend>();
    }
    return std::make_unique<NuXmvBackend>();
}

} // namespace ahfl::formal
