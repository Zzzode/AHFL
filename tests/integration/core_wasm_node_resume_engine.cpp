// RFC 0026 KR6.5 E4-B2-D2a (F5): see the matching header for the protocol.

#include "core_wasm_node_resume_engine.hpp"

#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

extern char **environ;

namespace ahfl::runtime::core_wasm_node_resume_engine {

namespace {

namespace eng = ahfl::runtime::core_wasm_resume_engine;

// Fixed 64 KiB Core-Wasm page (the F1 SSOT mirrored for the test adapter).
constexpr std::size_t kPageBytes = 65536;
constexpr int kFrameHeaderBytes = 5;
constexpr int kReadTimeoutSeconds = 30;

// Child->parent frame kinds (also documented in the header).
constexpr std::uint8_t kFrameReady = 0;
constexpr std::uint8_t kFrameImport = 10;
constexpr std::uint8_t kFrameInstantiated = 16;
constexpr std::uint8_t kFrameAllocResult = 17;
constexpr std::uint8_t kFrameWholeMemory = 18;
constexpr std::uint8_t kFrameRun2Outcome = 19;
constexpr std::uint8_t kFrameFatal = 99;

// Parent->child command kinds.
constexpr std::uint8_t kCmdInstantiate = 1;
constexpr std::uint8_t kCmdAllocWrite = 2;
constexpr std::uint8_t kCmdInvokeRun2 = 3;
constexpr std::uint8_t kCmdReadMemory = 4;
constexpr std::uint8_t kCmdBye = 5;
constexpr std::uint8_t kCmdImportReply = 6;
constexpr std::uint8_t kCmdImportAbort = 7;

// RAII pipe fd.
class Fd {
  public:
    Fd() = default;
    explicit Fd(int fd) : fd_(fd) {}
    ~Fd() {
        reset();
    }
    Fd(const Fd &) = delete;
    Fd &operator=(const Fd &) = delete;
    Fd(Fd &&other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }
    Fd &operator=(Fd &&other) noexcept {
        if (this != &other) {
            reset();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept {
        return fd_;
    }
    [[nodiscard]] bool valid() const noexcept {
        return fd_ >= 0;
    }
    int release() {
        const int v = fd_;
        fd_ = -1;
        return v;
    }
    void reset(int fd = -1) {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = fd;
    }

  private:
    int fd_{-1};
};

// Exact write of a command-channel frame. The command channel is a SOCK_STREAM
// socketpair and MSG_NOSIGNAL suppresses SIGPIPE, so a peer that has closed its
// read end (crashed, killed, or drained past its deadline) yields EPIPE -> false
// and the caller maps it to a typed InstanceUnavailable. A default-disposition
// SIGPIPE would instead terminate the whole host process before the EPIPE check
// could run, which is the exact fail-open the port contract forbids.
[[nodiscard]] bool send_all(int fd, const std::uint8_t *data, std::size_t size) {
    while (size > 0) {
        const auto n = ::send(fd, data, size, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (n == 0) {
            return false;
        }
        data += n;
        size -= static_cast<std::size_t>(n);
    }
    return true;
}

// Exact blocking read with a deadline; distinguishes EOF/timeout from errors.
enum class ReadState {
    Ok,
    Eof,
    Timeout,
    Error
};

[[nodiscard]] ReadState read_exact(int fd,
                                   std::uint8_t *data,
                                   std::size_t size,
                                   std::chrono::steady_clock::time_point deadline) {
    while (size > 0) {
        pollfd pfd{fd, POLLIN, 0};
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return ReadState::Timeout;
        }
        const auto remaining_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        const int pr = ::poll(&pfd, 1, static_cast<int>(remaining_ms));
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            return ReadState::Error;
        }
        if (pr == 0) {
            return ReadState::Timeout;
        }
        const auto n = ::read(fd, data, size);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return ReadState::Error;
        }
        if (n == 0) {
            return ReadState::Eof;
        }
        data += n;
        size -= static_cast<std::size_t>(n);
    }
    return ReadState::Ok;
}

// The launch module file's bytes, read once so fresh_instance can hold the port
// to its contract ("instantiate ... the digest-admitted module bytes"): the JS
// child instantiates the file at process.argv[2], so the launch file -- not the
// span handed to fresh_instance -- is the authority. Binding the two is what
// keeps a future reuse from silently executing un-admitted bytes.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> read_module_file(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
}

// One steady_clock bound shared by every read of a session exchange. It is
// computed once per session entry (launch, or an engine call) and threaded
// through every recv()/read_exact() -- including all nested import exchanges of
// a single invoke_run2 -- so the bound covers the whole session, not each
// individual read. A child that trickles one well-formed frame just under the
// per-read window can therefore not extend the session unboundedly.
[[nodiscard]] std::chrono::steady_clock::time_point read_bound() {
    return std::chrono::steady_clock::now() + std::chrono::seconds(kReadTimeoutSeconds);
}

} // namespace

