#include "runtime/wasm_host/p6_frame_driver.hpp"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <utility>
#include <variant>

namespace ahfl::runtime::wasm_host {

namespace {

namespace eng = ::ahfl::runtime::core_wasm_resume_engine;

// Map a packer failure into the driver's error variant.
[[nodiscard]] P6FrameError
map_pack_error(FramePackError e) noexcept {
    return P6FrameError{std::in_place_type<FramePackError>, e};
}

// Map a reader failure into the driver's error variant.
[[nodiscard]] P6FrameError
map_read_error(FrameReadError e) noexcept {
    return P6FrameError{std::in_place_type<FrameReadError>, e};
}

// Map an engine failure into the driver's error variant.
[[nodiscard]] P6FrameError
map_engine_error(eng::EngineError e) noexcept {
    return P6FrameError{std::in_place_type<eng::EngineError>, e};
}

// Map a run_frame execution failure into the driver's error variant.
[[nodiscard]] P6FrameError
map_run_frame_error(FrameRunError e) noexcept {
    return P6FrameError{std::in_place_type<FrameRunError>, e};
}

} // namespace

std::expected<std::string, P6FrameError>
execute_p6_frame(Wasm3ResumeEngine &engine,
                 std::span<const std::uint8_t> module_bytes,
                 const ahfl::ir::core::CoreFrameLayoutSection &section,
                 const ahfl::ir::core::VerifiedWireSchemaBinding &input_binding,
                 const ahfl::ir::core::VerifiedWireSchemaBinding &output_binding,
                 const Value &input, P6FinalKind final_kind,
                 eng::ImportCallback import_callback) {
    // 1. Instantiate. The engine cross-checks the fixed single-page memory and
    //    the export surface (run_workflow + alloc) before returning.
    auto inst = engine.fresh_instance(module_bytes, std::move(import_callback));
    if (!inst.has_value()) {
        return std::unexpected(map_engine_error(inst.error()));
    }

    // 2. Acquire the live mutable page and pack the input frame directly into it
    //    (the JS oracle's exact discipline: write e.memory.buffer). The packer
    //    zeroes [1024,16384) first, so padding words are 0; it never touches
    //    [0,1024) (the zero page + the rodata the Data section initialized).
    auto page = engine.mutable_whole_memory();
    if (!page.has_value()) {
        return std::unexpected(map_engine_error(page.error()));
    }
    auto packed = pack_p6_input(*page, section, input_binding, input);
    if (!packed.has_value()) {
        return std::unexpected(map_pack_error(packed.error()));
    }

    // 3. Invoke run_frame. The module walks the state machine and returns
    //    (status, value_ptr).
    auto run_frame = engine.invoke_run_frame();
    if (!run_frame.has_value()) {
        return std::unexpected(map_engine_error(run_frame.error()));
    }

    // 4. Classify the run_frame outcome. A trap (wasm trap) or host abort fails
    //    closed; a non-OK status fails closed. These are execution failures,
    //    not frame read failures, so they carry their own FrameRunError category.
    if (std::holds_alternative<eng::WorkflowRunTrapped>(*run_frame)) {
        return std::unexpected(map_run_frame_error(FrameRunError{FrameRunError::Kind::Trapped}));
    }
    if (std::holds_alternative<eng::WorkflowRunHostAborted>(*run_frame)) {
        return std::unexpected(map_run_frame_error(FrameRunError{FrameRunError::Kind::HostAborted}));
    }
    const auto &result = std::get<FrameRunResult>(*run_frame);
    if (result.raw_status != 0u) {
        return std::unexpected(
            map_run_frame_error(FrameRunError{FrameRunError::Kind::NonOkStatus, result.raw_status}));
    }

    // 5. Re-acquire the page as const (the span discipline: the mutable span
    //    is valid until the next mutating call, and invoke_run_frame mutated). The
    //    memory is the same allocation; this is a fresh view.
    auto const_page = engine.read_whole_memory();
    if (!const_page.has_value()) {
        return std::unexpected(map_engine_error(const_page.error()));
    }

    // 6. Authorize the run_frame root + walk the output frame + render canonical
    //    value JSON.
    auto output =
        encode_p6_output(*const_page, section, output_binding,
                         result.value_ptr.value, final_kind);
    if (!output.has_value()) {
        return std::unexpected(map_read_error(output.error()));
    }
    return *output;
}

} // namespace ahfl::runtime::wasm_host
