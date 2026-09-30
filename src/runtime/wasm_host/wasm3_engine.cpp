// RFC 0026 KR6.8 WH-1: the wasm3-backed CoreWasmResumeEngine port. See
// wasm3_engine.hpp for the contract and the fail-closed discipline; this file
// is the implementation.

#include "runtime/wasm_host/wasm3_engine.hpp"

// wasm3 is a C library; every include is guarded as extern "C" (WH-0 build
// discipline, mirrored from tests/unit/runtime/wasm_host/wasm3_smoke.cpp).
extern "C" {
#include <wasm3.h>
}

#include "runtime/engine/core_wasm_resume_capacity.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ahfl/compiler/ir/core_wasm_abi_constants.hpp"

namespace ahfl::runtime::wasm_host {

namespace {

namespace eng = ::ahfl::runtime::core_wasm_resume_engine;
namespace ir = ::ahfl::ir::core;
namespace cap = ::ahfl::runtime::core_wasm_resume_capacity;

// The wasm3 runtime value stack, in bytes. Generous on purpose: the emitted
// modules bound native recursion by F1's kFnRecursionNativeStackDepthMax and
// wasm3's per-frame operand stack can reach d_m3MaxFunctionStackHeight slots;
// 1 MiB leaves ample headroom for the nested import calls WH-1 drives.
constexpr std::uint32_t kRuntimeStackSizeBytes = 1u << 20;

// The distinct trap pointer that signals "the host import callback aborted"
// (ImportAbort, or a C++ exception escaped across the C boundary). wasm3
// propagates a raw callback's return pointer as the call's trap (m3_exec.h
// forwardTrap), so a pointer that is not any wasm3 error constant is an
// unambiguous host-abort signal, compared by identity (never by string).
const char kHostAbortSentinelStorage[] = "ahfl-wasm3-host-abort";
const void *const kHostAbortSentinel = kHostAbortSentinelStorage;

constexpr std::uint8_t kWasmValueTypeI32 = 0x7f;
constexpr std::uint8_t kFuncTypeForm = 0x60;

constexpr std::uint8_t kSectionType = 1;
constexpr std::uint8_t kSectionImport = 2;
constexpr std::uint8_t kSectionMemory = 5;

constexpr std::uint8_t kImportKindFunc = 0;

constexpr std::uint8_t kLimitsFlagHasMax = 0x01;
constexpr std::uint8_t kLimitsFlagIs64 = 0x02; // memory64: not admissible

constexpr std::string_view kCapabilityModuleName = "ahfl_cap";
constexpr std::string_view kCapabilityFieldPrefix = "cap_";

// A bounds-checked cursor over the module bytes. Every read is checked, so a
// malformed or truncated module yields std::nullopt and never over-reads.
class Cursor {
  public:
    explicit Cursor(std::span<const std::uint8_t> data) noexcept : data_(data) {}

    [[nodiscard]] bool eof() const noexcept { return pos_ >= data_.size(); }

    [[nodiscard]] std::optional<std::uint8_t> read_u8() {
        if (pos_ >= data_.size()) {
            return std::nullopt;
        }
        return data_[pos_++];
    }

    [[nodiscard]] std::optional<std::uint32_t> read_uleb32() {
        std::uint64_t value = 0;
        unsigned shift = 0;
        for (int i = 0; i < 5; ++i) {
            const auto byte = read_u8();
            if (!byte) {
                return std::nullopt;
            }
            value |= static_cast<std::uint64_t>(*byte & 0x7f) << shift;
            if ((*byte & 0x80) == 0) {
                if (value > std::numeric_limits<std::uint32_t>::max()) {
                    return std::nullopt;
                }
                return static_cast<std::uint32_t>(value);
            }
            shift += 7;
        }
        return std::nullopt; // more than 5 bytes: malformed u32 LEB
    }

    [[nodiscard]] std::optional<std::span<const std::uint8_t>> take(std::size_t n) {
        if (n > data_.size() - pos_) {
            return std::nullopt;
        }
        auto slice = data_.subspan(pos_, n);
        pos_ += n;
        return slice;
    }

    [[nodiscard]] std::optional<std::string> read_name() {
        const auto len = read_uleb32();
        if (!len) {
            return std::nullopt;
        }
        const auto bytes = take(*len);
        if (!bytes) {
            return std::nullopt;
        }
        return std::string(reinterpret_cast<const char *>(bytes->data()), bytes->size());
    }