struct NodeResumeEngine::Impl {
    pid_t pid{-1};
    Fd cmd_write;  // parent -> child (child fd 0)
    Fd frame_read; // child -> parent (child fd 1)
    Fd stderr_read;
    std::thread stderr_drain;
    std::mutex stderr_mutex;
    std::string stderr_text;
    bool instantiated{false};

    // Scratch storage backing the spans the port hands out: the most recent
    // whole-page observation (re-filled by read_whole_memory / an import).
    std::vector<std::uint8_t> page;

    // The exact bytes of the module file the child compiled at launch. The JS
    // peer instantiates process.argv[2], so this -- not the span passed to
    // fresh_instance -- is what actually executes; fresh_instance refuses a
    // mismatch rather than let the adapter run un-admitted bytes.
    std::vector<std::uint8_t> launch_module_bytes;

    eng::ImportCallback callback;

    ~Impl() {
        teardown();
    }

    void drain_stderr() {
        std::array<char, 4096> chunk{};
        while (true) {
            const auto n = ::read(stderr_read.get(), chunk.data(), chunk.size());
            if (n > 0) {
                std::lock_guard lock(stderr_mutex);
                stderr_text.append(chunk.data(), static_cast<std::size_t>(n));
                continue;
            }
            if (n < 0 && errno == EINTR) {
                continue;
            }
            break;
        }
    }

    [[nodiscard]] std::string diagnostic() {
        std::lock_guard lock(stderr_mutex);
        return stderr_text;
    }

    [[nodiscard]] bool send(std::uint8_t kind, std::span<const std::uint8_t> payload = {}) const {
        std::uint8_t header[kFrameHeaderBytes];
        header[0] = kind;
        const auto n = static_cast<std::uint32_t>(payload.size());
        header[1] = static_cast<std::uint8_t>(n & 0xFFu);
        header[2] = static_cast<std::uint8_t>((n >> 8) & 0xFFu);
        header[3] = static_cast<std::uint8_t>((n >> 16) & 0xFFu);
        header[4] = static_cast<std::uint8_t>((n >> 24) & 0xFFu);
        return send_all(cmd_write.get(), header, sizeof(header)) &&
               send_all(cmd_write.get(), payload.data(), payload.size());
    }

    // One inbound frame: kind + owned payload. Eof/timeout/error all surface as
    // nullopt; `eof` distinguishes a clean child exit from a protocol fault.
    struct InFrame {
        std::uint8_t kind{0};
        std::vector<std::uint8_t> payload;
    };
    [[nodiscard]] std::optional<InFrame> recv(bool &eof, std::chrono::steady_clock::time_point bound) {
        eof = false;
        std::uint8_t header[kFrameHeaderBytes]{};
        const auto hs = read_exact(frame_read.get(), header, sizeof(header), bound);
        if (hs != ReadState::Ok) {
            eof = (hs == ReadState::Eof);
            return std::nullopt;
        }
        const std::uint64_t n = static_cast<std::uint64_t>(header[1]) |
                                (static_cast<std::uint64_t>(header[2]) << 8) |
                                (static_cast<std::uint64_t>(header[3]) << 16) |
                                (static_cast<std::uint64_t>(header[4]) << 24);
        // Every legitimate frame is small or exactly one import observation
        // (12-byte header + one page); reject larger lengths before allocating.
        constexpr std::uint64_t kMaxFrameBytes = 12 + kPageBytes;
        if (n > kMaxFrameBytes) {
            return std::nullopt;
        }
        InFrame frame;
        frame.kind = header[0];
        frame.payload.resize(static_cast<std::size_t>(n));
        if (n > 0) {
            const auto ps = read_exact(
                frame_read.get(), frame.payload.data(), frame.payload.size(), bound);
            if (ps != ReadState::Ok) {
                eof = (ps == ReadState::Eof);
                return std::nullopt;
            }
        }
        return frame;
    }

