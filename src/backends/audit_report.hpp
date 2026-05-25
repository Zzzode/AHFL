#pragma once

#include <ostream>

#include "audit_report/report.hpp"

namespace ahfl {

void print_audit_report_json(const audit_report::AuditReport &report, std::ostream &out);

} // namespace ahfl
