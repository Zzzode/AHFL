#include "ahfl/compiler/ir/tower.hpp"

// RFC 0026 slice P1 / KR6.2: compile-time lock on the IR tower's layer/path
// fork. These static_asserts are the executable form of the tower's design
// invariants — if a later slice accidentally re-points a layer to the wrong
// path, the build fails here rather than silently miscompiling. No runtime
// code: this TU exists so the skeleton is compiled under -Werror and so the
// invariants have a single, discoverable home.

namespace ahfl::ir::tower {
namespace {

// The three layers lower in order TypedHir(0) -> AhflIr(1) -> CoreIr(2).
static_assert(static_cast<std::uint8_t>(Layer::TypedHir) == 0);
static_assert(static_cast<std::uint8_t>(Layer::AhflIr) == 1);
static_assert(static_cast<std::uint8_t>(Layer::CoreIr) == 2);

// The Dafny-style fork: verification consumes AhflIr, execution consumes
// CoreIr, and the diagnostics layer sits above the emit fork.
static_assert(path_of(Layer::TypedHir) == Path::Diagnostics);
static_assert(path_of(Layer::AhflIr) == Path::Verification);
static_assert(path_of(Layer::CoreIr) == Path::Execution);

// Layer tags carry their path in the type (zero-cost), so consumers can be
// constrained at compile time in later slices.
static_assert(AhflIrTag::path == Path::Verification);
static_assert(CoreIrTag::path == Path::Execution);
static_assert(TypedHirTag::layer == Layer::TypedHir);

} // namespace
} // namespace ahfl::ir::tower