    void teardown() {
        // Best-effort clean shutdown; never throw from the destructor. Sending
        // kCmdBye then closing the command channel gives the child's blocking
        // readSync EOF; the kill covers a child parked inside the Wasm import.
        // The command channel is a socketpair written with MSG_NOSIGNAL, so this
        // send is EPIPE-safe when the child is already dead (the pre-fix
        // default-disposition SIGPIPE killed the host here). The stderr drain
        // thread is joined BEFORE its read fd is closed (child death ends it).
        if (instantiated || pid > 0) {
            static_cast<void>(send(kCmdBye));
        }
        cmd_write.reset();
        frame_read.reset();
        if (pid > 0) {
            ::kill(pid, SIGKILL);
            int status = 0;
            for (int i = 0; i < 100; ++i) {
                if (::waitpid(pid, &status, WNOHANG) == pid) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            ::waitpid(pid, &status, 0);
            pid = -1;
        }
        if (stderr_drain.joinable()) {
            stderr_drain.join();
        }
        stderr_read.reset();
    }
};

NodeResumeEngine::NodeResumeEngine(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

NodeResumeEngine::~NodeResumeEngine() = default;
NodeResumeEngine::NodeResumeEngine(NodeResumeEngine &&) noexcept = default;
NodeResumeEngine &NodeResumeEngine::operator=(NodeResumeEngine &&) noexcept = default;

int NodeResumeEngine::child_pid_for_test() const noexcept {
    return impl_ == nullptr ? -1 : static_cast<int>(impl_->pid);
}

std::expected<void, eng::EngineError>
NodeResumeEngine::fresh_instance(std::span<const std::uint8_t> module_bytes,
                                 eng::ImportCallback import_callback) {
    if (impl_ == nullptr || impl_->instantiated || module_bytes.empty()) {
        return std::unexpected(eng::EngineError::InvalidSequence);
    }
    // The child compiles the launch FILE, so fresh_instance is only sound when
    // the span it is handed IS that file's content: the caller's A2 admission
    // covers the bytes it passed, and any divergence would execute un-admitted
    // bytes. Refuse rather than trust.
    if (module_bytes.size() != impl_->launch_module_bytes.size() ||
        !std::equal(module_bytes.begin(), module_bytes.end(), impl_->launch_module_bytes.begin())) {
        return std::unexpected(eng::EngineError::InvalidSequence);
    }
    impl_->callback = std::move(import_callback);
    if (!impl_->send(kCmdInstantiate)) {
        return std::unexpected(eng::EngineError::InstanceUnavailable);
    }
    bool eof = false;
    auto frame = impl_->recv(eof, read_bound());
    if (!frame.has_value() || frame->kind != kFrameInstantiated) {
        return std::unexpected(eng::EngineError::InstanceUnavailable);
    }
    impl_->instantiated = true;
    return {};
}

std::expected<eng::GuestPointer, eng::EngineError>
NodeResumeEngine::alloc_then_write(std::span<const std::uint8_t> bytes) {
    if (impl_ == nullptr || !impl_->instantiated) {
        return std::unexpected(eng::EngineError::InvalidSequence);
    }
    if (bytes.size() > kPageBytes) {
        return std::unexpected(eng::EngineError::MemoryCapacityExceeded);
    }
    std::vector<std::uint8_t> payload(4 + bytes.size());
    const auto n = static_cast<std::uint32_t>(bytes.size());
    payload[0] = static_cast<std::uint8_t>(n & 0xFFu);
    payload[1] = static_cast<std::uint8_t>((n >> 8) & 0xFFu);
    payload[2] = static_cast<std::uint8_t>((n >> 16) & 0xFFu);
    payload[3] = static_cast<std::uint8_t>((n >> 24) & 0xFFu);
    std::copy(bytes.begin(), bytes.end(), payload.begin() + 4);
    if (!impl_->send(kCmdAllocWrite, payload)) {
        return std::unexpected(eng::EngineError::InstanceUnavailable);
    }
    bool eof = false;
    auto frame = impl_->recv(eof, read_bound());
    if (!frame.has_value() || frame->kind != kFrameAllocResult || frame->payload.size() != 4) {
        return std::unexpected(eng::EngineError::InstanceUnavailable);
    }
    const std::uint32_t ptr = static_cast<std::uint32_t>(frame->payload[0]) |
                              (static_cast<std::uint32_t>(frame->payload[1]) << 8) |
                              (static_cast<std::uint32_t>(frame->payload[2]) << 16) |
                              (static_cast<std::uint32_t>(frame->payload[3]) << 24);
    // The module's checked alloc returns 0 without advancing on overflow.
    if (ptr == 0) {
        return std::unexpected(eng::EngineError::MemoryCapacityExceeded);
    }
    return eng::GuestPointer{ptr};
}

std::expected<std::span<const std::uint8_t>, eng::EngineError>
NodeResumeEngine::read_whole_memory() {
    if (impl_ == nullptr || !impl_->instantiated) {
        return std::unexpected(eng::EngineError::InvalidSequence);
    }
    if (!impl_->send(kCmdReadMemory)) {
        return std::unexpected(eng::EngineError::InstanceUnavailable);
    }
    bool eof = false;
    auto frame = impl_->recv(eof, read_bound());
    if (!frame.has_value() || frame->kind != kFrameWholeMemory ||
        frame->payload.size() != kPageBytes) {
        return std::unexpected(eng::EngineError::InstanceUnavailable);
    }
    impl_->page = std::move(frame->payload);
    return std::span<const std::uint8_t>(impl_->page);
}

std::expected<eng::Run2Outcome, eng::EngineError>
NodeResumeEngine::invoke_run2(eng::GuestPointer entry_ptr, std::uint32_t entry_len) {
    if (impl_ == nullptr || !impl_->instantiated) {
        return std::unexpected(eng::EngineError::InvalidSequence);
    }
    std::uint8_t request[8]{};
    request[0] = static_cast<std::uint8_t>(entry_ptr.value & 0xFFu);
    request[1] = static_cast<std::uint8_t>((entry_ptr.value >> 8) & 0xFFu);
    request[2] = static_cast<std::uint8_t>((entry_ptr.value >> 16) & 0xFFu);
    request[3] = static_cast<std::uint8_t>((entry_ptr.value >> 24) & 0xFFu);
    request[4] = static_cast<std::uint8_t>(entry_len & 0xFFu);
    request[5] = static_cast<std::uint8_t>((entry_len >> 8) & 0xFFu);
    request[6] = static_cast<std::uint8_t>((entry_len >> 16) & 0xFFu);
    request[7] = static_cast<std::uint8_t>((entry_len >> 24) & 0xFFu);
    if (!impl_->send(kCmdInvokeRun2, request)) {
        return std::unexpected(eng::EngineError::InstanceUnavailable);
    }

    // Drive run2 until its terminal outcome, serving every nested ahfl_cap
    // import synchronously on this same stack. ONE bound covers the whole
    // run2 exchange (including every nested import) so an open-ended import
    // loop cannot refresh the deadline frame by frame.
    const auto bound = read_bound();
    while (true) {
        bool eof = false;
        auto frame = impl_->recv(eof, bound);
        if (!frame.has_value()) {
            return std::unexpected(eng::EngineError::InstanceUnavailable);
        }
        if (frame->kind == kFrameRun2Outcome) {
            const auto &p = frame->payload;
            if (p.empty()) {
                return std::unexpected(eng::EngineError::InstanceUnavailable);
            }
            if (p[0] == 0) {
                if (p.size() != 13) {
                    return std::unexpected(eng::EngineError::InstanceUnavailable);
                }
                const auto u32at = [&](std::size_t off) {
                    return static_cast<std::uint32_t>(p[off]) |
                           (static_cast<std::uint32_t>(p[off + 1]) << 8) |
                           (static_cast<std::uint32_t>(p[off + 2]) << 16) |
                           (static_cast<std::uint32_t>(p[off + 3]) << 24);
                };
                eng::Run2ResultTuple tuple;
                tuple.raw_status = u32at(1);
                tuple.output_ptr = eng::GuestPointer{u32at(5)};
                tuple.output_len = u32at(9);
                return eng::Run2Outcome{tuple};
            }
            if (p[0] == 1) {
                return eng::Run2Outcome{eng::Run2Trapped{}};
            }
            if (p[0] == 2) {
                return eng::Run2Outcome{eng::Run2HostAborted{}};
            }
            return std::unexpected(eng::EngineError::InstanceUnavailable);
        }
        if (frame->kind == kFrameImport) {
            const auto &p = frame->payload;
            if (p.size() != 12 + kPageBytes) {
                return std::unexpected(eng::EngineError::InstanceUnavailable);
            }
            const auto u32at = [&](std::size_t off) {
                return static_cast<std::uint32_t>(p[off]) |
                       (static_cast<std::uint32_t>(p[off + 1]) << 8) |
                       (static_cast<std::uint32_t>(p[off + 2]) << 16) |
                       (static_cast<std::uint32_t>(p[off + 3]) << 24);
            };
            const std::uint32_t ordinal = u32at(0);
            const std::uint32_t param_ptr = u32at(4);
            const std::uint32_t param_len = u32at(8);
            // The page is the authority for the whole-memory observation; the
            // L2 param span borrows into it and must stay inside the page.
            impl_->page.assign(p.begin() + 12, p.end());
            const std::uint64_t param_end =
                static_cast<std::uint64_t>(param_ptr) + static_cast<std::uint64_t>(param_len);
            if (param_end > kPageBytes || param_ptr > param_end) {
                static_cast<void>(impl_->send(kCmdImportAbort));
                return std::unexpected(eng::EngineError::InvalidSequence);
            }
            eng::ImportObservation observation;
            observation.import_ordinal = ordinal;
            // The child sends the first i32 arg as param_ptr; for a 1-param
            // import (bridge control-block pointer / section-9 scalar) it is
            // the authoritative scalar_arg, for a 2-param opaque import it is
            // the frame pointer (also the param_frame start).
            observation.scalar_arg = param_ptr;
            observation.param_frame =
                std::span<const std::uint8_t>(impl_->page.data() + param_ptr, param_len);
            observation.whole_memory = std::span<const std::uint8_t>(impl_->page);

            auto reply = impl_->callback(observation);
            if (std::holds_alternative<eng::ImportAbort>(reply)) {
                if (!impl_->send(kCmdImportAbort)) {
                    return std::unexpected(eng::EngineError::InstanceUnavailable);
                }
                continue;
            }
            const auto &answer = std::get<eng::ImportReply>(reply);
            // WH-3: the import-reply payload is the raw ahfl_cap_status word
            // followed by (ptr,len): 12 bytes. The child truncates the tuple
            // it returns to the import functype's result arity (3 for the
            // opaque lane, 2 for the bridge lane).
            std::uint8_t decision[12]{};
            decision[0] = static_cast<std::uint8_t>(answer.raw_status & 0xFFu);
            decision[1] = static_cast<std::uint8_t>((answer.raw_status >> 8) & 0xFFu);
            decision[2] = static_cast<std::uint8_t>((answer.raw_status >> 16) & 0xFFu);
            decision[3] = static_cast<std::uint8_t>((answer.raw_status >> 24) & 0xFFu);
            decision[4] = static_cast<std::uint8_t>(answer.result_ptr.value & 0xFFu);
            decision[5] = static_cast<std::uint8_t>((answer.result_ptr.value >> 8) & 0xFFu);
            decision[6] = static_cast<std::uint8_t>((answer.result_ptr.value >> 16) & 0xFFu);
            decision[7] = static_cast<std::uint8_t>((answer.result_ptr.value >> 24) & 0xFFu);
            decision[8] = static_cast<std::uint8_t>(answer.result_len & 0xFFu);
            decision[9] = static_cast<std::uint8_t>((answer.result_len >> 8) & 0xFFu);
            decision[10] = static_cast<std::uint8_t>((answer.result_len >> 16) & 0xFFu);
            decision[11] = static_cast<std::uint8_t>((answer.result_len >> 24) & 0xFFu);
            if (!impl_->send(kCmdImportReply, decision)) {
                return std::unexpected(eng::EngineError::InstanceUnavailable);
            }
            continue;
        }
        // Fatal or any unsolicited frame: the session is unusable.
        return std::unexpected(eng::EngineError::InstanceUnavailable);
    }
}

std::string node_resume_host_script() {
    return R"JS(// RFC 0026 KR6.5 E4-B2-D2a (F5): Node embedded resume engine. TEST SUPPORT.
// Synchronous stdio RPC peer of core_wasm_node_resume_engine.cpp. fds:
//   0 parent->child commands, 1 child->parent frames, 2 diagnostics.
// Every frame/command is u8 kind | u32le length | payload. The ahfl_cap import
// BLOCKS in readSync(0) after handing the parent the ordinal + Param view and
// the verbatim 64 KiB page, and resumes only when the parent's synchronous
// decision (reply ptr/len, or abort) arrives.
import fs from "node:fs";

const PAGE = 65536;
const K_READY = 0, K_IMPORT = 10, K_INSTANTIATED = 16, K_ALLOC = 17;
const K_MEMORY = 18, K_OUTCOME = 19, K_FATAL = 99;
const C_INSTANTIATE = 1, C_ALLOC = 2, C_RUN2 = 3, C_MEMORY = 4, C_BYE = 5;
const C_REPLY = 6, C_ABORT = 7;
const ABORT_MARKER = "__ahfl_host_abort__";

function readExact(fd, n) {
  const b = Buffer.alloc(n);
  let off = 0;
  while (off < n) {
    const r = fs.readSync(fd, b, off, n - off, null);
    if (r <= 0) process.exit(2);
    off += r;
  }
  return b;
}
function readFrame() {
  const h = readExact(0, 5);
  const n = h.readUInt32LE(1);
  return [h[0], n === 0 ? Buffer.alloc(0) : readExact(0, n)];
}
function writeAll(fd, b) {
  let off = 0;
  while (off < b.length) {
    const r = fs.writeSync(fd, b, off, b.length - off);
    if (r <= 0) process.exit(2);
    off += r;
  }
}
function writeFrame(kind, payload) {
  const p = payload === undefined ? Buffer.alloc(0) : payload;
  const h = Buffer.alloc(5);
  h[0] = kind;
  h.writeUInt32LE(p.length, 1);
  writeAll(1, h);
  writeAll(1, p);
}
function fatal(message) {
  try { writeFrame(K_FATAL, Buffer.from(String(message), "utf8")); } catch (_) {}
  process.exit(1);
}
function u32le(b, off) { return (b.readUInt32LE(off) >>> 0); }

let moduleBytes;
// WH-3 P1-2 regression: --self-test-arity <module> prints the import-arity
// table as JSON and exits, so the C++ test can assert the multi-type
// Type-section parse stays in sync through V8 (no faked parse).
const selfTestArity = process.argv[2] === "--self-test-arity";
try {
  moduleBytes = fs.readFileSync(selfTestArity ? process.argv[3] : process.argv[2]);
} catch (e) { fatal("cannot read module: " + e); }

let listed = [];
let compiled = null;
if (!selfTestArity) {
  try { compiled = new WebAssembly.Module(moduleBytes); }
  catch (e) { fatal("WebAssembly.compile failed: " + e); }

  listed = WebAssembly.Module.imports(compiled);
  const exported = WebAssembly.Module.exports(compiled);
  if (listed.length < 1 ||
      !listed.every((e) => e.module === "ahfl_cap" && e.name.startsWith("cap_") &&
                          e.kind === "function")) {
    fatal("unexpected import catalogue: " + JSON.stringify(listed));
  }
  if (!exported.some((e) => e.name === "memory" && e.kind === "memory") ||
      !exported.some((e) => e.name === "alloc" && e.kind === "function") ||
      !exported.some((e) => e.name === "run2" && e.kind === "function")) {
    fatal("missing memory/alloc/run2 exports: " + JSON.stringify(exported.map((e) => e.name)));
  }
}

// WH-3: parse the Type + Import sections at startup to build an
// ordinal->result-arity table. The parent's import-reply payload is always
// the symmetric (status,ptr,len) triple (12 bytes); the child truncates the
// tuple it returns to the import functype's result arity (3 for the opaque
// lane, 2 for the bridge lane) so V8's multi-value coercion never sees a
// length mismatch.
function parseImportArities(bytes) {
  let off = 8; // skip magic + version
  const typeResults = [];
  const importTypeIdx = [];
  function readUleb() {
    let v = 0, shift = 0;
    for (let i = 0; i < 5; i++) {
      const b = bytes[off++];
      v |= (b & 0x7f) << shift;
      if ((b & 0x80) === 0) return v >>> 0;
      shift += 7;
    }
    fatal("module section has a malformed u32 LEB");
  }
  while (off < bytes.length) {
    const sectionId = bytes[off++];
    const size = readUleb();
    const end = off + size;
    if (sectionId === 1) { // Type
      const count = readUleb();
      for (let t = 0; t < count; t++) {
        off++; // form 0x60
        const pc = readUleb();
        off += pc; // skip param value types
        const rc = readUleb();
        off += rc; // skip result value types
        typeResults.push(rc);
      }
    } else if (sectionId === 2) { // Import
      const count = readUleb();
      for (let i = 0; i < count; i++) {
        const mn = readUleb(); off += mn; // module name
        const fn = readUleb(); off += fn; // field name
        const kind = bytes[off++];
        const typeIdx = readUleb();
        if (kind === 0) importTypeIdx.push(typeIdx);
      }
    }
    off = end;
  }
  return importTypeIdx.map((idx) => typeResults[idx] ?? 0);
}
const importArities = parseImportArities(moduleBytes);
if (!selfTestArity && importArities.length !== listed.length) {
  fatal("import arity table disagrees with the import catalogue");
}

// WH-3 P1-2 regression self-test: print the arity table and exit. The C++
// test feeds a synthetic module whose Type section carries the opaque tuple,
// the bridge signature, and an unrelated (i32)->i32 type, with imports
// referencing the first two, and asserts the table is exactly [3,2].
if (selfTestArity) {
  process.stdout.write(JSON.stringify(importArities) + "\n");
  process.exit(0);
}

let instance = null;
let exports_ = null;

function pageCopy() { return Buffer.from(new Uint8Array(exports_.memory.buffer)); }

function allocWrite(payload) {
  const n = u32le(payload, 0);
  const ptr = exports_.alloc(n) >>> 0;
  if (ptr !== 0 && n !== 0) {
    new Uint8Array(exports_.memory.buffer, ptr, n).set(payload.subarray(4, 4 + n));
  }
  const r = Buffer.alloc(4);
  r.writeUInt32LE(ptr, 0);
  writeFrame(K_ALLOC, r);
}

// One synchronous ahfl_cap import callback, parameterized by its import
// ordinal. A 2-param opaque import receives (ptr,len); a 1-param bridge import
// receives the single control-block pointer (len is undefined). The
// observation header carries (ordinal, param_ptr, param_len); for a 1-param
// import param_len is 0 and param_ptr is the scalar arg. The reply tuple is
// truncated to the import's result arity.
function makeCapCallback(ordinal) {
  return function (...args) {
    const ptr = args.length >= 1 ? (args[0] >>> 0) : 0;
    const len = args.length >= 2 ? (args[1] >>> 0) : 0;
    const h = Buffer.alloc(12);
    h.writeUInt32LE(ordinal, 0);
    h.writeUInt32LE(ptr, 4);
    h.writeUInt32LE(len, 8);
    writeFrame(K_IMPORT, Buffer.concat([h, pageCopy()]));
    // Parked: serve nested allocations until the terminal host decision.
    for (;;) {
      const [kind, payload] = readFrame();
      if (kind === C_ALLOC) { allocWrite(payload); continue; }
      if (kind === C_REPLY) {
        // The parent always sends (status, ptr, len); truncate to the import
        // functype's result arity (3 opaque / 2 bridge).
        const tuple = [u32le(payload, 0), u32le(payload, 4), u32le(payload, 8)];
        return tuple.slice(0, importArities[ordinal]);
      }
      if (kind === C_ABORT) throw new Error(ABORT_MARKER);
      fatal("unexpected command while import parked: " + kind);
    }
  };
}

writeFrame(K_READY, Buffer.alloc(0));

for (;;) {
  const [kind, payload] = readFrame();
  if (kind === C_BYE) process.exit(0);
  if (kind === C_INSTANTIATE) {
    try {
      const capImports = {};
      for (let i = 0; i < listed.length; i++) {
        capImports[listed[i].name] = makeCapCallback(i);
      }
      instance = new WebAssembly.Instance(compiled, {ahfl_cap: capImports});
      exports_ = instance.exports;
      if (exports_.memory.buffer.byteLength !== PAGE) {
        fatal("module memory is not the fixed single 64 KiB page");
      }
      writeFrame(K_INSTANTIATED, Buffer.alloc(0));
    } catch (e) { fatal("instantiate failed: " + e); }
  } else if (kind === C_ALLOC) {
    allocWrite(payload);
  } else if (kind === C_MEMORY) {
    writeFrame(K_MEMORY, pageCopy());
  } else if (kind === C_RUN2) {
    const ptr = u32le(payload, 0);
    const len = u32le(payload, 4);
    const out = Buffer.alloc(13);
    try {
      const tuple = exports_.run2(ptr, len);
      out[0] = 0;
      out.writeUInt32LE(tuple[0] >>> 0, 1);
      out.writeUInt32LE(tuple[1] >>> 0, 5);
      out.writeUInt32LE(tuple[2] >>> 0, 9);
      writeFrame(K_OUTCOME, out.subarray(0, 13));
    } catch (e) {
      out[0] = String(e).includes(ABORT_MARKER) ? 2 : 1;
      writeFrame(K_OUTCOME, out.subarray(0, 1));
    }
  } else {
    fatal("unexpected outer command: " + kind);
  }
}
)JS";
}

std::expected<NodeResumeEngine, NodeEngineLaunchError> launch_node_resume_engine(
    std::string node_executable, std::string host_script_path, std::string module_path) {
    auto impl = std::make_unique<NodeResumeEngine::Impl>();

    // Bind the port to the exact bytes the child will compile. A read failure
    // here is a launch failure, not a later fresh_instance surprise.
    auto launch_bytes = read_module_file(module_path);
    if (!launch_bytes.has_value() || launch_bytes->empty()) {
        return std::unexpected(NodeEngineLaunchError{NodeEngineError::ScriptFailed,
                                                     "cannot read launch module: " + module_path});
    }
    impl->launch_module_bytes = std::move(*launch_bytes);

    int cmd_pair[2] = {-1, -1};
    int frame_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    // The command channel is a SOCK_STREAM socketpair so every command byte can
    // be sent with MSG_NOSIGNAL: a write to a dead child then surfaces as EPIPE
    // -> typed InstanceUnavailable instead of a SIGPIPE that signal-kills the
    // host. The frames and diagnostics channels are only ever read by the parent,
    // so they stay anonymous pipes. All parent ends are close-on-exec.
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, cmd_pair) != 0 ||
        ::pipe(frame_pipe) != 0 || ::pipe(stderr_pipe) != 0) {
        const int saved_errno = errno;
        for (const int fd : {cmd_pair[0], cmd_pair[1], frame_pipe[0], frame_pipe[1],
                             stderr_pipe[0], stderr_pipe[1]}) {
            if (fd >= 0) {
                ::close(fd);
            }
        }
        return std::unexpected(
            NodeEngineLaunchError{NodeEngineError::PipeFailed, std::strerror(saved_errno)});
    }
    for (const int fd : {cmd_pair[0], cmd_pair[1], frame_pipe[0], frame_pipe[1],
                         stderr_pipe[0], stderr_pipe[1]}) {
        static_cast<void>(::fcntl(fd, F_SETFD, FD_CLOEXEC));
    }
    // Parent ends; child ends are dup2'd by the spawn actions.
    impl->cmd_write = Fd{cmd_pair[1]};
    impl->frame_read = Fd{frame_pipe[0]};
    impl->stderr_read = Fd{stderr_pipe[0]};
    Fd child_cmd_read{cmd_pair[0]};
    Fd child_frame_write{frame_pipe[1]};
    Fd child_stderr_write{stderr_pipe[1]};

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, child_cmd_read.get(), STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, child_frame_write.get(), STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, child_stderr_write.get(), STDERR_FILENO);
    // The child would otherwise inherit a duplicate of EVERY parent-made fd: the
    // dup2 sources plus the three parent-only ends. Close them all so the child's
    // fd table is exactly its three stdio descriptors -- the repo reference
    // src/base/support/process.cpp:196 addclose()'s its unused end for the same
    // reason. In particular an extra child copy of the frame WRITE end keeps the
    // parent's frame_read from ever seeing EOF if the script execs/spawns a
    // descendant that inherits it, and the parent-only command write-end copy
    // would keep the child's command read alive past the parent's close.
    posix_spawn_file_actions_addclose(&actions, child_cmd_read.get());
    posix_spawn_file_actions_addclose(&actions, child_frame_write.get());
    posix_spawn_file_actions_addclose(&actions, child_stderr_write.get());
    posix_spawn_file_actions_addclose(&actions, impl->cmd_write.get());
    posix_spawn_file_actions_addclose(&actions, impl->frame_read.get());
    posix_spawn_file_actions_addclose(&actions, impl->stderr_read.get());

    std::vector<std::string> arguments_storage = {node_executable, host_script_path, module_path};
    std::vector<char *> argv;
    argv.reserve(arguments_storage.size() + 1);
    for (auto &a : arguments_storage) {
        argv.push_back(a.data());
    }
    argv.push_back(nullptr);

    pid_t pid = -1;
    const int spawn_error =
        posix_spawnp(&pid, node_executable.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    // The child fd copies are consumed (dup'd) by the spawn; close them in the
    // parent regardless of spawn success so EOF detection works.
    child_cmd_read.reset();
    child_frame_write.reset();
    child_stderr_write.reset();
    if (spawn_error != 0) {
        return std::unexpected(
            NodeEngineLaunchError{NodeEngineError::SpawnFailed, std::strerror(spawn_error)});
    }
    impl->pid = pid;
    NodeResumeEngine::Impl *impl_raw = impl.get();
    impl->stderr_drain = std::thread([impl_raw] { impl_raw->drain_stderr(); });

    // Wait for startup: ready (kind 0) or fatal (kind 99).
    bool eof = false;
    auto frame = impl->recv(eof, read_bound());
    if (!frame.has_value()) {
        const auto reason = eof ? NodeEngineError::ChildExited : NodeEngineError::ProtocolFailed;
        std::string diag = impl->diagnostic();
        return std::unexpected(NodeEngineLaunchError{reason, std::move(diag)});
    }
    if (frame->kind == kFrameFatal) {
        std::string diag(reinterpret_cast<const char *>(frame->payload.data()),
                         frame->payload.size());
        return std::unexpected(
            NodeEngineLaunchError{NodeEngineError::ProtocolFailed, std::move(diag)});
    }
    if (frame->kind != kFrameReady || !frame->payload.empty()) {
        return std::unexpected(
            NodeEngineLaunchError{NodeEngineError::ProtocolFailed, impl->diagnostic()});
    }
    return NodeResumeEngine{std::move(impl)};
}

} // namespace ahfl::runtime::core_wasm_node_resume_engine
