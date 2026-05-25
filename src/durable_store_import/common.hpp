#pragma once

namespace ahfl::durable_store_import {

/// Adapter capability kinds - shared across all durable store import artifacts
enum class AdapterCapabilityKind {
    // V0.16 baseline
    ConsumeStoreImportDescriptor,
    ConsumeExportManifest,
    ConsumePersistenceDescriptor,
    ConsumeHumanReviewContext,
    ConsumeCheckpointRecord,
    PreservePartialWorkflowState,
};

} // namespace ahfl::durable_store_import