  private:
    std::span<const std::uint8_t> data_;
    std::size_t pos_{0};
};

struct ParsedFuncType {
    std::vector<std::uint8_t> params; // wasm value-type bytes
    std::vector<std::uint8_t> results;
};

struct ParsedImport {
    std::string module_name;
    std::string field_name;
    std::uint32_t type_index{0};
};

struct ParsedModule {
    std::vector<ParsedFuncType> types;
    std::vector<ParsedImport> imports;
    bool has_memory{false};
    std::uint32_t memory_min_pages{0};
    bool memory_has_max{false};
};

// Parse the sections the engine must inspect (Type, Import, Memory). Every
// other section (custom, function, global, export, code, ...) is size-skipped:
// wasm3 parses and validates them, so the engine only decodes what it must
// cross-check itself. Returns std::nullopt on any structural malformation.
[[nodiscard]] std::optional<ParsedModule>
parse_module_sections(std::span<const std::uint8_t> bytes) {
    Cursor cursor(bytes);
    const auto header = cursor.take(8);
    if (!header) {
        return std::nullopt;
    }
    // magic \0asm + version 1 (the only version wasm3 runs)
    static constexpr std::array<std::uint8_t, 8> kHeader = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
    if (!std::equal(header->begin(), header->end(), kHeader.begin(), kHeader.end())) {
        return std::nullopt;
    }

    ParsedModule module;
    while (!cursor.eof()) {
        const auto section_id = cursor.read_u8();
        if (!section_id) {
            return std::nullopt;
        }
        const auto section_size = cursor.read_uleb32();
        if (!section_size) {
            return std::nullopt;
        }
        const auto payload = cursor.take(*section_size);
        if (!payload) {
            return std::nullopt;
        }

        switch (*section_id) {
        case kSectionType: {
            Cursor section(*payload);
            const auto count = section.read_uleb32();
            if (!count) {
                return std::nullopt;
            }
            for (std::uint32_t i = 0; i < *count; ++i) {
                const auto form = section.read_u8();
                if (!form || *form != kFuncTypeForm) {
                    return std::nullopt;
                }
                ParsedFuncType type;
                const auto param_count = section.read_uleb32();
                if (!param_count) {
                    return std::nullopt;
                }
                for (std::uint32_t p = 0; p < *param_count; ++p) {
                    const auto vt = section.read_u8();
                    if (!vt) {
                        return std::nullopt;
                    }
                    type.params.push_back(*vt);
                }
                const auto result_count = section.read_uleb32();
                if (!result_count) {
                    return std::nullopt;
                }
                for (std::uint32_t r = 0; r < *result_count; ++r) {
                    const auto vt = section.read_u8();
                    if (!vt) {
                        return std::nullopt;
                    }
                    type.results.push_back(*vt);
                }
                module.types.push_back(std::move(type));
            }
            break;
        }
        case kSectionImport: {
            Cursor section(*payload);
            const auto count = section.read_uleb32();
            if (!count) {
                return std::nullopt;
            }
            for (std::uint32_t i = 0; i < *count; ++i) {
                auto module_name = section.read_name();
                auto field_name = section.read_name();
                const auto kind = section.read_u8();
                if (!module_name || !field_name || !kind) {
                    return std::nullopt;
                }
                // Only func imports are admissible: a memory/table/global
                // import would break the fixed-page / export-surface contract.
                if (*kind != kImportKindFunc) {
                    return std::nullopt;
                }
                const auto type_index = section.read_uleb32();
                if (!type_index) {
                    return std::nullopt;
                }
                module.imports.push_back(ParsedImport{
                    .module_name = std::move(*module_name),
                    .field_name = std::move(*field_name),
                    .type_index = *type_index,
                });
            }
            break;
        }
        case kSectionMemory: {
            Cursor section(*payload);
            const auto count = section.read_uleb32();
            if (!count || *count != 1) {
                return std::nullopt; // exactly one memory
            }
            const auto flags = section.read_u8();
            if (!flags || (*flags & kLimitsFlagIs64) != 0) {
                return std::nullopt; // memory64 limits are not admissible
            }
            const auto min_pages = section.read_uleb32();
            if (!min_pages) {
                return std::nullopt;
            }
            module.has_memory = true;
            module.memory_min_pages = *min_pages;
            module.memory_has_max = (*flags & kLimitsFlagHasMax) != 0;
            if (module.memory_has_max) {
                // Decode the max for cursor discipline; it is rejected below.
                if (!section.read_uleb32()) {
                    return std::nullopt;
                }
            }
            break;
        }
        default:
            // Size-skipped (the cursor already consumed the payload). wasm3
            // parses and validates these sections itself.
            break;
        }
    }
    return module;
}

// Whether a wasm3 result pointer is one of the interpreter's trap constants.
// Compared by pointer identity: wasm3 error codes are static addresses, and a
// raw callback's return is propagated as the trap by pointer (forwardTrap), so
// identity is the sound and string-free test.
[[nodiscard]] bool is_wasm_trap(M3Result result) noexcept {
    return result == m3Err_trapOutOfBoundsMemoryAccess ||
           result == m3Err_trapDivisionByZero ||
           result == m3Err_trapIntegerOverflow ||
           result == m3Err_trapIntegerConversion ||
           result == m3Err_trapIndirectCallTypeMismatch ||
           result == m3Err_trapTableIndexOutOfRange ||
           result == m3Err_trapTableElementIsNull ||
           result == m3Err_trapNullReference ||
           result == m3Err_trapNullFunctionRef ||
           result == m3Err_trapTableOutOfBounds || result == m3Err_trapExit ||
           result == m3Err_trapAbort || result == m3Err_trapUnreachable ||
           result == m3Err_trapStackOverflow;
}

// Verify an exported function's signature is exactly (i32 x arg_count) ->
// (i32 x ret_count). m3_FindFunction compiles the function eagerly, so a
// compile error (and an unresolved import) surfaces here, at fresh_instance.
[[nodiscard]] bool export_has_i32_signature(IM3Function function, std::uint32_t arg_count,
                                            std::uint32_t ret_count) noexcept {
    if (function == nullptr || m3_GetArgCount(function) != arg_count ||
        m3_GetRetCount(function) != ret_count) {
        return false;
    }
    for (std::uint32_t i = 0; i < arg_count; ++i) {
        if (m3_GetArgType(function, i) != c_m3Type_i32) {
            return false;
        }
    }
    for (std::uint32_t i = 0; i < ret_count; ++i) {
        if (m3_GetRetType(function, i) != c_m3Type_i32) {
            return false;
        }
    }
    return true;
}

} // namespace

namespace detail {

// The engine session state. Defined in the .cpp so wasm3's C types never leak
// into the public header. Members are public by design: the trampoline (a
// static member, C-ABI-compatible) and the engine methods are the only
// accessors, and both live in this translation unit.
struct Wasm3EngineImpl {
    struct ImportBinding {
        std::uint32_t ordinal{0}; // position in the ahfl_cap import table
        std::uint32_t param_count{0}; // 1 (section-9 probe / bridge) or 2 (opaque)
        // WH-3: the import functype's RESULT arity. 3 for the opaque
        // (i32,i32)->(i32,i32,i32) tuple lane; 2 for the bridge
        // (i32)->(i32,i32) lane. The trampoline writes exactly this many
        // result slots (sp[0..result_count)); on the 2-result lane the
        // reply's result_ptr carries the bridge result_base semantics.
        std::uint32_t result_count{0};
    };

