#pragma once

#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/base/support/source.hpp"

#include <functional>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string_view>

namespace ahfl::cli {

/// Abstract base for consuming diagnostics from the CLI pipeline.
class DiagnosticConsumer {
  public:
    virtual ~DiagnosticConsumer() = default;

    /// Consume a batch of diagnostics, optionally with source context.
    virtual void consume(
        const ahfl::DiagnosticBag &bag,
        std::optional<std::reference_wrapper<const ahfl::SourceFile>> source = std::nullopt) = 0;

    /// Called once when the CLI is about to exit — flush buffered state.
    virtual void finalize() {}
};

/// Default consumer: renders text diagnostics to a stream (current behavior).
class TextDiagnosticConsumer final : public DiagnosticConsumer {
  public:
    explicit TextDiagnosticConsumer(std::ostream &out);

    void consume(const ahfl::DiagnosticBag &bag,
                 std::optional<std::reference_wrapper<const ahfl::SourceFile>> source =
                     std::nullopt) override;

  private:
    std::ostream &out_;
};

/// JSON Lines diagnostic consumer for tooling/IDE integration.
class JsonDiagnosticConsumer final : public DiagnosticConsumer {
  public:
    explicit JsonDiagnosticConsumer(std::ostream &out);

    void consume(const ahfl::DiagnosticBag &bag,
                 std::optional<std::reference_wrapper<const ahfl::SourceFile>> source =
                     std::nullopt) override;

    void finalize() override;

  private:
    std::ostream &out_;
    bool first_{true};
};

/// Factory: create appropriate consumer based on format string.
/// Supported formats: "text" (default), "json"
[[nodiscard]] std::unique_ptr<DiagnosticConsumer> make_diagnostic_consumer(std::string_view format,
                                                                           std::ostream &out);

} // namespace ahfl::cli
