#include <doctest.h>

#include "ahfl/compiler/ir/tower.hpp"

// RFC 0026 slice P1 / KR6.2: runtime coverage of the IR tower's layer/path
// fork. The design invariants are also locked at compile time in
// src/compiler/ir/tower.cpp via static_assert; these cases give the tower a
// registered ctest so the skeleton is exercised (not just compiled) and so a
// later slice that re-points a layer's path is caught by a failing test as well
// as a failing build.

namespace {

using ahfl::ir::tower::Layer;
using ahfl::ir::tower::Path;
using ahfl::ir::tower::path_of;

TEST_CASE("IR tower layers lower in TypedHir -> AhflIr -> CoreIr order") {
    CHECK(static_cast<int>(Layer::TypedHir) == 0);
    CHECK(static_cast<int>(Layer::AhflIr) == 1);
    CHECK(static_cast<int>(Layer::CoreIr) == 2);
}

TEST_CASE("IR tower forks: verification consumes AhflIr, execution consumes CoreIr") {
    // Dafny-style split (RFC 0026 Design): temporal/contract stay on the
    // verification path, monomorphization/memory-layout on the execution path.
    CHECK(path_of(Layer::TypedHir) == Path::Diagnostics);
    CHECK(path_of(Layer::AhflIr) == Path::Verification);
    CHECK(path_of(Layer::CoreIr) == Path::Execution);
}

TEST_CASE("IR tower layer tags carry their path in the type") {
    CHECK(ahfl::ir::tower::AhflIrTag::path == Path::Verification);
    CHECK(ahfl::ir::tower::CoreIrTag::path == Path::Execution);
    CHECK(ahfl::ir::tower::TypedHirTag::layer == Layer::TypedHir);
}

} // namespace