    IM3Environment env{nullptr};
    IM3Runtime runtime{nullptr};
    IM3Module module{nullptr};
    // Set after a successful m3_LoadModule: the runtime then owns the module
    // (m3_FreeRuntime frees it via ForEachModule). On a load failure the
    // module is unloaded (wasm3.h: "m3_LoadModule returned a result" =>
    // unloaded) and the dtor frees it separately.
    bool module_loaded{false};
    IM3Function run2{nullptr};
    IM3Function alloc{nullptr};
    // WH-2: found lazily on the first invoke_runv (a non-P6 module does not
    // export runv and still instantiates, so fresh_instance never requires it).
    IM3Function runv{nullptr};
    // WH-4: found lazily on the first invoke_step / invoke_current_state /
    // read_transition_count (a workflow module does not export these and
    // still instantiates, so fresh_instance never requires them).
    IM3Function step{nullptr};
    IM3Function current_state{nullptr};
    IM3Global transition_count{nullptr};

    // m3_ParseModule requires the module bytes to outlive the parsed module;
    // the runtime owns the module, so they live as long as this Impl.
    std::vector<std::uint8_t> module_bytes;

    eng::ImportCallback callback;
    bool instantiated{false};
    bool run_started{false};

    // Stable once fresh_instance returns: the trampoline holds raw pointers
    // into this vector, so it must not reallocate after linking.
    std::vector<ImportBinding> import_bindings;

    // The single owner of wasm3 teardown (RAII). This is what makes the
    // outer engine's defaulted move ops safe: a moved-from engine holds a
    // null impl_ (no-op destruction, no null-deref) and a move-assigned
    // engine's old Impl is torn down here, freeing every raw wasm3 pointer
    // (no leak per move-assignment). The environment is freed last:
    // runtimes and modules reference it.
    ~Wasm3EngineImpl() {
        if (module != nullptr && !module_loaded) {
            m3_FreeModule(module);
        }
        if (runtime != nullptr) {
            m3_FreeRuntime(runtime); // frees the module when module_loaded
        }
        if (env != nullptr) {
            m3_FreeEnvironment(env);
        }
    }

