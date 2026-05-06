#pragma once

#include "ahfl/cli/command_catalog.hpp"
#include "ahfl/dry_run/runner.hpp"
#include "ahfl/handoff/package.hpp"
#include "ahfl/ir/ir.hpp"

namespace ahfl::cli {

[[nodiscard]] int
emit_execution_plan_with_diagnostics(const ahfl::ir::Program &program,
                                     const ahfl::handoff::PackageMetadata &metadata);

[[nodiscard]] int
emit_dry_run_trace_with_diagnostics(const ahfl::ir::Program &program,
                                    const ahfl::handoff::PackageMetadata &metadata,
                                    const ahfl::dry_run::CapabilityMockSet &mock_set,
                                    const CommandLineOptions &options);

[[nodiscard]] int
emit_runtime_session_with_diagnostics(const ahfl::ir::Program &program,
                                      const ahfl::handoff::PackageMetadata &metadata,
                                      const ahfl::dry_run::CapabilityMockSet &mock_set,
                                      const CommandLineOptions &options);

[[nodiscard]] int
emit_execution_journal_with_diagnostics(const ahfl::ir::Program &program,
                                        const ahfl::handoff::PackageMetadata &metadata,
                                        const ahfl::dry_run::CapabilityMockSet &mock_set,
                                        const CommandLineOptions &options);

[[nodiscard]] int
emit_replay_view_with_diagnostics(const ahfl::ir::Program &program,
                                  const ahfl::handoff::PackageMetadata &metadata,
                                  const ahfl::dry_run::CapabilityMockSet &mock_set,
                                  const CommandLineOptions &options);

[[nodiscard]] int
emit_audit_report_with_diagnostics(const ahfl::ir::Program &program,
                                   const ahfl::handoff::PackageMetadata &metadata,
                                   const ahfl::dry_run::CapabilityMockSet &mock_set,
                                   const CommandLineOptions &options);

[[nodiscard]] int
emit_scheduler_snapshot_with_diagnostics(const ahfl::ir::Program &program,
                                         const ahfl::handoff::PackageMetadata &metadata,
                                         const ahfl::dry_run::CapabilityMockSet &mock_set,
                                         const CommandLineOptions &options);

[[nodiscard]] int
emit_scheduler_review_with_diagnostics(const ahfl::ir::Program &program,
                                       const ahfl::handoff::PackageMetadata &metadata,
                                       const ahfl::dry_run::CapabilityMockSet &mock_set,
                                       const CommandLineOptions &options);

[[nodiscard]] int
emit_checkpoint_record_with_diagnostics(const ahfl::ir::Program &program,
                                        const ahfl::handoff::PackageMetadata &metadata,
                                        const ahfl::dry_run::CapabilityMockSet &mock_set,
                                        const CommandLineOptions &options);

[[nodiscard]] int
emit_checkpoint_review_with_diagnostics(const ahfl::ir::Program &program,
                                        const ahfl::handoff::PackageMetadata &metadata,
                                        const ahfl::dry_run::CapabilityMockSet &mock_set,
                                        const CommandLineOptions &options);

[[nodiscard]] int
emit_persistence_descriptor_with_diagnostics(const ahfl::ir::Program &program,
                                             const ahfl::handoff::PackageMetadata &metadata,
                                             const ahfl::dry_run::CapabilityMockSet &mock_set,
                                             const CommandLineOptions &options);

[[nodiscard]] int
emit_persistence_review_with_diagnostics(const ahfl::ir::Program &program,
                                         const ahfl::handoff::PackageMetadata &metadata,
                                         const ahfl::dry_run::CapabilityMockSet &mock_set,
                                         const CommandLineOptions &options);

[[nodiscard]] int
emit_export_manifest_with_diagnostics(const ahfl::ir::Program &program,
                                      const ahfl::handoff::PackageMetadata &metadata,
                                      const ahfl::dry_run::CapabilityMockSet &mock_set,
                                      const CommandLineOptions &options);

[[nodiscard]] int
emit_export_review_with_diagnostics(const ahfl::ir::Program &program,
                                    const ahfl::handoff::PackageMetadata &metadata,
                                    const ahfl::dry_run::CapabilityMockSet &mock_set,
                                    const CommandLineOptions &options);

[[nodiscard]] int
emit_store_import_descriptor_with_diagnostics(const ahfl::ir::Program &program,
                                              const ahfl::handoff::PackageMetadata &metadata,
                                              const ahfl::dry_run::CapabilityMockSet &mock_set,
                                              const CommandLineOptions &options);

[[nodiscard]] int
emit_store_import_review_with_diagnostics(const ahfl::ir::Program &program,
                                          const ahfl::handoff::PackageMetadata &metadata,
                                          const ahfl::dry_run::CapabilityMockSet &mock_set,
                                          const CommandLineOptions &options);

} // namespace ahfl::cli
