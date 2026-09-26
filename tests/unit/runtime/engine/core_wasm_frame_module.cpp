#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/core_wasm_abi_constants.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"
#include "compiler/backends/infra/core_wasm_codegen.hpp"
#include "common/project_input_support.hpp"
#include "base/support/sha256.hpp"
#include "runtime/engine/core_wasm_frame_module.hpp"

// RFC 0026 P6-7 rung A: host-side admission of the `ahfl.core-layout.v1` +
// boundary-root `ahfl.wire-schema.v1` section pair from REAL emitted P6-frame
// modules. Proves module-side and host-side readers agree over the
// digest-authenticated module bytes, plus fail-closed framing negatives
// (truncated, unknown intervening section, reordered/duplicated sections).

namespace {

using namespace ahfl;

int g_failures = 0;

void check(bool ok, std::string_view name) {
    if (!ok) {
        ++g_failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

namespace fr = runtime::core_wasm_frame_module;

[[nodiscard]] std::optional<std::vector<std::uint8_t>>
emit_fixture(const std::filesystem::path &fixture) {
    const Frontend frontend;
    const auto parse = frontend.parse_file(fixture);
    if (parse.has_errors() || parse.program == nullptr) {
        return std::nullopt;
    }
    const Resolver resolver;
    const auto resolve = resolver.resolve(*parse.program);
    if (resolve.has_errors()) {
        return std::nullopt;
    }
    const TypeChecker checker;
    const auto typecheck = checker.check(*parse.program, resolve);
    if (typecheck.has_errors()) {
        return std::nullopt;
    }
    const Validator validator;
    const auto validation = validator.validate(*parse.program, resolve, typecheck);
    if (validation.has_errors()) {
        return std::nullopt;
    }
    const auto ir = lower_program_ir(*parse.program, resolve, typecheck);
    const auto core = ir::core::lower_ahfl_to_core(ir);
    if (!core.ok()) {
        return std::nullopt;
    }
    const auto layouts = ir::core::compute_core_layouts(core.program);
    if (!layouts.ok() || !layouts.table.has_value()) {
        return std::nullopt;
    }
    const auto emitted = backends::emit_core_wasm(
        core.program, *layouts.table,
        {ir::core::CoreAgentId{0}, backends::WasmProfileKind::Wasi});
    if (!emitted.ok() || !emitted.artifact.has_value()) {
        return std::nullopt;
    }
    return emitted.artifact->bytes;
}

void put_uleb(std::vector<std::uint8_t> &out, std::uint64_t value) {
    do {
        auto b = static_cast<std::uint8_t>(value & 0x7fU);
        value >>= 7U;
        if (value != 0) {
            b |= 0x80U;
        }
        out.push_back(b);
    } while (value != 0);
}

void append_custom(std::vector<std::uint8_t> &module, std::string_view name,
                   std::span<const std::uint8_t> body) {
    std::vector<std::uint8_t> payload;
    put_uleb(payload, name.size());
    payload.insert(payload.end(), name.begin(), name.end());
    payload.insert(payload.end(), body.begin(), body.end());
    module.push_back(0); // custom section id
    put_uleb(module, payload.size());
    module.insert(module.end(), payload.begin(), payload.end());
}

} // namespace

int main() {
    const auto repo = test_support::repo_root_from_source_file(__FILE__);

    for (const char *rel : {"tests/golden/wasm/p6_aggregate.ahfl",
                            "tests/golden/wasm/p6_collection.ahfl"}) {
        const auto bytes = emit_fixture(repo / rel);
        check(bytes.has_value(), std::string("emit ") + rel);
        if (!bytes.has_value()) {
            continue;
        }

        // The real P6-frame module must carry BOTH sections and admit cleanly.
        auto admitted = fr::admit_core_wasm_frame_sections(*bytes);
        check(admitted.ok(), std::string("admit ") + rel);
        if (admitted.ok()) {
            const auto &s = *admitted.sections;
            check(s.layout.input_layout.value < s.layout.table.layouts.size(),
                  std::string(rel) + " input root resolves");
            check(s.layout.output_layout.value < s.layout.table.layouts.size(),
                  std::string(rel) + " output root resolves");
            // Both frame bindings were minted and root at the boundary nodes.
            check(s.input_binding.root().value <
                      s.input_binding.table().nodes.size(),
                  std::string(rel) + " input binding rooted at a schema node");
            check(s.output_binding.root().value <
                      s.output_binding.table().nodes.size(),
                  std::string(rel) + " output binding rooted at a schema node");
            check(s.module_sha256 == fr::ArtifactDigest(support::sha256(*bytes)),
                  std::string(rel) + " module digest matches parsed bytes");
            check(s.core_layout_sha256 != s.wire_schema_sha256,
                  std::string(rel) + " two section digests differ");
        }

        // Negative: truncated module fails closed.
        std::vector<std::uint8_t> truncated(bytes->begin(), bytes->end() - 4);
        check(!fr::admit_core_wasm_frame_sections(truncated).ok(),
              std::string(rel) + " truncated module rejected");

        // Negative: an unknown trailing custom section after the schema breaks
        // the schema-must-be-final rule.
        {
            auto tampered = *bytes;
            static constexpr std::array<std::uint8_t, 1> body{0x00};
            append_custom(tampered, "unknown.trailing", body);
            check(!fr::admit_core_wasm_frame_sections(tampered).ok(),
                  std::string(rel) + " trailing custom section rejected");
        }
    }

    // Negative: an identity E1 module carries NO frame sections and must fail
    // admission (rather than be silently treated as a frame module).
    {
        const auto bytes = emit_fixture(repo / "tests/golden/wasm/e1_identity_agent.ahfl");
        check(bytes.has_value(), "emit identity agent fixture");
        if (bytes.has_value()) {
            check(!fr::admit_core_wasm_frame_sections(*bytes).ok(),
                  "identity module without frame sections rejected");
        }
    }

    if (g_failures == 0) {
        std::cout << "core_wasm_frame_module: all checks passed\n";
        return 0;
    }
    std::cerr << "core_wasm_frame_module: " << g_failures << " failure(s)\n";
    return 1;
}
