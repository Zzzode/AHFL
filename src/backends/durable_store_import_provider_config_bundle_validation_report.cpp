#include "ahfl/backends/durable_store_import_provider_config_bundle_validation_report.hpp"

#include <ostream>

namespace ahfl {
namespace {

void print_validation_status(durable_store_import::ConfigValidationStatus status,
                             std::ostream &out) {
    switch (status) {
    case durable_store_import::ConfigValidationStatus::Valid:
        out << "valid";
        return;
    case durable_store_import::ConfigValidationStatus::Invalid:
        out << "invalid";
        return;
    case durable_store_import::ConfigValidationStatus::Missing:
        out << "missing";
        return;
    }
}

} // namespace

void print_durable_store_import_provider_config_bundle_validation_report(
    const durable_store_import::ProviderConfigBundleValidationReport &report, std::ostream &out) {
    out << report.format_version << '\n';
    out << "workflow " << report.workflow_canonical_name << '\n';
    out << "session " << report.session_id << '\n';
    out << "config_bundle " << report.config_bundle_identity << '\n';

    // Provider references
    for (const auto &ref : report.provider_references) {
        out << "provider_reference " << ref.provider_name << " ";
        print_validation_status(ref.status, out);
        if (ref.validation_error.has_value()) {
            out << " error=" << *ref.validation_error;
        }
        out << '\n';
    }

    // Secret handles（redacted 输出，不暴露 secret value）
    for (const auto &sh : report.secret_handles) {
        out << "secret_handle " << sh.secret_name << " scope=" << sh.secret_scope
            << " presence=";
        print_validation_status(sh.presence_status, out);
        out << " scope_status=";
        print_validation_status(sh.scope_status, out);
        out << " redaction=" << (sh.has_redaction_policy ? "yes" : "no") << '\n';
    }

    // Endpoint shapes
    for (const auto &ep : report.endpoint_shapes) {
        out << "endpoint_shape " << ep.endpoint_name << " expected=" << ep.expected_shape << " ";
        print_validation_status(ep.status, out);
        out << '\n';
    }

    // Environment bindings
    for (const auto &eb : report.environment_bindings) {
        out << "environment_binding " << eb.binding_name << " ";
        print_validation_status(eb.status, out);
        out << '\n';
    }

    // Summary
    out << "summary " << report.validation_summary << '\n';
    out << "blocking " << report.blocking_summary << '\n';
}

} // namespace ahfl