    // The raw-import trampoline. A static member function has C calling
    // convention (no captures) and is passed directly as M3RawCall. It
    // recovers the session from the runtime userdata and the per-import
    // binding from the import-context userdata.
    static const void *raw_import_trampoline(IM3Runtime runtime, IM3ImportContext ctx,
                                             std::uint64_t *sp, void *mem) {
        auto *self = static_cast<Wasm3EngineImpl *>(m3_GetUserData(runtime));
        const auto *binding = static_cast<const ImportBinding *>(ctx->userdata);

        const std::uint32_t memory_size = m3_GetMemorySize(runtime);
        const auto *memory_bytes = static_cast<const std::uint8_t *>(mem);

        // wasm3 raw ABI: results at sp[0..result_count), args at
        // sp[result_count..]. The arg base is therefore the binding's result
        // arity, NOT a hardcoded 3 (the bridge lane has 2 results).
        const std::uint32_t arg_base = binding->result_count;
        std::span<const std::uint8_t> param_frame;
        if (binding->param_count == 2) {
            const auto ptr = static_cast<std::uint32_t>(sp[arg_base]);
            const auto len = static_cast<std::uint32_t>(sp[arg_base + 1]);
            // m3ApiCheckMem discipline: the module-supplied param frame must
            // lie entirely inside the page. An out-of-bounds frame fails
            // closed as a trap (the module, not the host, is at fault).
            if (ptr > memory_size || len > memory_size - ptr) {
                return m3Err_trapOutOfBoundsMemoryAccess;
            }
            param_frame = std::span<const std::uint8_t>(memory_bytes + ptr, len);
        }
        // param_count == 1: the single arg is an opaque scalar (the section-9
        // probe shape, or the bridge control-block pointer); the param frame
        // is empty.

        eng::ImportObservation observation;
        observation.import_ordinal = binding->ordinal;
        // The single i32 arg of a 1-param import (bridge control-block pointer
        // / section-9 scalar); for a 2-param import this is the frame pointer
        // (also the start of param_frame) and is not authoritative.
        observation.scalar_arg = static_cast<std::uint32_t>(sp[arg_base]);
        observation.param_frame = param_frame;
        observation.whole_memory =
            std::span<const std::uint8_t>(memory_bytes, memory_size);

        eng::ImportCallbackResult reply;
        try {
            reply = self->callback(observation);
        } catch (...) {
            // A C++ exception must not cross the C wasm3 boundary: fail closed
            // as a host abort (the host, not the module, failed).
            return kHostAbortSentinel;
        }

        if (std::holds_alternative<eng::ImportAbort>(reply)) {
            return kHostAbortSentinel;
        }

        const auto &frame = std::get<eng::ImportReply>(reply);
        // Write exactly the import functype's result slots. The raw status is
        // carried verbatim (the engine never classifies); the opaque lane's
        // third slot is the result frame length, the bridge lane's second slot
        // is the result_base (no length word on a 2-result functype).
        sp[0] = frame.raw_status;
        sp[1] = frame.result_ptr.value;
        if (binding->result_count == 3) {
            sp[2] = frame.result_len;
        }
        return m3Err_none;
    }
};

} // namespace detail

Wasm3ResumeEngine::Wasm3ResumeEngine() noexcept
    : impl_(std::make_unique<detail::Wasm3EngineImpl>()) {}

Wasm3ResumeEngine::~Wasm3ResumeEngine() = default;

Wasm3ResumeEngine::Wasm3ResumeEngine(Wasm3ResumeEngine &&) noexcept = default;
Wasm3ResumeEngine &Wasm3ResumeEngine::operator=(Wasm3ResumeEngine &&) noexcept = default;

std::expected<void, eng::EngineError>
Wasm3ResumeEngine::fresh_instance(std::span<const std::uint8_t> module_bytes,
                                  eng::ImportCallback import_callback) {
    if (impl_->instantiated) {
        return std::unexpected(eng::EngineError::InvalidSequence);
    }
    // A previous failed attempt may have left wasm3 resources in this Impl.
    // Replace it wholesale so every attempt starts from a coherent null
    // state and a retry never leaks the remnants of the first (the Impl
    // dtor is the single owner of teardown).
    impl_ = std::make_unique<detail::Wasm3EngineImpl>();

    // 1. Structural admission: parse the Type/Import/Memory sections so we can
    //    (a) reject non-ahfl_cap imports and unsupported functypes, and (b)
    //    cross-check the fixed single-page memory against the F1 SSOT BEFORE
    //    any wasm3 memory access -- m3_GetMemory dereferences the memory
    //    unconditionally and would crash on a memory-less module.
    const auto parsed = parse_module_sections(module_bytes);
    if (!parsed) {
        return std::unexpected(eng::EngineError::InstanceUnavailable);
    }

    // Memory cross-check (the F3-equivalent fixed-page gate). The A2 admission
    // layer runs this for capability modules in the production driver; the
    // engine applies it directly for import-free modules, which A2 cannot
    // admit (it requires a capability Import section).
    if (!parsed->has_memory || parsed->memory_has_max) {
        return std::unexpected(eng::EngineError::InstanceUnavailable);
    }
    const auto capacity = cap::fixed_single_page_capacity();
    const std::uint64_t declared_bytes =
        std::uint64_t{parsed->memory_min_pages} * ir::kCoreWasmLinearMemoryPageSizeBytes;
    if (parsed->memory_min_pages != ir::kCoreWasmFixedLinearMemoryMinPages ||
        declared_bytes != capacity.value) {
        return std::unexpected(eng::EngineError::InstanceUnavailable);
    }

    // Import classification + functype validation. Every func import must be
    // ahfl_cap.cap_<decimal> with one of the two sealed lane functypes:
    //   * opaque: (i32,i32)->(i32,i32,i32) or (i32)->(i32,i32,i32) (3 results);
    //   * bridge: (i32)->(i32,i32) (2 results; WH-3).
    std::vector<detail::Wasm3EngineImpl::ImportBinding> bindings;
    bindings.reserve(parsed->imports.size());
    for (const auto &import : parsed->imports) {
        if (import.module_name != kCapabilityModuleName) {
            return std::unexpected(eng::EngineError::InstanceUnavailable);
        }
        if (import.field_name.size() <= kCapabilityFieldPrefix.size() ||
            import.field_name.compare(0, kCapabilityFieldPrefix.size(),
                                      kCapabilityFieldPrefix) != 0) {
            return std::unexpected(eng::EngineError::InstanceUnavailable);
        }
        const bool decimal =
            std::all_of(import.field_name.begin() + kCapabilityFieldPrefix.size(),
                        import.field_name.end(),
                        [](char c) { return c >= '0' && c <= '9'; });
        if (!decimal) {
            return std::unexpected(eng::EngineError::InstanceUnavailable);
        }
        if (import.type_index >= parsed->types.size()) {
            return std::unexpected(eng::EngineError::InstanceUnavailable);
        }
        const auto &type = parsed->types[import.type_index];
        const auto is_i32 = [](std::uint8_t vt) { return vt == kWasmValueTypeI32; };
        const bool results_i32 =
            std::all_of(type.results.begin(), type.results.end(), is_i32);
        const bool params_i32 =
            std::all_of(type.params.begin(), type.params.end(), is_i32);
        if (!results_i32 || !params_i32) {
            return std::unexpected(eng::EngineError::InstanceUnavailable);
        }
        const bool opaque_lane = type.results.size() == 3 &&
                                 !type.params.empty() && type.params.size() <= 2;
        const bool bridge_lane = type.results.size() == 2 && type.params.size() == 1;
        if (!opaque_lane && !bridge_lane) {
            return std::unexpected(eng::EngineError::InstanceUnavailable);
        }
        bindings.push_back(detail::Wasm3EngineImpl::ImportBinding{
            .ordinal = static_cast<std::uint32_t>(bindings.size()),
            .param_count = static_cast<std::uint32_t>(type.params.size()),
            .result_count = static_cast<std::uint32_t>(type.results.size()),
        });
    }

    // 2. wasm3 environment + parse. Resources commit to the Impl as they are
    //    acquired: every error path below just returns, and the Impl dtor
    //    tears down whatever was acquired (no manual frees, no leak on a
    //    failed attempt). The module bytes must outlive the module, so copy
    //    them into the Impl first and parse from there.
    impl_->module_bytes.assign(module_bytes.begin(), module_bytes.end());

    impl_->env = m3_NewEnvironment();
    if (impl_->env == nullptr) {
        return std::unexpected(eng::EngineError::InstanceUnavailable);
    }

    if (m3_ParseModule(impl_->env, &impl_->module, impl_->module_bytes.data(),
                       static_cast<std::uint32_t>(impl_->module_bytes.size())) !=
        nullptr) {
        return std::unexpected(eng::EngineError::InstanceUnavailable);
    }

    // 3. Runtime with this session as userdata (the trampoline recovers it).
    impl_->runtime = m3_NewRuntime(impl_->env, kRuntimeStackSizeBytes, impl_.get());
    if (impl_->runtime == nullptr) {
        return std::unexpected(eng::EngineError::InstanceUnavailable);
    }

    // 4. Load (transfers module ownership to the runtime). wasm3 resolves raw
    //    imports against the LOADED module, so linking follows the load (the
    //    WH-0 smoke test establishes this order); m3_LoadModule itself does
    //    not require imports to be linked. On FAILURE the module is unloaded
    //    (wasm3.h: "m3_LoadModule returned a result" => unloaded); the dtor
    //    frees BOTH the module and the runtime. On SUCCESS the runtime owns
    //    the module, so the dtor frees only the runtime.
    if (m3_LoadModule(impl_->runtime, impl_->module) != nullptr) {
        return std::unexpected(eng::EngineError::InstanceUnavailable);
    }
    impl_->module_loaded = true;

    // 5. Link every capability import with its raw trampoline. `bindings` is
    //    fully built, so its element addresses are stable for the session
    //    (committed to the Impl below before any invocation). The wasm3 raw
    //    signature is results-then-(params): the opaque 3-result lane is
    //    "iii(ii)"/"iii(i)", the bridge 2-result lane is "ii(i)".
    for (std::size_t i = 0; i < parsed->imports.size(); ++i) {
        const char *signature = nullptr;
        if (bindings[i].result_count == 3) {
            signature = bindings[i].param_count == 2 ? "iii(ii)" : "iii(i)";
        } else {
            signature = "ii(i)";
        }
        if (m3_LinkRawFunctionEx(impl_->module, "ahfl_cap",
                                 parsed->imports[i].field_name.c_str(), signature,
                                 &detail::Wasm3EngineImpl::raw_import_trampoline,
                                 &bindings[i]) != nullptr) {
            return std::unexpected(eng::EngineError::InstanceUnavailable);
        }
    }

    // 6. Post-load memory check: the live page must be exactly the fixed
    //    capacity (wasm3 allocates min pages at load).
    std::uint32_t memory_size = 0;
    if (m3_GetMemory(impl_->runtime, &memory_size, 0) == nullptr ||
        memory_size != capacity.value) {
        return std::unexpected(eng::EngineError::InstanceUnavailable);
    }

    // 7. Export surface: run2 (i32,i32)->(i32,i32,i32) and alloc (i32)->i32.
    //    m3_FindFunction compiles eagerly, so an unresolved import or a body
    //    that does not compile fails here, at fresh_instance.
    if (m3_FindFunction(&impl_->run2, impl_->runtime, "run2") != nullptr ||
        !export_has_i32_signature(impl_->run2, 2, 3) ||
        m3_FindFunction(&impl_->alloc, impl_->runtime, "alloc") != nullptr ||
        !export_has_i32_signature(impl_->alloc, 1, 1)) {
        return std::unexpected(eng::EngineError::InstanceUnavailable);
    }

    // 8. Commit: the session is live.
    impl_->callback = std::move(import_callback);
    impl_->import_bindings = std::move(bindings);
    impl_->instantiated = true;
    return {};
}

std::expected<std::span<const std::uint8_t>, eng::EngineError>
Wasm3ResumeEngine::read_whole_memory() {
    if (!impl_->instantiated) {
        return std::unexpected(eng::EngineError::InvalidSequence);
    }
    std::uint32_t size = 0;
    std::uint8_t *memory = m3_GetMemory(impl_->runtime, &size, 0);
    if (memory == nullptr || size != cap::fixed_single_page_capacity().value) {
        return std::unexpected(eng::EngineError::InstanceUnavailable);
    }
    return std::span<const std::uint8_t>(memory, size);
}

std::expected<eng::GuestPointer, eng::EngineError>
Wasm3ResumeEngine::alloc_then_write(std::span<const std::uint8_t> bytes) {
    if (!impl_->instantiated) {
        return std::unexpected(eng::EngineError::InvalidSequence);
    }

    // A frame larger than the whole page can never fit; reject without
    // touching the module (no partial mutation).
    if (bytes.size() > cap::fixed_single_page_capacity().value) {
        return std::unexpected(eng::EngineError::MemoryCapacityExceeded);
    }

    // Drive the module's own exported checked allocator (the same lane the
    // Node embedded engine uses): alloc(len) -> ptr, returning 0 on OOM.
    const auto len = static_cast<std::uint32_t>(bytes.size());
    const void *argptrs[1] = {&len};
    if (m3_Call(impl_->alloc, 1, argptrs) != nullptr) {
        return std::unexpected(eng::EngineError::InstanceUnavailable);
    }
    std::uint32_t ptr = 0;
    const void *retptrs[1] = {&ptr};
    if (m3_GetResults(impl_->alloc, 1, retptrs) != nullptr) {
        return std::unexpected(eng::EngineError::InstanceUnavailable);
    }
    if (ptr == 0) {
        // The module's checked allocator refused: the frame does not fit.
        return std::unexpected(eng::EngineError::MemoryCapacityExceeded);
    }

    // m3ApiCheckMem discipline: bounds-check the pointer the module handed
    // back before writing through it.
    std::uint32_t memory_size = 0;
    std::uint8_t *memory = m3_GetMemory(impl_->runtime, &memory_size, 0);
    if (memory == nullptr || ptr > memory_size || len > memory_size - ptr) {
        return std::unexpected(eng::EngineError::InstanceUnavailable);
    }
    if (len > 0) {
        std::memcpy(memory + ptr, bytes.data(), len);
    }
    return eng::GuestPointer{ptr};
}

std::expected<eng::Run2Outcome, eng::EngineError>
Wasm3ResumeEngine::invoke_run2(eng::GuestPointer entry_ptr, std::uint32_t entry_len) {
    if (!impl_->instantiated || impl_->run_started) {
        return std::unexpected(eng::EngineError::InvalidSequence);
    }
    impl_->run_started = true;

    // The L0 entry frame must be a valid range inside the page (the Fake's
    // exact discipline: a null or past-end entry is InvalidSequence).
    std::uint32_t memory_size = 0;
    if (m3_GetMemory(impl_->runtime, &memory_size, 0) == nullptr) {
        return std::unexpected(eng::EngineError::InstanceUnavailable);
    }
    if (entry_ptr.value == 0 ||
        static_cast<std::uint64_t>(entry_ptr.value) + entry_len > memory_size) {
        return std::unexpected(eng::EngineError::InvalidSequence);
    }

    const std::uint32_t args[2] = {entry_ptr.value, entry_len};
    const void *argptrs[2] = {&args[0], &args[1]};
    const M3Result result = m3_Call(impl_->run2, 2, argptrs);

    if (result == nullptr) {
        // Fixed-page invariant (decision doc section 5): the module declared
        // exactly one 64 KiB page with no max, so a structurally admissible
        // module can still execute memory.grow mid-invoke -- wasm3 v0.9.0
        // reallocates the linear memory (ResizeMemory, m3_env.c), breaking
        // the "exactly one fixed page" guarantee AFTER admission. The engine
        // contract holds the invariant for the whole session, so a grown
        // run2 fails closed as MemoryCapacityExceeded: the guest's grow is a
        // write beyond the fixed page. Not Run2Trapped (no wasm trap
        // occurred); not a byte scan for 0x40 (it legitimately appears as a
        // LEB byte, e.g. the terminator of i32.const 64).
        std::uint32_t post_size = 0;
        if (m3_GetMemory(impl_->runtime, &post_size, 0) == nullptr) {
            return std::unexpected(eng::EngineError::InstanceUnavailable);
        }
        if (post_size != cap::fixed_single_page_capacity().value) {
            return std::unexpected(eng::EngineError::MemoryCapacityExceeded);
        }

        std::uint32_t status = 0;
        std::uint32_t ptr = 0;
        std::uint32_t len = 0;
        const void *retptrs[3] = {&status, &ptr, &len};
        if (m3_GetResults(impl_->run2, 3, retptrs) != nullptr) {
            return std::unexpected(eng::EngineError::InstanceUnavailable);
        }
        eng::Run2ResultTuple tuple;
        tuple.raw_status = status; // pass through verbatim; no classification
        tuple.output_ptr = eng::GuestPointer{ptr};
        tuple.output_len = len;
        return eng::Run2Outcome{tuple};
    }
    if (result == kHostAbortSentinel) {
        return eng::Run2Outcome{eng::Run2HostAborted{}};
    }
    if (is_wasm_trap(result)) {
        return eng::Run2Outcome{eng::Run2Trapped{}};
    }
    return std::unexpected(eng::EngineError::InstanceUnavailable);
}

std::expected<std::span<std::uint8_t>, eng::EngineError>
Wasm3ResumeEngine::mutable_whole_memory() {
    if (!impl_->instantiated) {
        return std::unexpected(eng::EngineError::InvalidSequence);
    }
    std::uint32_t size = 0;
    std::uint8_t *memory = m3_GetMemory(impl_->runtime, &size, 0);
    if (memory == nullptr || size != cap::fixed_single_page_capacity().value) {
        return std::unexpected(eng::EngineError::InstanceUnavailable);
    }
    return std::span<std::uint8_t>(memory, size);
}

std::expected<RunvOutcome, eng::EngineError>
Wasm3ResumeEngine::invoke_runv() {
    if (!impl_->instantiated || impl_->run_started) {
        return std::unexpected(eng::EngineError::InvalidSequence);
    }
    impl_->run_started = true;

    // Lazily find + signature-check runv. m3_FindFunction compiles eagerly, so
    // a body that does not compile (or an unresolved import) surfaces here.
    if (impl_->runv == nullptr) {
        if (m3_FindFunction(&impl_->runv, impl_->runtime, "runv") != nullptr ||
            !export_has_i32_signature(impl_->runv, 0, 2)) {
            return std::unexpected(eng::EngineError::InstanceUnavailable);
        }
    }

    const M3Result result = m3_Call(impl_->runv, 0, nullptr);
    if (result == nullptr) {
        // Fixed-page invariant (same discipline as invoke_run2): a structurally
        // admissible module can still execute memory.grow mid-runv, breaking the
        // "exactly one fixed page" guarantee after admission.
        std::uint32_t post_size = 0;
        if (m3_GetMemory(impl_->runtime, &post_size, 0) == nullptr) {
            return std::unexpected(eng::EngineError::InstanceUnavailable);
        }
        if (post_size != cap::fixed_single_page_capacity().value) {
            return std::unexpected(eng::EngineError::MemoryCapacityExceeded);
        }

        std::uint32_t status = 0;
        std::uint32_t ptr = 0;
        const void *retptrs[2] = {&status, &ptr};
        if (m3_GetResults(impl_->runv, 2, retptrs) != nullptr) {
            return std::unexpected(eng::EngineError::InstanceUnavailable);
        }
        return RunvOutcome{RunvResult{status, eng::GuestPointer{ptr}}};
    }
    if (result == kHostAbortSentinel) {
        // runv on an import-free P6-frame module cannot reach a host import,
        // but a bridge module (WH-3) can; map the sentinel faithfully as a
        // host abort (distinct from a Wasm trap, mirroring Run2Outcome).
        return RunvOutcome{eng::Run2HostAborted{}};
    }
    if (is_wasm_trap(result)) {
        return RunvOutcome{eng::Run2Trapped{}};
    }
    return std::unexpected(eng::EngineError::InstanceUnavailable);
}

std::expected<std::uint32_t, eng::EngineError>
Wasm3ResumeEngine::invoke_step() {
    if (!impl_->instantiated) {
        return std::unexpected(eng::EngineError::InvalidSequence);
    }
    // Lazily find + signature-check step. m3_FindFunction compiles eagerly.
    if (impl_->step == nullptr) {
        if (m3_FindFunction(&impl_->step, impl_->runtime, "step") != nullptr ||
            !export_has_i32_signature(impl_->step, 0, 1)) {
            return std::unexpected(eng::EngineError::InstanceUnavailable);
        }
    }
    const M3Result result = m3_Call(impl_->step, 0, nullptr);
    if (result != nullptr) {
        if (is_wasm_trap(result)) {
            return std::unexpected(eng::EngineError::InstanceUnavailable);
        }
        return std::unexpected(eng::EngineError::InstanceUnavailable);
    }
    std::uint32_t state = 0;
    const void *retptrs[1] = {&state};
    if (m3_GetResults(impl_->step, 1, retptrs) != nullptr) {
        return std::unexpected(eng::EngineError::InstanceUnavailable);
    }
    return state;
}

std::expected<std::uint32_t, eng::EngineError>
Wasm3ResumeEngine::invoke_current_state() {
    if (!impl_->instantiated) {
        return std::unexpected(eng::EngineError::InvalidSequence);
    }
    if (impl_->current_state == nullptr) {
        if (m3_FindFunction(&impl_->current_state, impl_->runtime,
                            "current_state") != nullptr ||
            !export_has_i32_signature(impl_->current_state, 0, 1)) {
            return std::unexpected(eng::EngineError::InstanceUnavailable);
        }
    }
    const M3Result result = m3_Call(impl_->current_state, 0, nullptr);
    if (result != nullptr) {
        return std::unexpected(eng::EngineError::InstanceUnavailable);
    }
    std::uint32_t state = 0;
    const void *retptrs[1] = {&state};
    if (m3_GetResults(impl_->current_state, 1, retptrs) != nullptr) {
        return std::unexpected(eng::EngineError::InstanceUnavailable);
    }
    return state;
}

std::expected<std::uint32_t, eng::EngineError>
Wasm3ResumeEngine::read_transition_count() {
    if (!impl_->instantiated) {
        return std::unexpected(eng::EngineError::InvalidSequence);
    }
    if (impl_->transition_count == nullptr) {
        impl_->transition_count =
            m3_FindGlobal(impl_->module, "transition_count");
        if (impl_->transition_count == nullptr) {
            return std::unexpected(eng::EngineError::InstanceUnavailable);
        }
    }
    M3TaggedValue value{};
    if (m3_GetGlobal(impl_->transition_count, &value) != nullptr) {
        return std::unexpected(eng::EngineError::InstanceUnavailable);
    }
    if (value.type != c_m3Type_i32) {
        return std::unexpected(eng::EngineError::InstanceUnavailable);
    }
    return value.value.i32;
}

} // namespace ahfl::runtime::wasm_host
